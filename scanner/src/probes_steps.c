/* SPDX-License-Identifier: GPL-2.0-only */
#include "probes_steps.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "netio.h"

/* The packets, byte for byte those of the desktop program (taken from its
 * source by a script and compared with it before every change, see
 * docs/PRUEFUNGEN.md). Some answer only to exactly these bytes. */
static const uint8_t pkt_mssql_prelogin[94] = {
	0x12, 0x01, 0x00, 0x5e, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x24, 0x00,
	0x06, 0x01, 0x00, 0x2a, 0x00, 0x01, 0x02, 0x00, 0x2b, 0x00, 0x01, 0x03,
	0x00, 0x2c, 0x00, 0x04, 0x04, 0x00, 0x30, 0x00, 0x01, 0x05, 0x00, 0x31,
	0x00, 0x24, 0x06, 0x00, 0x55, 0x00, 0x01, 0xff, 0x03, 0x0f, 0x5a, 0xfc,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
};
static const uint8_t pkt_opcua_hello[57] = {
	0x48, 0x45, 0x4c, 0x46, 0x39, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x6f, 0x70, 0x63, 0x2e,
	0x74, 0x63, 0x70, 0x3a, 0x2f, 0x2f, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x68,
	0x6f, 0x73, 0x74, 0x3a, 0x34, 0x38, 0x34, 0x30, 0x2f,
};
static const uint8_t pkt_s7_cotp_cr[22] = {
	0x03, 0x00, 0x00, 0x16, 0x11, 0xe0, 0x00, 0x00, 0x00, 0x01, 0x00, 0xc0,
	0x01, 0x0a, 0xc1, 0x02, 0x01, 0x00, 0xc2, 0x02, 0x01, 0x02,
};
static const uint8_t pkt_s7_setup_comm[25] = {
	0x03, 0x00, 0x00, 0x19, 0x02, 0xf0, 0x80, 0x32, 0x01, 0x00, 0x00, 0xff,
	0xff, 0x00, 0x08, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01,
	0xe0,
};
static const uint8_t pkt_s7_szl_0011[33] = {
	0x03, 0x00, 0x00, 0x21, 0x02, 0xf0, 0x80, 0x32, 0x07, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x08, 0x00, 0x08, 0x00, 0x01, 0x12, 0x04, 0x11, 0x44, 0x01,
	0x00, 0xff, 0x09, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00,
};
static const uint8_t pkt_s7_szl_001c[33] = {
	0x03, 0x00, 0x00, 0x21, 0x02, 0xf0, 0x80, 0x32, 0x07, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x08, 0x00, 0x08, 0x00, 0x01, 0x12, 0x04, 0x11, 0x44, 0x01,
	0x00, 0xff, 0x09, 0x00, 0x04, 0x00, 0x1c, 0x00, 0x00,
};
static const uint8_t pkt_modbus_read_holding_registers[12] = {
	0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01,
};
static const uint8_t pkt_modbus_read_device_id[11] = {
	0x00, 0x02, 0x00, 0x00, 0x00, 0x05, 0x01, 0x2b, 0x0e, 0x01, 0x00,
};
static const uint8_t pkt_modbus_report_server_id[8] = {
	0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x01, 0x11,
};
static const uint8_t pkt_modbus_read_holding_template[12] = {
	0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t pkt_wago_read_device_id[20] = {
	0x88, 0x12, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x08, 0x01,
};
static const uint8_t pkt_secsgem_select_req[14] = {
	0x00, 0x00, 0x00, 0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
	0x00, 0x01,
};
static const uint8_t pkt_secsgem_select_req_session0[14] = {
	0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
	0x00, 0x01,
};
static const uint8_t pkt_secsgem_select_req_session1[14] = {
	0x00, 0x00, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
	0x00, 0x01,
};
static const uint8_t pkt_secsgem_linktest_req_knock[14] = {
	0x00, 0x00, 0x00, 0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00,
	0x00, 0x01,
};
static const uint8_t pkt_secsgem_separate_req[14] = {
	0x00, 0x00, 0x00, 0x0a, 0xff, 0xff, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00,
	0x00, 0x63,
};

/* ------------------------------------------------------------- helpers */

static unsigned int u16be(const uint8_t *p)
{
	return (unsigned int)p[0] << 8 | p[1];
}

static uint32_t u32be(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

void mnm_detail_add(char *out, size_t outlen, const char *part)
{
	size_t used = strlen(out);
	if (!part[0] || used + 1 >= outlen)
		return;
	snprintf(out + used, outlen - used, "%s%s", used ? " · " : "", part);
}

/* Printable ASCII from the start of a field, at most max bytes, without the
 * spaces around it; "" if fewer than two characters remain (a single
 * character is chance, not information). */
static void field_text(const uint8_t *p, size_t len, size_t max, char *out, size_t outlen)
{
	size_t end = 0, start = 0, n;
	if (len > max)
		len = max;
	while (end < len && p[end] >= 0x20 && p[end] <= 0x7e)
		end++;
	while (start < end && p[start] == ' ')
		start++;
	while (end > start && p[end - 1] == ' ')
		end--;
	n = end - start;
	if (n < 2 || outlen == 0) {
		if (outlen)
			out[0] = '\0';
		return;
	}
	if (n >= outlen)
		n = outlen - 1;
	memcpy(out, p + start, n);
	out[n] = '\0';
}

/* The common procedure of the desktop program: connect, send the greeting,
 * read once (up to cap bytes). Returns the connected socket, with the length of
 * the answer in *len (0 if nothing came in time or the peer closed), or -1
 * without a connection. */
static int greet(uint32_t address, uint16_t port, const uint8_t *hello, size_t hello_len, int timeout_ms,
		 uint8_t *buf, size_t cap, size_t *len)
{
	int fd;
	*len = 0;
	if (mnm_tcp_connect(address, port, timeout_ms, &fd) != MNM_CONNECTED)
		return -1;
	if (mnm_send_all(fd, hello, hello_len, timeout_ms) == 0) {
		ssize_t n = mnm_recv_once(fd, buf, cap, timeout_ms);
		if (n > 0)
			*len = (size_t)n;
	}
	return fd;
}

/* A request on a new connection, answered by one read, as the follow-ups of
 * the desktop program do. Returns the length (0: closed without an answer or
 * no connection), or -1 if the answer did not come in time or the connection
 * failed while reading - there the desktop program gives up all further steps. */
static ssize_t ask_new(uint32_t address, uint16_t port, const uint8_t *req, size_t req_len, int timeout_ms,
		       uint8_t *buf, size_t cap)
{
	int fd;
	if (mnm_tcp_connect(address, port, timeout_ms, &fd) != MNM_CONNECTED)
		return 0;
	ssize_t n = -1;
	if (mnm_send_all(fd, req, req_len, timeout_ms) == 0) {
		n = mnm_recv_once(fd, buf, cap, timeout_ms);
		if (n < 0)
			n = -1;
	}
	close(fd);
	return n;
}

/* ------------------------------------------------------------- MS-SQL */

/* A TDS answer (MS-TDS 2.2.3.1): type 0x04 (response), status 0x01 (end of
 * message) and a plausible packet length. */
static int identify_mssql(const uint8_t *r, size_t len)
{
	if (len <= 8 || r[0] != 0x04 || r[1] != 0x01)
		return 0;
	unsigned int packet = u16be(r + 2);
	return packet > 8 && packet < 512;
}

static const char *mssql_product(int major, int minor)
{
	switch (major) {
	case 8: return "SQL Server 2000";
	case 9: return "SQL Server 2005";
	case 10: return minor >= 50 ? "SQL Server 2008 R2" : "SQL Server 2008";
	case 11: return "SQL Server 2012";
	case 12: return "SQL Server 2014";
	case 13: return "SQL Server 2016";
	case 14: return "SQL Server 2017";
	case 15: return "SQL Server 2019";
	case 16: return "SQL Server 2022";
	case 17: return "SQL Server 2025";
	default: return "";
	}
}

/* The PRELOGIN answer (MS-TDS 2.2.6.5): after the 8-byte header a list of
 * 5-byte entries (token, offset, length; offsets count from the list), ended
 * by 0xFF. VERSION (0x00) and ENCRYPTION (0x01) are reported. */
static int describe_mssql(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	char part[80];
	out[0] = '\0';
	for (size_t at = 8; at + 5 <= len; at += 5) {
		if (r[at] == 0xff)
			break;
		size_t offset = 8 + u16be(r + at + 1), length = u16be(r + at + 3);
		if (offset + length > len)
			continue;
		if (r[at] == 0x00 && length >= 4) {
			int major = r[offset], minor = r[offset + 1];
			unsigned int build = u16be(r + offset + 2);
			const char *product = mssql_product(major, minor);
			if (product[0])
				snprintf(part, sizeof(part), "Version: %d.%d.%u (%s)", major, minor, build, product);
			else
				snprintf(part, sizeof(part), "Version: %d.%d.%u", major, minor, build);
			mnm_detail_add(out, outlen, part);
		} else if (r[at] == 0x01 && length >= 1) {
			static const char *const states[] = { "available, but not required", "in use for login",
							      "not supported by server", "required by server" };
			if (r[offset] < 4) {
				snprintf(part, sizeof(part), "Encryption: %s", states[r[offset]]);
				mnm_detail_add(out, outlen, part);
			}
		}
	}
	return out[0] != '\0';
}

/* ------------------------------------------------------------- OPC UA */

/* OPC UA Part 6, 7.1.2: the server answers the Hello with Acknowledge (ACKF);
 * a Hello in return is accepted as well. */
static int identify_opcua(const uint8_t *r, size_t len)
{
	return len >= 4 && (memcmp(r, "HELF", 4) == 0 || memcmp(r, "ACKF", 4) == 0);
}

/* ----------------------------------------------------------------- S7 */

/* TPKT (RFC 1006) with COTP Connect Confirm (ISO 8073, code 0xD0). */
static int identify_s7(const uint8_t *r, size_t len)
{
	return len >= 6 && r[0] == 0x03 && r[1] == 0x00 && r[5] == 0xd0;
}

/* Reads one TPKT message (length in bytes 2-3, 4 to 4096). Returns its length,
 * -1 if the peer closed or it is not TPKT, -2 on timeout. */
static ssize_t read_tpkt(int fd, uint8_t *buf, size_t cap, int64_t deadline)
{
	int r = mnm_recv_exact(fd, buf, 4, deadline);
	if (r < 0)
		return r;
	size_t length = u16be(buf + 2);
	if (buf[0] != 0x03 || length < 4 || length > 4096 || length > cap)
		return -1;
	r = mnm_recv_exact(fd, buf + 4, length - 4, deadline);
	return r < 0 ? r : (ssize_t)length;
}

/* The data part of an SZL answer, from its return code: TPKT (4), COTP (3),
 * S7 header (10), then the parameters in the length the header gives. */
static const uint8_t *szl_data(const uint8_t *r, size_t len, size_t *dlen)
{
	if (!r || len < 19 || r[7] != 0x32)
		return NULL;
	size_t start = 17 + u16be(r + 13);
	if (start + 12 > len || r[start] != 0xff)   /* 0xFF: success */
		return NULL;
	*dlen = len - start;
	return r + start;
}

void mnm_s7_describe_szl(const uint8_t *modules, size_t modules_len, const uint8_t *components,
			 size_t components_len, char *out, size_t outlen)
{
	for (int list = 0; list < 2; list++) {
		size_t dlen;
		const uint8_t *d = list == 0 ? szl_data(modules, modules_len, &dlen)
					     : szl_data(components, components_len, &dlen);
		if (!d)
			continue;
		size_t record = u16be(d + 8), count = u16be(d + 10), p = 12;
		if (record < 3)
			continue;
		for (size_t i = 0; i < count && p + record <= dlen; i++, p += record) {
			unsigned int index = u16be(d + p);
			const uint8_t *payload = d + p + 2;
			size_t plen = record - 2;
			char text[40], part[80];
			const char *label = NULL;
			if (list == 0) {
				/* 0x0011: record 1 the order number in its first 20 characters,
				 * record 7 the firmware as 'V' and three numbers at offset 22 */
				if (index == 1) {
					field_text(payload, plen, 20, text, sizeof(text));
					if (text[0]) {
						snprintf(part, sizeof(part), "Order number: %s", text);
						mnm_detail_add(out, outlen, part);
					}
				} else if (index == 7 && plen >= 26 && payload[22] == 'V') {
					snprintf(part, sizeof(part), "Firmware: V%u.%u.%u", payload[23], payload[24], payload[25]);
					mnm_detail_add(out, outlen, part);
				}
				continue;
			}
			/* 0x001C: the names given in the configuration and the serial number */
			switch (index) {
			case 1: label = "System name"; break;
			case 2: label = "Module name"; break;
			case 3: label = "Plant designation"; break;
			case 4: label = "Copyright"; break;
			case 5: label = "Serial number"; break;
			case 7: label = "Module type"; break;
			case 9: label = "Location"; break;
			}
			if (!label)
				continue;
			field_text(payload, plen, 32, text, sizeof(text));
			if (text[0]) {
				snprintf(part, sizeof(part), "%s: %s", label, text);
				mnm_detail_add(out, outlen, part);
			}
		}
	}
}

/* How the controller reacted to a connection request. */
enum attempt { SILENT, REJECTED, CONFIRMED };

#define TSAP_PARALLEL 6

struct tsap_try {
	int fd, reading;
	int64_t deadline;
	uint8_t *buf;
	size_t got;
	enum attempt result;
	int done;
};

/* A connection request for an endpoint: the detection packet with connection
 * type and rack/slot in its last two bytes (rack in the upper three bits, slot
 * in the lower five). */
static void tsap_request(uint8_t req[22], uint8_t type, int rack, int slot)
{
	memcpy(req, pkt_s7_cotp_cr, 22);
	req[20] = type;
	req[21] = (uint8_t)((rack << 5) | (slot & 0x1f));
}

/* Tries up to six endpoints at the same time, as the desktop program does:
 * enough to overlap the waiting, few enough for the connection limit of a
 * small controller. Each attempt has timeout_ms to connect and then again to
 * answer, but ends at the budget at the latest. */
static void tsap_batch(uint32_t address, uint16_t port, uint8_t (*reqs)[22], size_t n, int timeout_ms,
		       int64_t budget, enum attempt *out)
{
	struct tsap_try t[TSAP_PARALLEL];
	struct pollfd pf[TSAP_PARALLEL];
	uint8_t *buffers = malloc(TSAP_PARALLEL * 4096u);
	struct sockaddr_in sa;

	for (size_t i = 0; i < n; i++)
		out[i] = SILENT;
	if (!buffers)
		return;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(address);
	for (size_t i = 0; i < n; i++) {
		t[i] = (struct tsap_try){ .fd = -1, .buf = buffers + i * 4096u, .result = SILENT };
		int64_t end = mnm_now_ms() + timeout_ms;
		t[i].deadline = end < budget ? end : budget;
		t[i].fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
		if (t[i].fd < 0 || (connect(t[i].fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 && errno != EINPROGRESS))
			t[i].done = 1;   /* refused at once or no socket: silent */
	}
	for (;;) {
		size_t active = 0, index[TSAP_PARALLEL];
		int64_t now = mnm_now_ms(), wait = 200;
		for (size_t i = 0; i < n; i++) {
			if (t[i].done)
				continue;
			if (now >= t[i].deadline) {
				t[i].done = 1;   /* own timeout: silent */
				continue;
			}
			pf[active] = (struct pollfd){ .fd = t[i].fd, .events = t[i].reading ? POLLIN : POLLOUT };
			index[active++] = i;
			if (t[i].deadline - now < wait)
				wait = t[i].deadline - now;
		}
		if (active == 0)
			break;
		if (poll(pf, (nfds_t)active, (int)wait) < 0 && errno != EINTR)
			break;
		for (size_t k = 0; k < active; k++) {
			struct tsap_try *a = &t[index[k]];
			if (!pf[k].revents)
				continue;
			if (!a->reading) {
				int error = 0;
				socklen_t elen = sizeof(error);
				if (getsockopt(a->fd, SOL_SOCKET, SO_ERROR, &error, &elen) != 0 || error != 0) {
					a->done = 1;   /* refused or unreachable: silent */
					continue;
				}
				/* Connected: the time for the answer starts now. */
				int64_t end = mnm_now_ms() + timeout_ms;
				a->deadline = end < budget ? end : budget;
				a->reading = 1;
				if (send(a->fd, reqs[index[k]], 22, MSG_NOSIGNAL) != 22) {
					a->result = REJECTED;   /* connection reset */
					a->done = 1;
				}
				continue;
			}
			size_t need = a->got < 4 ? 4 : u16be(a->buf + 2);
			ssize_t got = recv(a->fd, a->buf + a->got, need - a->got, MSG_DONTWAIT);
			if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
				continue;
			if (got <= 0) {
				/* closed without an answer, or reset: something is there */
				a->result = REJECTED;
				a->done = 1;
				continue;
			}
			a->got += (size_t)got;
			if (a->got == 4) {
				size_t length = u16be(a->buf + 2);
				if (a->buf[0] != 0x03 || length < 4 || length > 4096) {
					a->result = REJECTED;
					a->done = 1;
					continue;
				}
			}
			if (a->got >= 4 && a->got == u16be(a->buf + 2)) {
				a->result = (a->got >= 6 && a->buf[5] == 0xd0) ? CONFIRMED : REJECTED;
				a->done = 1;
			}
		}
	}
	for (size_t i = 0; i < n; i++) {
		if (t[i].fd >= 0)
			close(t[i].fd);
		out[i] = t[i].result;
	}
	free(buffers);
}

static const char *s7_type_name(uint8_t type)
{
	return type == 0x01 ? "PG" : type == 0x02 ? "OP" : "Basic";
}

/* Asks the candidates in groups of six; the first confirmed one in list order
 * wins. Returns its index or -1; *responded tells whether anything rejected. */
static int tsap_try_all(uint32_t address, uint16_t port, uint8_t (*reqs)[22], size_t n, int timeout_ms,
			int64_t budget, int *responded)
{
	enum attempt results[TSAP_PARALLEL];
	for (size_t start = 0; start < n && mnm_now_ms() < budget; start += TSAP_PARALLEL) {
		size_t count = n - start < TSAP_PARALLEL ? n - start : TSAP_PARALLEL;
		tsap_batch(address, port, reqs + start, count, timeout_ms, budget, results);
		for (size_t i = 0; i < count; i++) {
			if (results[i] == CONFIRMED)
				return (int)(start + i);
			if (results[i] == REJECTED)
				*responded = 1;
		}
	}
	return -1;
}

/*
 * Searches the endpoint the controller confirms, as the desktop program does.
 * Round 1: the common endpoints - slot 1 (S7-1200/1500), 0, 2, 3; rack 0 and 1;
 * connection types PG, OP, Basic - without the detection packet (PG, rack 0,
 * slot 2), which has already failed. If nothing answered at all, the search
 * ends: where nothing rejects, the full space finds nothing either. Otherwise
 * round 2: all 8 racks, 32 slots and three types not asked yet, within 5 s.
 */
static int find_tsap(uint32_t address, uint16_t port, int timeout_ms, uint8_t accepted[22], char *endpoint,
		     size_t endpoint_len)
{
	static const int slots[] = { 1, 0, 2, 3 };
	static const uint8_t types[] = { 0x01, 0x02, 0x03 };
	uint8_t (*reqs)[22] = malloc(768 * sizeof(*reqs));
	int what[768][3];
	size_t n = 0;
	int responded = 0, found;
	int t = timeout_ms < 1500 ? timeout_ms : 1500;

	if (!reqs)
		return 0;
	for (size_t s = 0; s < 4; s++)
		for (int rack = 0; rack <= 1; rack++)
			for (size_t k = 0; k < 3; k++) {
				if (types[k] == 0x01 && rack == 0 && slots[s] == 2)
					continue;
				tsap_request(reqs[n], types[k], rack, slots[s]);
				what[n][0] = types[k], what[n][1] = rack, what[n][2] = slots[s];
				n++;
			}
	found = tsap_try_all(address, port, reqs, n, t, INT64_MAX, &responded);
	if (found < 0 && responded) {
		size_t first = n;
		for (int rack = 0; rack <= 7; rack++)
			for (int slot = 0; slot <= 31; slot++)
				for (size_t k = 0; k < 3; k++) {
					if ((types[k] == 0x01 && rack == 0 && slot == 2) || (rack <= 1 && slot <= 3))
						continue;
					tsap_request(reqs[n], types[k], rack, slot);
					what[n][0] = types[k], what[n][1] = rack, what[n][2] = slot;
					n++;
				}
		found = tsap_try_all(address, port, reqs + first, n - first, t, mnm_now_ms() + 5000, &responded);
		if (found >= 0)
			found += (int)first;
	}
	if (found >= 0) {
		memcpy(accepted, reqs[found], 22);
		snprintf(endpoint, endpoint_len, "%s, rack %d, slot %d", s7_type_name((uint8_t)what[found][0]),
			 what[found][1], what[found][2]);
	}
	free(reqs);
	return found >= 0;
}

/* Connection request (the confirmed one), Setup Communication, then the system
 * status lists 0x0011 and 0x001C, within three times the timeout. Information
 * only; a timeout anywhere yields none, as in the desktop program. */
static void s7_identification(uint32_t address, uint16_t port, int timeout_ms, const uint8_t accepted[22],
			      char *out, size_t outlen)
{
	uint8_t *buf = malloc(3 * 4096);
	int fd;
	if (!buf)
		return;
	if (mnm_tcp_connect(address, port, timeout_ms, &fd) != MNM_CONNECTED) {
		free(buf);
		return;
	}
	int64_t deadline = mnm_now_ms() + 3 * (int64_t)timeout_ms;
	uint8_t *confirm = buf, *modules = buf + 4096, *components = buf + 8192;
	ssize_t n, m = -1, c = -1;
	if (mnm_send_all(fd, accepted, 22, timeout_ms) != 0 || (n = read_tpkt(fd, confirm, 4096, deadline)) < 6 ||
	    confirm[5] != 0xd0)
		goto done;
	if (mnm_send_all(fd, pkt_s7_setup_comm, sizeof(pkt_s7_setup_comm), timeout_ms) != 0 ||
	    (n = read_tpkt(fd, confirm, 4096, deadline)) == -2 || n < 0)
		goto done;
	if (mnm_send_all(fd, pkt_s7_szl_0011, sizeof(pkt_s7_szl_0011), timeout_ms) != 0 ||
	    (m = read_tpkt(fd, modules, 4096, deadline)) == -2)
		goto done;
	if (mnm_send_all(fd, pkt_s7_szl_001c, sizeof(pkt_s7_szl_001c), timeout_ms) != 0 ||
	    (c = read_tpkt(fd, components, 4096, deadline)) == -2)
		goto done;
	mnm_s7_describe_szl(m > 0 ? modules : NULL, m > 0 ? (size_t)m : 0, c > 0 ? components : NULL,
			    c > 0 ? (size_t)c : 0, out, outlen);
done:
	close(fd);
	free(buf);
}

static int run_s7(uint32_t address, uint16_t port, int timeout_ms, char *detail, size_t detail_len)
{
	uint8_t answer[4096], accepted[22];
	char endpoint[48] = "", info[MNM_DETAIL_LEN] = "", part[80];
	size_t len;
	int fd = greet(address, port, pkt_s7_cotp_cr, sizeof(pkt_s7_cotp_cr), timeout_ms, answer, sizeof(answer), &len);
	if (fd < 0)
		return 0;
	close(fd);
	if (identify_s7(answer, len))
		memcpy(accepted, pkt_s7_cotp_cr, 22);
	else if (!find_tsap(address, port, timeout_ms, accepted, endpoint, sizeof(endpoint)))
		return 0;
	detail[0] = '\0';
	if (endpoint[0]) {
		snprintf(part, sizeof(part), "Connection endpoint: %s", endpoint);
		mnm_detail_add(detail, detail_len, part);
	}
	s7_identification(address, port, timeout_ms, accepted, info, sizeof(info));
	mnm_detail_add(detail, detail_len, info[0] ? info : "Connection confirmed, but no S7comm identification available.");
	return 1;
}

/* ------------------------------------------------------------- Modbus */

/* Modbus TCP: protocol id 0 in the MBAP header, then a function code of
 * 0x01-0x10 or its exception (0x81-0x90) - also "no" is a Modbus answer. */
static int identify_modbus(const uint8_t *r, size_t len)
{
	if (len < 8 || r[2] != 0x00 || r[3] != 0x00)
		return 0;
	return (r[7] >= 0x01 && r[7] <= 0x10) || (r[7] >= 0x81 && r[7] <= 0x90);
}

int mnm_modbus_device_id(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	static const char *const names[] = { "Vendor", "Product code", "Revision", "Vendor URL", "Product name",
					     "Model name", "Application name" };
	out[0] = '\0';
	/* 0xAB instead of 0x2B: the device does not know the function */
	if (len < 15 || r[7] != 0x2b)
		return 0;
	size_t count = r[13], p = 14;
	for (size_t i = 0; i < count && p + 1 < len; i++) {
		unsigned int id = r[p];
		size_t length = r[p + 1];
		p += 2;
		if (p + length > len)
			break;
		char value[64], part[96], label[24];
		mnm_printable(r + p, length, value, sizeof(value));
		p += length;
		char *v = value;
		while (*v == ' ')
			v++;
		size_t vl = strlen(v);
		while (vl > 0 && v[vl - 1] == ' ')
			v[--vl] = '\0';
		if (!vl)
			continue;
		if (id < 7)
			snprintf(label, sizeof(label), "%s", names[id]);
		else
			snprintf(label, sizeof(label), "Object 0x%02X", id);
		snprintf(part, sizeof(part), "%s: %s", label, v);
		mnm_detail_add(out, outlen, part);
	}
	return out[0] != '\0';
}

int mnm_modbus_server_id(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	out[0] = '\0';
	if (len < 9 || r[7] != 0x11)   /* 0x91 would be the rejection */
		return 0;
	size_t count = r[8];
	if (count == 0 || 9 + count > len)
		return 0;
	char text[64];
	field_text(r + 9, count, sizeof(text) - 1, text, sizeof(text));
	if (!text[0])
		return 0;
	snprintf(out, outlen, "Server ID: %s", text);
	return 1;
}

int mnm_swapped_text(const uint8_t *data, size_t len, char *out, size_t outlen)
{
	uint8_t swapped[64];
	size_t n = 0;
	for (size_t i = 0; i + 1 < len && n + 2 <= sizeof(swapped); i += 2) {
		swapped[n++] = data[i + 1];
		swapped[n++] = data[i];
	}
	field_text(swapped, n, n, out, outlen);
	return out[0] != '\0';
}

/* Reads holding registers on an open connection (the read template with
 * register and count). Returns the number of data bytes, 0 if the answer is
 * none, -1 if it did not come in time. */
static ssize_t read_holding(int fd, unsigned int reg, unsigned int count, int64_t deadline, uint8_t *data, size_t cap)
{
	uint8_t req[12], buf[512];
	memcpy(req, pkt_modbus_read_holding_template, sizeof(req));
	req[8] = (uint8_t)(reg >> 8), req[9] = (uint8_t)reg;
	req[10] = (uint8_t)(count >> 8), req[11] = (uint8_t)count;
	int64_t left = deadline - mnm_now_ms();
	if (left <= 0 || mnm_send_all(fd, req, sizeof(req), (int)left) != 0)
		return -1;
	left = deadline - mnm_now_ms();
	ssize_t n = left > 0 ? mnm_recv_once(fd, buf, sizeof(buf), (int)left) : -2;
	if (n < 0)
		return -1;
	if (n < 9 || buf[7] != 0x03)
		return 0;
	size_t length = buf[8];
	if (length == 0 || 9 + length > (size_t)n || length > cap)
		return 0;
	memcpy(data, buf + 9, length);
	return (ssize_t)length;
}

/* WAGO controllers answer both standard identifications with "illegal function"
 * but keep the same information in holding registers: 0x2020 (16) the
 * description, 0x2011/0x2012 the order number, 0x2013/0x2014 the firmware,
 * 0x2022 (8) its date. Returns -1 if a step timed out. */
static int modbus_wago(uint32_t address, uint16_t port, int timeout_ms, char *out, size_t outlen)
{
	uint8_t d[64];
	char text[48], part[96];
	int fd;
	ssize_t n;
	unsigned int w[4];
	if (mnm_tcp_connect(address, port, timeout_ms, &fd) != MNM_CONNECTED)
		return 0;
	int64_t deadline = mnm_now_ms() + timeout_ms;
	int result = -1;
	if ((n = read_holding(fd, 0x2020, 16, deadline, d, sizeof(d))) < 0)
		goto done;
	result = 0;
	if (!mnm_swapped_text(d, (size_t)n, text, sizeof(text)) || strncasecmp(text, "WAGO", 4) != 0)
		goto done;
	snprintf(part, sizeof(part), "Device: %s", text);
	mnm_detail_add(out, outlen, part);
	for (int i = 0; i < 4; i++) {
		if ((n = read_holding(fd, 0x2011u + (unsigned int)i, 1, deadline, d, sizeof(d))) < 0) {
			result = -1;
			goto done;
		}
		w[i] = n >= 2 ? (unsigned int)d[0] << 8 | d[1] : 0x10000u;
		if (i == 1 && w[0] < 0x10000u && w[1] < 0x10000u) {
			snprintf(part, sizeof(part), "Order number: %u-%u", w[0], w[1]);
			mnm_detail_add(out, outlen, part);
		}
		if (i == 3 && w[2] < 0x10000u && w[3] < 0x10000u) {
			snprintf(part, sizeof(part), "Firmware: V%u.%u", w[2], w[3]);
			mnm_detail_add(out, outlen, part);
		}
	}
	if ((n = read_holding(fd, 0x2022, 8, deadline, d, sizeof(d))) < 0) {
		result = -1;
		goto done;
	}
	if (mnm_swapped_text(d, (size_t)n, text, sizeof(text))) {
		snprintf(part, sizeof(part), "Firmware date: %s", text);
		mnm_detail_add(out, outlen, part);
	}
	result = 1;
done:
	close(fd);
	return result;
}

/* Detection with one request; then, for the details only, the standard device
 * identification, the server id and the WAGO registers, each on a new
 * connection. A follow-up that runs out of time ends all further ones. */
static int run_modbus(uint32_t address, uint16_t port, int timeout_ms, char *detail, size_t detail_len)
{
	uint8_t answer[4096];
	size_t len;
	int fd = greet(address, port, pkt_modbus_read_holding_registers, sizeof(pkt_modbus_read_holding_registers),
		       timeout_ms, answer, sizeof(answer), &len);
	if (fd < 0)
		return 0;
	close(fd);
	if (!identify_modbus(answer, len))
		return 0;
	detail[0] = '\0';
	ssize_t n = ask_new(address, port, pkt_modbus_read_device_id, sizeof(pkt_modbus_read_device_id), timeout_ms,
			    answer, 512);
	if (n > 0 && mnm_modbus_device_id(answer, (size_t)n, detail, detail_len))
		return 1;
	if (n >= 0) {
		n = ask_new(address, port, pkt_modbus_report_server_id, sizeof(pkt_modbus_report_server_id), timeout_ms,
			    answer, 512);
		if (n > 0 && mnm_modbus_server_id(answer, (size_t)n, detail, detail_len))
			return 1;
		if (n >= 0 && modbus_wago(address, port, timeout_ms, detail, detail_len) == 1 && detail[0])
			return 1;
	}
	detail[0] = '\0';
	mnm_detail_add(detail, detail_len, "Unit ID 1 answered, device identification not supported.");
	return 1;
}

/* --------------------------------------------------------------- WAGO */

static const uint8_t *find_bytes(const uint8_t *hay, size_t len, const char *needle)
{
	size_t n = strlen(needle);
	for (size_t i = 0; n && i + n <= len; i++)
		if (memcmp(hay + i, needle, n) == 0)
			return hay + i;
	return NULL;
}

/* The WAGO service protocol (port 6626): the answer to "read device
 * identification" starts with 0x88 0x12 and carries "ORDER=..." in plain text. */
static int identify_wago(const uint8_t *r, size_t len)
{
	return len >= 4 && r[0] == 0x88 && r[1] == 0x12 && find_bytes(r, len, "ORDER=") != NULL;
}

/* The KEY=value; list from "ORDER=" on: order number, description, firmware,
 * hardware, firmware loader, serial. */
static int describe_wago(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	static const char *const keys[][2] = { { "ORDER", "Order number" }, { "DESCR", "Description" },
					       { "SW-VER", "Firmware" }, { "HW-VER", "Hardware" },
					       { "FWL-VER", "Firmware loader" }, { "SN", "Serial" } };
	const uint8_t *start = find_bytes(r, len, "ORDER=");
	char text[512];
	out[0] = '\0';
	if (!start)
		return 0;
	size_t n = 0;
	while (start + n < r + len && start[n] >= 0x20 && start[n] <= 0x7e && n + 1 < sizeof(text)) {
		text[n] = (char)start[n];
		n++;
	}
	text[n] = '\0';
	for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
		/* the first non-empty value of each key, keys without regard to case */
		for (char *part = text; part && *part;) {
			char *end = strchr(part, ';');
			size_t plen = end ? (size_t)(end - part) : strlen(part);
			char item[160];
			snprintf(item, sizeof(item), "%.*s", (int)(plen < sizeof(item) - 1 ? plen : sizeof(item) - 1), part);
			part = end ? end + 1 : NULL;
			char *eq = strchr(item, '='), *key = item;
			if (!eq || eq == item)
				continue;
			*eq = '\0';
			char *value = eq + 1;
			while (*key == ' ')
				key++;
			for (size_t l = strlen(key); l > 0 && key[l - 1] == ' ';)
				key[--l] = '\0';
			while (*value == ' ')
				value++;
			for (size_t l = strlen(value); l > 0 && value[l - 1] == ' ';)
				value[--l] = '\0';
			if (strcasecmp(key, keys[k][0]) != 0 || !value[0])
				continue;
			char line[192];
			snprintf(line, sizeof(line), "%s: %s", keys[k][1], value);
			mnm_detail_add(out, outlen, line);
			break;
		}
	}
	return out[0] != '\0';
}

/* Sends the request and reads until the identification is complete (the last
 * ';' after "ORDER="), until nothing more is waiting, or until the time is up -
 * the answer may come in several reads. */
static int run_wago(uint32_t address, uint16_t port, int timeout_ms, char *detail, size_t detail_len)
{
	uint8_t *answer = malloc(8192);
	size_t len = 0;
	int fd, found = 0;
	if (!answer)
		return 0;
	if (mnm_tcp_connect(address, port, timeout_ms, &fd) != MNM_CONNECTED) {
		free(answer);
		return 0;
	}
	int64_t deadline = mnm_now_ms() + timeout_ms;
	if (mnm_send_all(fd, pkt_wago_read_device_id, sizeof(pkt_wago_read_device_id), timeout_ms) == 0) {
		while (len < 8192) {
			int64_t left = deadline - mnm_now_ms();
			if (left <= 0)
				break;
			ssize_t n = mnm_recv_once(fd, answer + len, 8192 - len < 4096 ? 8192 - len : 4096, (int)left);
			if (n <= 0)
				break;
			len += (size_t)n;
			const uint8_t *order = find_bytes(answer, len, "ORDER=");
			const uint8_t *semi = memchr(order ? order : answer, ';', len - (size_t)((order ? order : answer) - answer));
			if (order && semi)
				break;
			int waiting = 0;
			if (ioctl(fd, FIONREAD, &waiting) != 0 || waiting <= 0)
				break;
		}
	}
	close(fd);
	if (identify_wago(answer, len)) {
		describe_wago(answer, len, detail, detail_len);
		found = 1;
	}
	free(answer);
	return found;
}

/* ----------------------------------------------------------- SECS/GEM */

/* An HSMS message header (SEMI E37): length (10 to 1 MiB), session, two
 * bytes, PType, SType, system bytes. */
static int hsms_header(const uint8_t *r, size_t len)
{
	if (len < 14)
		return 0;
	uint32_t length = u32be(r);
	return length >= 10 && length <= 0x100000;
}

/* A control answer of HSMS: Select.rsp, Deselect.rsp, Linktest.rsp or Reject.req. */
static int identify_secsgem(const uint8_t *r, size_t len)
{
	return hsms_header(r, len) && r[8] == 0 && (r[9] == 2 || r[9] == 4 || r[9] == 6 || r[9] == 7);
}

static int describe_secsgem(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	if (!hsms_header(r, len))
		return 0;
	if (r[9] == 7)
		snprintf(out, outlen, "HSMS select rejected (reason %u) - link probably already in use.", r[7]);
	else if (r[9] != 2)
		snprintf(out, outlen, "HSMS answered, but did not open a session.");
	else if (r[7] == 0)
		snprintf(out, outlen, "HSMS select accepted.");
	else
		snprintf(out, outlen, "HSMS select refused (status %u).", r[7]);
	return 1;
}

/* Reads one complete HSMS message and returns its PType, -1 if the peer closed
 * or it is not HSMS, -2 on timeout. Only the header is kept. */
static int read_hsms(int fd, int64_t deadline)
{
	uint8_t head[14], rest[512];
	int r = mnm_recv_exact(fd, head, 4, deadline);
	if (r < 0)
		return r;
	uint32_t length = u32be(head);
	if (length < 10 || length > 0x100000)
		return -1;
	if ((r = mnm_recv_exact(fd, head + 4, 10, deadline)) < 0)
		return r;
	for (uint32_t left = length - 10; left > 0;) {
		size_t part = left < sizeof(rest) ? left : sizeof(rest);
		if ((r = mnm_recv_exact(fd, rest, part, deadline)) < 0)
			return r;
		left -= (uint32_t)part;
	}
	return head[8];
}

/*
 * Select.req on session 0xFFFF. An accepted session is ended again with
 * Separate.req: equipment allows only one, and it belongs to its host. If the
 * port stays silent or answers with something else, three more attempts on new
 * connections (Select.req on sessions 0 and 1, Linktest.req): any HSMS answer
 * then counts. S1F13 (Establish Communication) is never sent - it would put the
 * equipment into the communicating state.
 */
static int run_secsgem(uint32_t address, uint16_t port, int timeout_ms, char *detail, size_t detail_len)
{
	static const struct { const uint8_t *packet; const char *note; } knocks[] = {
		{ pkt_secsgem_select_req_session0, "HSMS answered on session 0." },
		{ pkt_secsgem_select_req_session1, "HSMS answered on session 1." },
		{ pkt_secsgem_linktest_req_knock, "HSMS linktest answered without a session." },
	};
	uint8_t answer[4096];
	size_t len;
	int fd = greet(address, port, pkt_secsgem_select_req, sizeof(pkt_secsgem_select_req), timeout_ms, answer,
		       sizeof(answer), &len);
	if (fd < 0)
		return 0;
	if (identify_secsgem(answer, len)) {
		describe_secsgem(answer, len, detail, detail_len);
		if (answer[9] == 2 && answer[7] == 0)
			mnm_send_all(fd, pkt_secsgem_separate_req, sizeof(pkt_secsgem_separate_req), 500);
		close(fd);
		return 1;
	}
	close(fd);
	int t = timeout_ms < 1500 ? timeout_ms : 1500;
	for (size_t k = 0; k < sizeof(knocks) / sizeof(knocks[0]); k++) {
		if (mnm_tcp_connect(address, port, t, &fd) != MNM_CONNECTED)
			continue;
		int64_t deadline = mnm_now_ms() + t;
		int ptype = mnm_send_all(fd, knocks[k].packet, 14, t) == 0 ? read_hsms(fd, deadline) : -1;
		close(fd);
		if (ptype == 0) {
			snprintf(detail, detail_len, "%s", knocks[k].note);
			return 1;
		}
	}
	return 0;
}

/* -------------------------------------------------------------- table */

#define FIXED(a) a, sizeof(a), NULL

const struct mnm_probe mnm_probes_steps[] = {
	{ "mssqlserver", "MS-SQL", FIXED(pkt_mssql_prelogin), identify_mssql, describe_mssql, NULL, MNM_TCP, NULL, NULL },
	{ "opcua", "OPC UA", FIXED(pkt_opcua_hello), identify_opcua, NULL, NULL, MNM_TCP, NULL, NULL },
	{ "s7", "S7", FIXED(pkt_s7_cotp_cr), identify_s7, NULL, run_s7, MNM_TCP, NULL, NULL },
	{ "modbus", "Modbus", FIXED(pkt_modbus_read_holding_registers), identify_modbus, NULL, run_modbus, MNM_TCP, NULL, NULL },
	{ "wago", "WAGO", FIXED(pkt_wago_read_device_id), identify_wago, describe_wago, run_wago, MNM_TCP, NULL, NULL },
	{ "secsgem", "SECS/GEM", FIXED(pkt_secsgem_select_req), identify_secsgem, describe_secsgem, run_secsgem, MNM_TCP, NULL, NULL },
	{ NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, MNM_TCP, NULL, NULL },
};
