/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * mynetworkmonitor-scan: runs one scan and writes what it finds as JSON.
 *
 * It is started by the LuCI backend for each scan and ends when the scan is
 * done; it never listens on a port. The scan runs in three phases: first the
 * devices are found (ARP, ping, neighbour table, DHCP leases, vendor), then
 * their TCP ports are tried, then the services behind the open ports are
 * recognised by their answer to a probe. The output file is rewritten at most once a
 * second while the scan runs, so that the page shows each device as soon as it
 * is found; it is replaced atomically (written to a temporary name, then
 * renamed), so a reader never sees half a file.
 */
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "devices.h"
#include "discover.h"
#include "json.h"
#include "probe_engine.h"
#include "ports.h"
#include "targets.h"
#include "tcpscan.h"

#ifndef MNM_VERSION
#define MNM_VERSION "unknown"
#endif

/* Version of the layout of the output file; the backend refuses other formats. */
#define OUTPUT_FORMAT 2

struct scan {
	struct mnm_devices devices;
	const char *output;
	const char *targets_text, *ports_text;
	const char *state, *phase;
	time_t started, finished;
	uint64_t done, total;
	int64_t last_write_ms;
};

/* A probe and the ports it is tried on (--probe id=ports). */
struct probe_map {
	const struct mnm_probe *probe;
	struct mnm_ports ports;
};

#define MAX_PROBE_MAPS 64

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

static int write_output(struct scan *scan)
{
	char tmp[4096];
	snprintf(tmp, sizeof(tmp), "%s.tmp", scan->output);
	FILE *f = fopen(tmp, "w");
	if (!f)
		return -1;

	fprintf(f, "{\"format\":%d,\"version\":", OUTPUT_FORMAT);
	mnm_json_string(f, MNM_VERSION, strlen(MNM_VERSION));
	fprintf(f, ",\"state\":\"%s\",\"phase\":\"%s\",\"started\":%lld,\"finished\":%lld,\"done\":%llu,\"total\":%llu,"
		   "\"targets\":", scan->state, scan->phase, (long long)scan->started, (long long)scan->finished,
		(unsigned long long)scan->done, (unsigned long long)scan->total);
	mnm_json_string(f, scan->targets_text, strlen(scan->targets_text));
	fputs(",\"ports\":", f);
	mnm_json_string(f, scan->ports_text, strlen(scan->ports_text));
	fputs(",\"devices\":", f);
	mnm_devices_write_json(&scan->devices, f);
	fputs("}\n", f);

	int ok = fflush(f) == 0 && !ferror(f);
	if (fclose(f) != 0)
		ok = 0;
	if (!ok || rename(tmp, scan->output) != 0) {
		unlink(tmp);
		return -1;
	}
	scan->last_write_ms = now_ms();
	return 0;
}

/* Writes if the last write is at least a second ago. /tmp is RAM, but
 * rewriting the file for every single answer would still cost a small router
 * noticeable CPU time. */
static void write_soon(struct scan *scan)
{
	if (now_ms() - scan->last_write_ms >= 1000)
		write_output(scan);
}

static void on_discover_change(void *context)
{
	write_soon(context);
}

static void on_open(void *context, uint32_t address, uint16_t port)
{
	struct scan *scan = context;
	struct mnm_device *d = mnm_devices_find(&scan->devices, address);
	if (!d)
		return;
	mnm_devices_add_tcp(&scan->devices, d, port);
	mnm_devices_mark(&scan->devices, d, MNM_SEEN_TCP);
}

static void on_service(void *context, uint32_t address, uint16_t port, const struct mnm_probe *probe,
		       const char *detail)
{
	struct scan *scan = context;
	struct mnm_device *d = mnm_devices_find(&scan->devices, address);
	if (d)
		mnm_devices_add_service(&scan->devices, d, probe->id, probe->name, port, detail);
}

/* "ssh=22" or "web=80,443,8080": a known probe and its ports. */
static int parse_probe_map(const char *text, struct probe_map *map, char *err, size_t errlen)
{
	const char *eq = strchr(text, '=');
	char id[32];

	if (!eq || eq == text || (size_t)(eq - text) >= sizeof(id)) {
		snprintf(err, errlen, "expected id=ports, got \"%.40s\"", text);
		return -1;
	}
	memcpy(id, text, (size_t)(eq - text));
	id[eq - text] = '\0';
	map->probe = mnm_probe_find(id);
	if (!map->probe) {
		snprintf(err, errlen, "unknown probe \"%s\"", id);
		return -1;
	}
	return mnm_ports_parse(eq + 1, &map->ports, err, errlen);
}

static int map_has_port(const struct probe_map *map, uint16_t port)
{
	for (size_t i = 0; i < map->ports.count; i++)
		if (map->ports.list[i] == port)
			return 1;
	return 0;
}

static void on_progress(void *context, uint64_t done, uint64_t total)
{
	struct scan *scan = context;
	scan->done = done;
	scan->total = total;
	write_soon(scan);
}

/* The found devices as a target list for the port phase, one range per device. */
static int devices_as_targets(const struct mnm_devices *devices, struct mnm_targets *targets)
{
	targets->ranges = calloc(devices->count ? devices->count : 1, sizeof(*targets->ranges));
	targets->count = 0;
	targets->total = 0;
	if (!targets->ranges)
		return -1;
	for (size_t i = 0; i < devices->count; i++) {
		targets->ranges[i].first = targets->ranges[i].last = devices->list[i].ipv4;
		targets->count++;
		targets->total++;
	}
	return 0;
}

static void usage(FILE *out)
{
	fputs("Usage: mynetworkmonitor-scan --targets LIST --output FILE [options]\n"
	      "\n"
	      "  --targets LIST      addresses, ranges or networks, e.g. 192.168.1.0/24,192.168.2.1-50\n"
	      "  --ports LIST        TCP ports and ranges to try on every device found; none: no port scan\n"
	      "  --output FILE       devices found, as JSON, rewritten while the scan runs\n"
	      "  --leases FILE       dnsmasq lease file for host names (default /tmp/dhcp.leases)\n"
	      "  --vendors FILE      MAC vendor list in the \"manuf\" format\n"
	      "  --timeout-ms N      time to wait for answers (50-10000, default 800)\n"
	      "  --parallel N        TCP connections at the same time (1-256, default 64)\n"
	      "  --no-ping           find devices by ARP and the neighbour table only\n"
	      "  --probe ID=PORTS    recognise a service on these ports by its answer, e.g. ssh=22 (repeatable)\n"
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
		{ "leases", required_argument, NULL, 'l' },
		{ "vendors", required_argument, NULL, 'v' },
		{ "timeout-ms", required_argument, NULL, 'w' },
		{ "parallel", required_argument, NULL, 'j' },
		{ "allow-public", no_argument, NULL, 'P' },
		{ "no-ping", no_argument, NULL, 'N' },
		{ "probe", required_argument, NULL, 'b' },
		{ "version", no_argument, NULL, 'V' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	const char *leases = "/tmp/dhcp.leases", *vendors = NULL;
	struct scan scan = { .targets_text = NULL, .ports_text = "", .state = "running", .phase = "discovery" };
	struct mnm_targets targets;
	struct mnm_ports ports = { 0 };
	struct mnm_tcpscan_options tcp = { .parallel = 64, .timeout_ms = 800 };
	char err[200];
	int allow_public = 0, ping = 1;
	struct probe_map maps[MAX_PROBE_MAPS];
	size_t map_count = 0;
	int c;

	while ((c = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
		switch (c) {
		case 't': scan.targets_text = optarg; break;
		case 'p': scan.ports_text = optarg; break;
		case 'o': scan.output = optarg; break;
		case 'l': leases = optarg; break;
		case 'v': vendors = optarg; break;
		case 'w':
			if (parse_int(optarg, 50, 10000, &tcp.timeout_ms) != 0) {
				fprintf(stderr, "--timeout-ms: expected 50 to 10000\n");
				return 2;
			}
			break;
		case 'j':
			if (parse_int(optarg, 1, MNM_TCPSCAN_MAX_PARALLEL, &tcp.parallel) != 0) {
				fprintf(stderr, "--parallel: expected 1 to %d\n", MNM_TCPSCAN_MAX_PARALLEL);
				return 2;
			}
			break;
		case 'P': allow_public = 1; break;
		case 'N': ping = 0; break;
		case 'b':
			if (map_count == MAX_PROBE_MAPS || parse_probe_map(optarg, &maps[map_count], err, sizeof(err)) != 0) {
				fprintf(stderr, "--probe: %s\n", map_count == MAX_PROBE_MAPS ? "too many" : err);
				return 2;
			}
			map_count++;
			break;
		case 'V': puts(MNM_VERSION); return 0;
		case 'h': usage(stdout); return 0;
		default: usage(stderr); return 2;
		}
	}
	if (!scan.targets_text || !scan.output) {
		usage(stderr);
		return 2;
	}
	if (mnm_targets_parse(scan.targets_text, &targets, err, sizeof(err)) != 0) {
		fprintf(stderr, "--targets: %s\n", err);
		return 2;
	}
	if (!allow_public && !mnm_targets_all_private(&targets, err, sizeof(err))) {
		fprintf(stderr, "--targets: %s\n", err);
		mnm_targets_free(&targets);
		return 2;
	}
	if (scan.ports_text[0] && mnm_ports_parse(scan.ports_text, &ports, err, sizeof(err)) != 0) {
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
	write_output(&scan);

	/* Phase 1: find the devices. The neighbour table and the leases are read
	 * first (instant results), then once more after ARP and ping, which fill
	 * the neighbour table with the devices that just answered. */
	struct mnm_discover_options d = { .timeout_ms = tcp.timeout_ms, .ping = ping, .stop = &stop_requested,
					  .on_change = on_discover_change, .context = &scan };
	mnm_discover_neighbors(&targets, &scan.devices);
	mnm_discover_leases(leases, &scan.devices);
	mnm_discover_vendors(vendors, &scan.devices);
	write_output(&scan);

	int interrupted = mnm_discover_arp_ping(&targets, &scan.devices, &d);
	mnm_discover_neighbors(&targets, &scan.devices);
	mnm_discover_leases(leases, &scan.devices);
	mnm_discover_vendors(vendors, &scan.devices);
	write_output(&scan);

	/* Phase 2: the TCP ports of every device found. */
	int status = 0;
	if (!interrupted && ports.count && scan.devices.count) {
		struct mnm_targets found;
		if (devices_as_targets(&scan.devices, &found) == 0) {
			scan.phase = "ports";
			tcp.stop = &stop_requested;
			tcp.on_open = on_open;
			tcp.on_progress = on_progress;
			tcp.context = &scan;
			int outcome = mnm_tcpscan_run(&found, &ports, &tcp);
			if (outcome == 1)
				interrupted = 1;
			else if (outcome < 0)
				status = 1;
			if (outcome == 0)
				for (size_t i = 0; i < scan.devices.count; i++)
					scan.devices.list[i].ports_scanned = 1;
			mnm_targets_free(&found);
		} else {
			status = 1;
		}
	}

	/* Phase 3: the services behind the open ports. */
	if (!interrupted && status == 0 && map_count) {
		size_t capacity = 0, count = 0;
		struct mnm_probe_job *jobs = NULL;
		for (size_t i = 0; i < scan.devices.count; i++) {
			struct mnm_device *dev = &scan.devices.list[i];
			for (size_t k = 0; k < dev->tcp_count; k++) {
				for (size_t m = 0; m < map_count; m++) {
					if (!map_has_port(&maps[m], dev->tcp[k]))
						continue;
					if (count == capacity) {
						capacity = capacity ? capacity * 2 : 64;
						struct mnm_probe_job *j = realloc(jobs, capacity * sizeof(*j));
						if (!j) {
							free(jobs);
							jobs = NULL;
							count = 0;
							status = 1;
							goto no_jobs;
						}
						jobs = j;
					}
					jobs[count++] = (struct mnm_probe_job){ dev->ipv4, dev->tcp[k], maps[m].probe };
				}
			}
		}
		if (count) {
			struct mnm_probe_options po = { .parallel = tcp.parallel, .timeout_ms = tcp.timeout_ms + 1500,
							.stop = &stop_requested, .on_found = on_service,
							.on_progress = on_progress, .context = &scan };
			scan.phase = "services";
			int outcome = mnm_probe_run(jobs, count, &po);
			if (outcome == 1)
				interrupted = 1;
			else if (outcome < 0)
				status = 1;
		}
		if (!interrupted && status == 0)
			for (size_t i = 0; i < scan.devices.count; i++)
				scan.devices.list[i].services_checked = 1;
no_jobs:
		free(jobs);
	}

	scan.phase = "finished";
	scan.state = status ? "failed" : (interrupted || stop_requested) ? "stopped" : "done";
	scan.finished = time(NULL);
	if (write_output(&scan) != 0) {
		fprintf(stderr, "%s: cannot write the result\n", scan.output);
		status = 1;
	}

	mnm_devices_free(&scan.devices);
	mnm_targets_free(&targets);
	mnm_ports_free(&ports);
	for (size_t m = 0; m < map_count; m++)
		mnm_ports_free(&maps[m].ports);
	return status;
}
