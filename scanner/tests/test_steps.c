/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tests of the probes with several steps: the checks and evaluations on made-up
 * answers, and complete runs against small servers on the loopback interface
 * that play a controller - an S7 that confirms only one endpoint, so that the
 * search for it really runs, a Modbus device, a WAGO controller and HSMS
 * equipment that must receive Separate.req after an accepted select.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../src/netio.h"
#include "../src/probes.h"
#include "../src/probes_steps.h"
#include "check.h"

static int identify(const char *id, const void *answer, size_t len)
{
	const struct mnm_probe *p = mnm_probe_find(id);
	return p ? p->identify(answer, len) : -1;
}

#define ID(id, lit) identify(id, lit, sizeof(lit) - 1)

/* An answer to an SZL read: TPKT, COTP, S7 header with 12 bytes of parameters,
 * then the data with return code 0xFF and count records of record_len bytes. */
static size_t szl_answer(uint8_t *out, unsigned int id, size_t record_len, const uint8_t *records, size_t count)
{
	size_t data = 12 + record_len * count, total = 17 + 12 + data;
	memset(out, 0, total);
	out[0] = 0x03, out[2] = (uint8_t)(total >> 8), out[3] = (uint8_t)total;
	out[4] = 0x02, out[5] = 0xf0, out[6] = 0x80;
	out[7] = 0x32, out[8] = 0x07, out[14] = 12;
	out[15] = (uint8_t)(data >> 8), out[16] = (uint8_t)data;
	uint8_t *d = out + 29;
	d[0] = 0xff, d[1] = 0x09, d[2] = (uint8_t)((data - 4) >> 8), d[3] = (uint8_t)(data - 4);
	d[4] = (uint8_t)(id >> 8), d[5] = (uint8_t)id;
	d[8] = (uint8_t)(record_len >> 8), d[9] = (uint8_t)record_len, d[11] = (uint8_t)count;
	memcpy(d + 12, records, record_len * count);
	return total;
}

/* SZL 0x0011: record 1 with the order number, record 7 with firmware V4.5.1. */
static size_t modules_answer(uint8_t *out)
{
	uint8_t rec[2][28];
	memset(rec, ' ', sizeof(rec));
	rec[0][0] = 0, rec[0][1] = 1;
	memcpy(rec[0] + 2, "6ES7 214-1AG40-0XB0", 19);
	rec[0][22] = 0, rec[0][23] = 0, rec[0][24] = 'V', rec[0][25] = 0, rec[0][26] = 0, rec[0][27] = 0;
	rec[1][0] = 0, rec[1][1] = 7;
	rec[1][22] = 0, rec[1][23] = 0, rec[1][24] = 'V', rec[1][25] = 4, rec[1][26] = 5, rec[1][27] = 1;
	return szl_answer(out, 0x0011, 28, &rec[0][0], 2);
}

/* SZL 0x001C: module name and serial number, and a location that is a number. */
static size_t components_answer(uint8_t *out)
{
	uint8_t rec[3][34];
	memset(rec, 0, sizeof(rec));
	rec[0][1] = 2;
	memcpy(rec[0] + 2, "PLC_1", 5);
	rec[1][1] = 5;
	memcpy(rec[1] + 2, "S C-XYZ123", 10);
	rec[2][1] = 9;
	rec[2][2] = 0x00, rec[2][3] = 0x2a;
	return szl_answer(out, 0x001c, 34, &rec[0][0], 3);
}

/* -------------------------------------------------------- test servers */

enum role { S7, MODBUS, WAGO, SECSGEM };

struct server {
	int listen_fd;
	enum role role;
	int separated;   /* SECS/GEM: Separate.req arrived */
	int connections;
	pthread_mutex_t lock;
};

struct conversation {
	struct server *server;
	int fd;
};

static int read_n(int fd, uint8_t *buf, size_t n)
{
	return mnm_recv_exact(fd, buf, n, mnm_now_ms() + 2000);
}

static void play_s7(int fd)
{
	uint8_t req[64], out[256];
	static const uint8_t confirm[] = { 0x03, 0x00, 0x00, 0x0b, 0x06, 0xd0, 0x00, 0x01, 0x00, 0x00, 0x00 };
	static const uint8_t setup_ack[] = { 0x03, 0x00, 0x00, 0x1b, 0x02, 0xf0, 0x80, 0x32, 0x03, 0x00, 0x00, 0xff,
					     0xff, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x01, 0x00,
					     0x01, 0x00, 0xf0 };
	/* only OP, rack 0, slot 1 is the CPU; every other endpoint is closed at once */
	if (read_n(fd, req, 22) != 0 || req[20] != 0x02 || req[21] != 0x01)
		return;
	send(fd, confirm, sizeof(confirm), MSG_NOSIGNAL);
	while (read_n(fd, req, 4) == 0) {
		size_t len = (size_t)req[2] << 8 | req[3];
		if (len < 4 || len > sizeof(req) || read_n(fd, req + 4, len - 4) != 0)
			return;
		if (len == 25) {
			send(fd, setup_ack, sizeof(setup_ack), MSG_NOSIGNAL);
		} else if (len == 33) {
			size_t n = req[30] == 0x11 ? modules_answer(out) : components_answer(out);
			send(fd, out, n, MSG_NOSIGNAL);
		}
	}
}

static void play_modbus(int fd)
{
	uint8_t req[16];
	static const uint8_t holding[] = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x01, 0x03, 0x02, 0x00, 0x00 };
	static const uint8_t ident[] = { 0x00, 0x02, 0x00, 0x00, 0x00, 0x1a, 0x01, 0x2b, 0x0e, 0x01, 0x01, 0x00, 0x00, 0x03,
					 0x00, 0x05, 'V', 'e', 'n', 'd', '1', 0x01, 0x04, 'P', '-', '0', '1',
					 0x02, 0x03, '1', '.', '0' };
	ssize_t n = recv(fd, req, sizeof(req), 0);
	if (n == 12 && req[7] == 0x03)
		send(fd, holding, sizeof(holding), MSG_NOSIGNAL);
	else if (n == 11 && req[7] == 0x2b)
		send(fd, ident, sizeof(ident), MSG_NOSIGNAL);
}

static void play_wago(int fd)
{
	uint8_t req[20];
	static const char answer[] = "\x88\x12\x01\x00" "ORDER=750-8212;DESCR=Controller;SW-VER=03.10.08;SN=X1;";
	if (read_n(fd, req, 20) == 0)
		send(fd, answer, sizeof(answer), MSG_NOSIGNAL);
}

static void play_secsgem(struct server *s, int fd)
{
	uint8_t req[14];
	static const uint8_t accepted[] = { 0x00, 0x00, 0x00, 0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01 };
	if (read_n(fd, req, 14) != 0 || req[9] != 1)
		return;
	send(fd, accepted, sizeof(accepted), MSG_NOSIGNAL);
	if (read_n(fd, req, 14) == 0 && req[9] == 9) {
		pthread_mutex_lock(&s->lock);
		s->separated = 1;
		pthread_mutex_unlock(&s->lock);
	}
}

static void *converse(void *arg)
{
	struct conversation *c = arg;
	switch (c->server->role) {
	case S7: play_s7(c->fd); break;
	case MODBUS: play_modbus(c->fd); break;
	case WAGO: play_wago(c->fd); break;
	case SECSGEM: play_secsgem(c->server, c->fd); break;
	}
	close(c->fd);
	free(c);
	return NULL;
}

/* Accepts connections until none came for a second; one thread each, so that
 * parallel attempts are answered in parallel. */
static void *serve(void *arg)
{
	struct server *s = arg;
	pthread_t threads[64];
	int count = 0;
	for (;;) {
		struct pollfd p = { .fd = s->listen_fd, .events = POLLIN };
		if (poll(&p, 1, 1000) <= 0 || count == 64)
			break;
		struct conversation *c = malloc(sizeof(*c));
		if (!c)
			break;
		c->server = s;
		c->fd = accept(s->listen_fd, NULL, NULL);
		if (c->fd < 0 || pthread_create(&threads[count], NULL, converse, c) != 0) {
			if (c->fd >= 0)
				close(c->fd);
			free(c);
			break;
		}
		count++;
	}
	for (int i = 0; i < count; i++)
		pthread_join(threads[i], NULL);
	s->connections = count;
	return NULL;
}

/* Runs the probe against a server in the given role; returns what run gave. */
static int run_against(enum role role, const char *id, char *detail, size_t len, struct server *s)
{
	uint16_t port;
	pthread_t thread;
	memset(s, 0, sizeof(*s));
	s->role = role;
	pthread_mutex_init(&s->lock, NULL);
	s->listen_fd = listen_local(&port);
	if (s->listen_fd < 0 || pthread_create(&thread, NULL, serve, s) != 0)
		return -1;
	detail[0] = '\0';
	int found = mnm_probe_find(id)->run(0x7f000001u, port, 500, detail, len);
	pthread_join(thread, NULL);
	close(s->listen_fd);
	pthread_mutex_destroy(&s->lock);
	return found;
}

/* --------------------------------------------------------------- tests */

void test_probes_steps(void)
{
	char d[MNM_DETAIL_LEN];
	uint8_t buf[512];
	struct server s;

	/* MS-SQL: TDS response with a PRELOGIN answer, version 15.0.2000, encryption off */
	static const uint8_t tds[] = { 0x04, 0x01, 0x00, 0x25, 0x00, 0x00, 0x01, 0x00,
				       0x00, 0x00, 0x0b, 0x00, 0x06, 0x01, 0x00, 0x11, 0x00, 0x01, 0xff,
				       15, 0, 0x07, 0xd0, 0x00, 0x00, 0x00,
				       0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	CHECK(identify("mssqlserver", tds, sizeof(tds)) == 1);
	CHECK(ID("mssqlserver", "\x04\x01\x02\x00\x00\x00\x01\x00\x00") == 0);   /* packet length 512 */
	CHECK(ID("mssqlserver", "\x12\x01\x00\x25\x00\x00\x01\x00\x00") == 0);
	mnm_probe_find("mssqlserver")->describe(tds, sizeof(tds), d, sizeof(d));
	CHECK(strcmp(d, "Version: 15.0.2000 (SQL Server 2019) · Encryption: available, but not required") == 0);

	/* OPC UA: Acknowledge or Hello, not Error */
	CHECK(ID("opcua", "ACKF\x1c\x00\x00\x00") == 1 && ID("opcua", "HELF") == 1 && ID("opcua", "ERRF....") == 0);

	/* S7: Connect Confirm; a Disconnect Request is not */
	CHECK(ID("s7", "\x03\x00\x00\x0b\x06\xd0\x00\x01") == 1);
	CHECK(ID("s7", "\x03\x00\x00\x0b\x06\x80\x00\x01") == 0);
	size_t m = modules_answer(buf), c;
	uint8_t components[256];
	c = components_answer(components);
	d[0] = '\0';
	mnm_s7_describe_szl(buf, m, components, c, d, sizeof(d));
	CHECK(strcmp(d, "Order number: 6ES7 214-1AG40-0XB0 · Firmware: V4.5.1 · Module name: PLC_1 · "
			"Serial number: S C-XYZ123") == 0);
	/* a failed list (return code not 0xFF) and every cut-off length are read safely */
	buf[29] = 0x0a;
	d[0] = '\0';
	mnm_s7_describe_szl(buf, m, NULL, 0, d, sizeof(d));
	CHECK(d[0] == '\0');
	for (size_t cut = 0; cut <= c; cut++) {
		d[0] = '\0';
		mnm_s7_describe_szl(components, cut, components, cut, d, sizeof(d));
	}

	/* Modbus: function or exception code after an MBAP header with protocol 0 */
	CHECK(ID("modbus", "\x00\x01\x00\x00\x00\x05\x01\x03\x02\x00\x00") == 1);
	CHECK(ID("modbus", "\x00\x01\x00\x00\x00\x03\x01\x83\x02") == 1);
	CHECK(ID("modbus", "\x00\x01\x00\x01\x00\x05\x01\x03\x02") == 0);
	CHECK(ID("modbus", "\x00\x01\x00\x00\x00\x05\x01\x2b\x02") == 0);
	CHECK(mnm_modbus_server_id((const uint8_t *)"\x00\x03\x00\x00\x00\x08\x01\x11\x05" "ABCD\x01", 14, d, sizeof(d)) == 1 &&
	      strcmp(d, "Server ID: ABCD") == 0);
	CHECK(mnm_modbus_server_id((const uint8_t *)"\x00\x03\x00\x00\x00\x03\x01\x91\x01", 9, d, sizeof(d)) == 0);
	CHECK(mnm_swapped_text((const uint8_t *)"AWOG7 05", 8, d, sizeof(d)) == 1 && strcmp(d, "WAGO 750") == 0);

	/* WAGO service protocol */
	CHECK(ID("wago", "\x88\x12\x01\x00ORDER=750-8212;") == 1);
	CHECK(ID("wago", "\x88\x12\x01\x00order=750-8212;") == 0);
	CHECK(ID("wago", "\x88\x13\x01\x00ORDER=750-8212;") == 0);

	/* HSMS: Select.rsp, Reject.req; a data message is not a control answer */
	CHECK(ID("secsgem", "\x00\x00\x00\x0a\xff\xff\x00\x00\x00\x02\x00\x00\x00\x01") == 1);
	CHECK(ID("secsgem", "\x00\x00\x00\x0a\xff\xff\x00\x04\x00\x07\x00\x00\x00\x01") == 1);
	CHECK(ID("secsgem", "\x00\x00\x00\x0a\xff\xff\x00\x00\x00\x00\x00\x00\x00\x01") == 0);
	CHECK(ID("secsgem", "\x00\x00\x00\x09\xff\xff\x00\x00\x00\x02\x00\x00\x00\x01") == 0);
	mnm_probe_find("secsgem")->describe((const uint8_t *)"\x00\x00\x00\x0a\xff\xff\x00\x01\x00\x02\x00\x00\x00\x01", 14,
					    d, sizeof(d));
	CHECK(strcmp(d, "HSMS select refused (status 1).") == 0);

	/* complete runs against the test servers */
	CHECK(run_against(S7, "s7", d, sizeof(d), &s) == 1);
	CHECK(strstr(d, "Connection endpoint: OP, rack 0, slot 1") == d);
	CHECK(strstr(d, "Order number: 6ES7 214-1AG40-0XB0") && strstr(d, "Firmware: V4.5.1") && strstr(d, "PLC_1"));
	CHECK(s.connections <= 1 + 6 + 1);   /* detection, one group of six, identification */

	CHECK(run_against(MODBUS, "modbus", d, sizeof(d), &s) == 1);
	CHECK(strcmp(d, "Vendor: Vend1 · Product code: P-01 · Revision: 1.0") == 0);

	CHECK(run_against(WAGO, "wago", d, sizeof(d), &s) == 1);
	CHECK(strcmp(d, "Order number: 750-8212 · Description: Controller · Firmware: 03.10.08 · Serial: X1") == 0);

	CHECK(run_against(SECSGEM, "secsgem", d, sizeof(d), &s) == 1);
	CHECK(strcmp(d, "HSMS select accepted.") == 0 && s.separated == 1);
}
