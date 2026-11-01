/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * mynetworkmonitor-scan: runs one scan and writes the result as JSON.
 *
 * It is started by the LuCI backend for each scan and ends when the scan is
 * done; it never listens on a port. Progress goes to a small file that the
 * page polls, the result to a second file. Both are replaced atomically
 * (written to a temporary name, then renamed), so a reader never sees half a
 * file.
 */
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "json.h"
#include "ports.h"
#include "targets.h"
#include "tcpscan.h"

#ifndef MNM_VERSION
#define MNM_VERSION "unknown"
#endif

/* Version of the layout of result.json; the page refuses files of another format. */
#define RESULT_FORMAT 1

struct host {
	uint32_t address;
	uint16_t *open;
	size_t count, capacity;
};

struct scan {
	struct host *hosts;
	size_t count, capacity;
	const char *progress_path;
	time_t started;
	int64_t last_progress_ms;
	uint64_t done;   /* attempts finished so far */
	int failed;   /* out of memory while recording a result */
};

static volatile sig_atomic_t stop_requested;

static void on_signal(int signal)
{
	(void)signal;
	stop_requested = 1;
}

static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Writes a file under a temporary name and renames it into place. */
static FILE *begin_file(const char *path, char *tmp, size_t tmplen)
{
	snprintf(tmp, tmplen, "%s.tmp", path);
	return fopen(tmp, "w");
}

static int end_file(FILE *f, const char *tmp, const char *path)
{
	int ok = fflush(f) == 0 && !ferror(f);
	if (fclose(f) != 0)
		ok = 0;
	if (!ok || rename(tmp, path) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

static void write_progress(const struct scan *scan, const char *state, uint64_t done, uint64_t total)
{
	char tmp[4096];
	FILE *f;

	if (!scan->progress_path)
		return;
	f = begin_file(scan->progress_path, tmp, sizeof(tmp));
	if (!f)
		return;
	fprintf(f, "{\"format\":%d,\"state\":\"%s\",\"done\":%llu,\"total\":%llu,\"hosts\":%zu}\n", RESULT_FORMAT,
		state, (unsigned long long)done, (unsigned long long)total, scan->count);
	end_file(f, tmp, scan->progress_path);
}

static void on_open(void *context, uint32_t address, uint16_t port)
{
	struct scan *scan = context;
	struct host *host = NULL;

	/* Open ports arrive interleaved over the targets; the number of hosts with
	 * open ports is small, so a linear search is cheaper than an index. */
	for (size_t i = 0; i < scan->count; i++) {
		if (scan->hosts[i].address == address) {
			host = &scan->hosts[i];
			break;
		}
	}
	if (!host) {
		if (scan->count == scan->capacity) {
			size_t grown = scan->capacity ? scan->capacity * 2 : 16;
			struct host *h = realloc(scan->hosts, grown * sizeof(*h));
			if (!h) {
				scan->failed = 1;
				return;
			}
			scan->hosts = h;
			scan->capacity = grown;
		}
		host = &scan->hosts[scan->count++];
		memset(host, 0, sizeof(*host));
		host->address = address;
	}
	if (host->count == host->capacity) {
		size_t grown = host->capacity ? host->capacity * 2 : 8;
		uint16_t *p = realloc(host->open, grown * sizeof(*p));
		if (!p) {
			scan->failed = 1;
			return;
		}
		host->open = p;
		host->capacity = grown;
	}
	host->open[host->count++] = port;
}

static void on_progress(void *context, uint64_t done, uint64_t total)
{
	struct scan *scan = context;
	int64_t now = now_ms();

	scan->done = done;

	/* At most twice a second: flash memory is not involved (/tmp is RAM), but
	 * thousands of renames per second would still cost the router CPU time. */
	if (now - scan->last_progress_ms >= 500 || done == total) {
		scan->last_progress_ms = now;
		write_progress(scan, "running", done, total);
	}
}

static int compare_hosts(const void *a, const void *b)
{
	uint32_t x = ((const struct host *)a)->address, y = ((const struct host *)b)->address;
	return x < y ? -1 : x > y;
}

static int compare_ports(const void *a, const void *b)
{
	return (int)*(const uint16_t *)a - (int)*(const uint16_t *)b;
}

static int write_result(struct scan *scan, const char *path, const char *targets_text, const char *ports_text,
			int partial)
{
	char tmp[4096];
	char address[16];
	FILE *f = begin_file(path, tmp, sizeof(tmp));

	if (!f)
		return -1;
	qsort(scan->hosts, scan->count, sizeof(*scan->hosts), compare_hosts);

	fprintf(f, "{\"format\":%d,\"version\":", RESULT_FORMAT);
	mnm_json_string(f, MNM_VERSION, strlen(MNM_VERSION));
	fprintf(f, ",\"started\":%lld,\"finished\":%lld,\"partial\":%s,\"targets\":", (long long)scan->started,
		(long long)time(NULL), partial ? "true" : "false");
	mnm_json_string(f, targets_text, strlen(targets_text));
	fputs(",\"ports\":", f);
	mnm_json_string(f, ports_text, strlen(ports_text));
	fputs(",\"hosts\":[", f);
	for (size_t i = 0; i < scan->count; i++) {
		struct host *h = &scan->hosts[i];
		qsort(h->open, h->count, sizeof(*h->open), compare_ports);
		mnm_format_ipv4(h->address, address);
		fprintf(f, "%s{\"address\":\"%s\",\"tcp\":[", i ? "," : "", address);
		for (size_t k = 0; k < h->count; k++)
			fprintf(f, "%s%u", k ? "," : "", h->open[k]);
		fputs("]}", f);
	}
	fputs("]}\n", f);
	return end_file(f, tmp, path);
}

static void usage(FILE *out)
{
	fputs("Usage: mynetworkmonitor-scan --targets LIST --ports LIST --output FILE [options]\n"
	      "\n"
	      "  --targets LIST      addresses, ranges or networks, e.g. 192.0.2.1-254,198.51.100.0/24\n"
	      "  --ports LIST        TCP ports and ranges, e.g. 22,80,443,8000-8100\n"
	      "  --output FILE       result as JSON, replaced when the scan ends\n"
	      "  --progress FILE     progress as JSON, updated while the scan runs\n"
	      "  --timeout-ms N      time per connection attempt (50-10000, default 800)\n"
	      "  --parallel N        connections at the same time (1-256, default 64)\n"
	      "  --allow-public      also scan addresses outside the private ranges\n"
	      "  --version           print the version\n",
	      out);
}

static int parse_int(const char *text, int min, int max, int *out)
{
	char *end;
	long value;

	errno = 0;
	value = strtol(text, &end, 10);
	if (errno || end == text || *end || value < min || value > max)
		return -1;
	*out = (int)value;
	return 0;
}

int main(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "targets", required_argument, NULL, 't' },
		{ "ports", required_argument, NULL, 'p' },
		{ "output", required_argument, NULL, 'o' },
		{ "progress", required_argument, NULL, 'g' },
		{ "timeout-ms", required_argument, NULL, 'w' },
		{ "parallel", required_argument, NULL, 'j' },
		{ "allow-public", no_argument, NULL, 'P' },
		{ "version", no_argument, NULL, 'V' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	const char *targets_text = NULL, *ports_text = NULL, *output = NULL;
	struct scan scan = { 0 };
	struct mnm_targets targets;
	struct mnm_ports ports;
	struct mnm_tcpscan_options options = { .parallel = 64, .timeout_ms = 800 };
	char err[160];
	int allow_public = 0;
	int c;

	while ((c = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
		switch (c) {
		case 't': targets_text = optarg; break;
		case 'p': ports_text = optarg; break;
		case 'o': output = optarg; break;
		case 'g': scan.progress_path = optarg; break;
		case 'w':
			if (parse_int(optarg, 50, 10000, &options.timeout_ms) != 0) {
				fprintf(stderr, "--timeout-ms: expected 50 to 10000\n");
				return 2;
			}
			break;
		case 'j':
			if (parse_int(optarg, 1, MNM_TCPSCAN_MAX_PARALLEL, &options.parallel) != 0) {
				fprintf(stderr, "--parallel: expected 1 to %d\n", MNM_TCPSCAN_MAX_PARALLEL);
				return 2;
			}
			break;
		case 'P': allow_public = 1; break;
		case 'V': puts(MNM_VERSION); return 0;
		case 'h': usage(stdout); return 0;
		default: usage(stderr); return 2;
		}
	}
	if (!targets_text || !ports_text || !output) {
		usage(stderr);
		return 2;
	}
	if (mnm_targets_parse(targets_text, &targets, err, sizeof(err)) != 0) {
		fprintf(stderr, "--targets: %s\n", err);
		return 2;
	}
	if (!allow_public && !mnm_targets_all_private(&targets, err, sizeof(err))) {
		fprintf(stderr, "--targets: %s\n", err);
		mnm_targets_free(&targets);
		return 2;
	}
	if (mnm_ports_parse(ports_text, &ports, err, sizeof(err)) != 0) {
		fprintf(stderr, "--ports: %s\n", err);
		mnm_targets_free(&targets);
		return 2;
	}

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	scan.started = time(NULL);
	options.stop = &stop_requested;
	options.on_open = on_open;
	options.on_progress = on_progress;
	options.context = &scan;

	uint64_t total = (uint64_t)targets.total * ports.count;
	write_progress(&scan, "running", 0, total);
	int outcome = mnm_tcpscan_run(&targets, &ports, &options);
	int status = 0;

	if (outcome < 0) {
		fprintf(stderr, "scan could not start: out of memory\n");
		write_progress(&scan, "failed", 0, total);
		status = 1;
	} else {
		if (write_result(&scan, output, targets_text, ports_text, outcome == 1 || scan.failed) != 0) {
			fprintf(stderr, "%s: cannot write result\n", output);
			status = 1;
		}
		write_progress(&scan, outcome == 1 ? "stopped" : "done", scan.done, total);
	}

	for (size_t i = 0; i < scan.count; i++)
		free(scan.hosts[i].open);
	free(scan.hosts);
	mnm_targets_free(&targets);
	mnm_ports_free(&ports);
	return status;
}
