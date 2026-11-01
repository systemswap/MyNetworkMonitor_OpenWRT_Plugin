/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tests of the pure functions (target and port lists, JSON output) and one
 * end-to-end run of the TCP scan against listening sockets on the loopback
 * interface. Runs on the build machine; no test framework, so that nothing
 * beyond a C compiler is needed.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../src/json.h"
#include "../src/ports.h"
#include "../src/targets.h"
#include "../src/tcpscan.h"

static int failures, checks;

#define CHECK(cond)                                                                 \
	do {                                                                        \
		checks++;                                                           \
		if (!(cond)) {                                                      \
			failures++;                                                 \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
		}                                                                   \
	} while (0)

static uint32_t ip(const char *text)
{
	uint32_t value = 0;
	mnm_parse_ipv4(text, strlen(text), &value);
	return value;
}

static void test_ipv4(void)
{
	uint32_t v;
	char buf[16];

	CHECK(mnm_parse_ipv4("192.0.2.7", 9, &v) == 0 && v == 0xc0000207u);
	CHECK(mnm_parse_ipv4("0.0.0.0", 7, &v) == 0 && v == 0);
	CHECK(mnm_parse_ipv4("255.255.255.255", 15, &v) == 0 && v == 0xffffffffu);
	CHECK(mnm_parse_ipv4("256.0.0.1", 9, &v) != 0);
	CHECK(mnm_parse_ipv4("1.2.3", 5, &v) != 0);
	CHECK(mnm_parse_ipv4("1.2.3.4.5", 9, &v) != 0);
	CHECK(mnm_parse_ipv4("01.2.3.4", 8, &v) != 0);
	CHECK(mnm_parse_ipv4("1.2.3.4 ", 8, &v) != 0);
	CHECK(mnm_parse_ipv4("1..3.4", 6, &v) != 0);
	mnm_format_ipv4(0xcb007105u, buf);
	CHECK(strcmp(buf, "203.0.113.5") == 0);
}

static void test_targets(void)
{
	struct mnm_targets t;
	char err[160];

	CHECK(mnm_targets_parse("192.0.2.7", &t, err, sizeof(err)) == 0 && t.total == 1);
	mnm_targets_free(&t);

	CHECK(mnm_targets_parse("192.0.2.1-254", &t, err, sizeof(err)) == 0 && t.total == 254);
	CHECK(mnm_targets_at(&t, 0) == ip("192.0.2.1") && mnm_targets_at(&t, 253) == ip("192.0.2.254"));
	mnm_targets_free(&t);

	CHECK(mnm_targets_parse("198.51.100.250-198.51.101.5", &t, err, sizeof(err)) == 0 && t.total == 12);
	mnm_targets_free(&t);

	/* /24 without network and broadcast address, /31 and /32 with both */
	CHECK(mnm_targets_parse("203.0.113.77/24", &t, err, sizeof(err)) == 0 && t.total == 254);
	CHECK(mnm_targets_at(&t, 0) == ip("203.0.113.1"));
	mnm_targets_free(&t);
	CHECK(mnm_targets_parse("203.0.113.4/31", &t, err, sizeof(err)) == 0 && t.total == 2);
	mnm_targets_free(&t);
	CHECK(mnm_targets_parse("203.0.113.4/32", &t, err, sizeof(err)) == 0 && t.total == 1);
	mnm_targets_free(&t);

	CHECK(mnm_targets_parse(" 192.0.2.1 , 192.0.2.3-4 ,,198.51.100.0/30", &t, err, sizeof(err)) == 0 &&
	      t.total == 1 + 2 + 2 && t.count == 3);
	CHECK(mnm_targets_at(&t, 3) == ip("198.51.100.1"));
	mnm_targets_free(&t);

	/* /16 is the largest network; one address more is refused */
	CHECK(mnm_targets_parse("10.0.0.0/16", &t, err, sizeof(err)) == 0 && t.total == 65534);
	mnm_targets_free(&t);
	CHECK(mnm_targets_parse("10.0.0.0/15", &t, err, sizeof(err)) != 0 && strstr(err, "too many"));
	CHECK(mnm_targets_parse("10.0.0.0/16,10.1.0.1-3", &t, err, sizeof(err)) != 0);

	CHECK(mnm_targets_parse("192.0.2.9-3", &t, err, sizeof(err)) != 0 && strstr(err, "before it starts"));
	CHECK(mnm_targets_parse("192.0.2.1-300", &t, err, sizeof(err)) != 0);
	CHECK(mnm_targets_parse("192.0.2.0/33", &t, err, sizeof(err)) != 0);
	CHECK(mnm_targets_parse("192.0.2.0/", &t, err, sizeof(err)) != 0);
	CHECK(mnm_targets_parse("example.org", &t, err, sizeof(err)) != 0);
	CHECK(mnm_targets_parse("", &t, err, sizeof(err)) != 0 && strstr(err, "no addresses"));
	CHECK(t.ranges == NULL && t.total == 0);
}

static void test_private_only(void)
{
	struct mnm_targets t;
	char err[200];
	const char *own[] = { "10.0.0.1", "172.20.0.0/16", "172.31.255.254", "192.168.123.0/24", "169.254.1.1",
			      "127.0.0.1", "192.168.0.1-192.168.255.254" };
	const char *foreign[] = { "8.8.8.8", "192.0.2.0/24", "172.32.0.1", "172.15.255.255", "192.169.0.1",
				  "11.0.0.1", "10.255.255.250-11.0.0.5", "100.64.0.1" };

	for (size_t i = 0; i < sizeof(own) / sizeof(own[0]); i++) {
		CHECK(mnm_targets_parse(own[i], &t, err, sizeof(err)) == 0);
		CHECK(mnm_targets_all_private(&t, err, sizeof(err)) == 1);
		mnm_targets_free(&t);
	}
	for (size_t i = 0; i < sizeof(foreign) / sizeof(foreign[0]); i++) {
		CHECK(mnm_targets_parse(foreign[i], &t, err, sizeof(err)) == 0);
		CHECK(mnm_targets_all_private(&t, err, sizeof(err)) == 0 && strstr(err, "not a private network"));
		mnm_targets_free(&t);
	}
	/* one public entry spoils the whole list */
	CHECK(mnm_targets_parse("192.168.1.0/24,203.0.113.5", &t, err, sizeof(err)) == 0);
	CHECK(mnm_targets_all_private(&t, err, sizeof(err)) == 0 && strstr(err, "203.0.113.5"));
	mnm_targets_free(&t);
}

static void test_ports(void)
{
	struct mnm_ports p;
	char err[160];

	CHECK(mnm_ports_parse("443,22,80,22", &p, err, sizeof(err)) == 0 && p.count == 3);
	CHECK(p.list[0] == 22 && p.list[1] == 80 && p.list[2] == 443);
	mnm_ports_free(&p);

	CHECK(mnm_ports_parse("8000-8010, 8005-8020", &p, err, sizeof(err)) == 0 && p.count == 21);
	mnm_ports_free(&p);

	CHECK(mnm_ports_parse("1-65535", &p, err, sizeof(err)) == 0 && p.count == 65535 && p.list[65534] == 65535);
	mnm_ports_free(&p);

	CHECK(mnm_ports_parse("0", &p, err, sizeof(err)) != 0);
	CHECK(mnm_ports_parse("65536", &p, err, sizeof(err)) != 0);
	CHECK(mnm_ports_parse("80-70", &p, err, sizeof(err)) != 0);
	CHECK(mnm_ports_parse("http", &p, err, sizeof(err)) != 0);
	CHECK(mnm_ports_parse(" , ", &p, err, sizeof(err)) != 0 && strcmp(err, "no ports given") == 0);
	CHECK(p.list == NULL && p.count == 0);
}

static void check_json(const char *input, size_t len, const char *expected)
{
	char *buf = NULL;
	size_t size = 0;
	FILE *f = open_memstream(&buf, &size);

	mnm_json_string(f, input, len);
	fclose(f);
	checks++;
	if (strcmp(buf, expected) != 0) {
		failures++;
		fprintf(stderr, "json: expected %s, got %s\n", expected, buf);
	}
	free(buf);
}

static void test_json(void)
{
	check_json("plain", 5, "\"plain\"");
	check_json("a\"b\\c", 5, "\"a\\\"b\\\\c\"");
	check_json("l1\nl2\r\t", 7, "\"l1\\nl2\\r\\t\"");
	check_json("\x01\x7f", 2, "\"\\u0001\\u007f\"");
	check_json("nul\0x", 5, "\"nul\\u0000x\"");
	check_json("\xc3\xa4", 2, "\"\xc3\xa4\"");                 /* valid UTF-8 stays */
	check_json("\xe2\x82\xac", 3, "\"\xe2\x82\xac\"");
	check_json("\xff\xfe", 2, "\"\\u00ff\\u00fe\"");           /* invalid bytes */
	check_json("\xc3", 1, "\"\\u00c3\"");                      /* cut sequence */
	check_json("\xc0\xaf", 2, "\"\\u00c0\\u00af\"");           /* overlong form */
	check_json("\xed\xa0\x80", 3, "\"\\u00ed\\u00a0\\u0080\""); /* surrogate */
}

struct collected {
	uint16_t ports[8];
	int count;
	uint64_t done;
};

static void collect_open(void *context, uint32_t address, uint16_t port)
{
	struct collected *c = context;
	if (address == 0x7f000001u && c->count < 8)
		c->ports[c->count++] = port;
}

static void collect_progress(void *context, uint64_t done, uint64_t total)
{
	(void)total;
	((struct collected *)context)->done = done;
}

/* Opens a listening socket on 127.0.0.1 with a free port chosen by the system. */
static int listen_local(uint16_t *port)
{
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(0x7f000001u) };
	socklen_t len = sizeof(sa);
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd < 0 || bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(fd, 16) != 0 ||
	    getsockname(fd, (struct sockaddr *)&sa, &len) != 0)
		return -1;
	*port = ntohs(sa.sin_port);
	return fd;
}

static void test_tcpscan_loopback(void)
{
	uint16_t open1, open2, closed;
	int l1 = listen_local(&open1), l2 = listen_local(&open2), l3 = listen_local(&closed);
	char list[64], err[160];
	struct mnm_targets t;
	struct mnm_ports p;
	struct collected c = { 0 };

	CHECK(l1 >= 0 && l2 >= 0 && l3 >= 0);
	close(l3);   /* this port is now closed: the connection is refused */
	snprintf(list, sizeof(list), "%u,%u,%u", open1, closed, open2);
	CHECK(mnm_targets_parse("127.0.0.1", &t, err, sizeof(err)) == 0);
	CHECK(mnm_ports_parse(list, &p, err, sizeof(err)) == 0);

	struct mnm_tcpscan_options o = { .parallel = 2, .timeout_ms = 500, .on_open = collect_open,
					 .on_progress = collect_progress, .context = &c };
	CHECK(mnm_tcpscan_run(&t, &p, &o) == 0);
	CHECK(c.done == 3);
	CHECK(c.count == 2);
	int found1 = 0, found2 = 0;
	for (int i = 0; i < c.count; i++) {
		found1 |= c.ports[i] == open1;
		found2 |= c.ports[i] == open2;
	}
	CHECK(found1 && found2);

	mnm_targets_free(&t);
	mnm_ports_free(&p);
	close(l1);
	close(l2);
}

int main(void)
{
	test_ipv4();
	test_targets();
	test_private_only();
	test_ports();
	test_json();
	test_tcpscan_loopback();
	printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
