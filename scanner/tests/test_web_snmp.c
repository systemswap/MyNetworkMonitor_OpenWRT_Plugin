/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tests of the web probes and SNMP: the requests against those of the desktop
 * program (the arrays below are its bytes for the address 192.0.2.1, compared
 * with its source by a script), the detail of a web page with a certificate,
 * the reading of SNMP answers, and complete runs against a web server and an
 * SNMP agent on the loopback interface. Made-up names, documentation addresses.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../src/probes.h"
#include "../src/probes_snmp.h"
#include "../src/probes_web.h"
#include "check.h"

static const uint8_t pkt_webidentity_http_get[103] = {
	0x47, 0x45, 0x54, 0x20, 0x2f, 0x20, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x31,
	0x2e, 0x31, 0x0d, 0x0a, 0x48, 0x6f, 0x73, 0x74, 0x3a, 0x20, 0x31, 0x39,
	0x32, 0x2e, 0x30, 0x2e, 0x32, 0x2e, 0x31, 0x0d, 0x0a, 0x55, 0x73, 0x65,
	0x72, 0x2d, 0x41, 0x67, 0x65, 0x6e, 0x74, 0x3a, 0x20, 0x4d, 0x79, 0x4e,
	0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x4d, 0x6f, 0x6e, 0x69, 0x74, 0x6f,
	0x72, 0x0d, 0x0a, 0x41, 0x63, 0x63, 0x65, 0x70, 0x74, 0x3a, 0x20, 0x74,
	0x65, 0x78, 0x74, 0x2f, 0x68, 0x74, 0x6d, 0x6c, 0x0d, 0x0a, 0x43, 0x6f,
	0x6e, 0x6e, 0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x3a, 0x20, 0x63, 0x6c,
	0x6f, 0x73, 0x65, 0x0d, 0x0a, 0x0d, 0x0a,
};
static const uint8_t pkt_webservices_http_get[147] = {
	0x47, 0x45, 0x54, 0x20, 0x2f, 0x20, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x31,
	0x2e, 0x31, 0x0d, 0x0a, 0x48, 0x6f, 0x73, 0x74, 0x3a, 0x20, 0x31, 0x39,
	0x32, 0x2e, 0x30, 0x2e, 0x32, 0x2e, 0x31, 0x0d, 0x0a, 0x43, 0x6f, 0x6e,
	0x6e, 0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x3a, 0x20, 0x63, 0x6c, 0x6f,
	0x73, 0x65, 0x0d, 0x0a, 0x55, 0x73, 0x65, 0x72, 0x2d, 0x41, 0x67, 0x65,
	0x6e, 0x74, 0x3a, 0x20, 0x4d, 0x6f, 0x7a, 0x69, 0x6c, 0x6c, 0x61, 0x2f,
	0x35, 0x2e, 0x30, 0x20, 0x28, 0x63, 0x6f, 0x6d, 0x70, 0x61, 0x74, 0x69,
	0x62, 0x6c, 0x65, 0x3b, 0x20, 0x4d, 0x79, 0x53, 0x63, 0x61, 0x6e, 0x6e,
	0x65, 0x72, 0x2f, 0x31, 0x2e, 0x30, 0x29, 0x0d, 0x0a, 0x41, 0x63, 0x63,
	0x65, 0x70, 0x74, 0x3a, 0x20, 0x2a, 0x2f, 0x2a, 0x0d, 0x0a, 0x41, 0x63,
	0x63, 0x65, 0x70, 0x74, 0x2d, 0x45, 0x6e, 0x63, 0x6f, 0x64, 0x69, 0x6e,
	0x67, 0x3a, 0x20, 0x69, 0x64, 0x65, 0x6e, 0x74, 0x69, 0x74, 0x79, 0x0d,
	0x0a, 0x0d, 0x0a,
};
static const uint8_t pkt_webservices_https_get[54] = {
	0x47, 0x45, 0x54, 0x20, 0x2f, 0x20, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x31,
	0x2e, 0x31, 0x0d, 0x0a, 0x48, 0x6f, 0x73, 0x74, 0x3a, 0x20, 0x31, 0x39,
	0x32, 0x2e, 0x30, 0x2e, 0x32, 0x2e, 0x31, 0x0d, 0x0a, 0x43, 0x6f, 0x6e,
	0x6e, 0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x3a, 0x20, 0x63, 0x6c, 0x6f,
	0x73, 0x65, 0x0d, 0x0a, 0x0d, 0x0a,
};


/* ------------------------------------------------------------- BER helpers */

static size_t put_tlv(uint8_t *p, uint8_t tag, const uint8_t *v, size_t n)
{
	size_t k = 0;
	p[k++] = tag;
	if (n < 128) {
		p[k++] = (uint8_t)n;
	} else {
		p[k++] = 0x81;
		p[k++] = (uint8_t)n;
	}
	if (n)
		memmove(p + k, v, n);
	return k + n;
}

/* A Response with the given ID, error status and values (OID bytes, tag, value). */
struct bind { const uint8_t *oid; size_t oid_len; uint8_t tag; const uint8_t *value; size_t len; };

static size_t snmp_answer(uint8_t *out, uint32_t id, int error, const struct bind *binds, size_t count)
{
	uint8_t list[600], pdu[700], msg[800], tmp[300];
	size_t l = 0, p = 0, m = 0;
	for (size_t i = 0; i < count; i++) {
		size_t t = put_tlv(tmp, 0x06, binds[i].oid, binds[i].oid_len);
		t += put_tlv(tmp + t, binds[i].tag, binds[i].value, binds[i].len);
		l += put_tlv(list + l, 0x30, tmp, t);
	}
	const uint8_t idb[4] = { (uint8_t)(id >> 24), (uint8_t)(id >> 16), (uint8_t)(id >> 8), (uint8_t)id };
	const uint8_t err[1] = { (uint8_t)error }, zero[1] = { 0 };
	p += put_tlv(pdu + p, 0x02, idb, 4);
	p += put_tlv(pdu + p, 0x02, err, 1);
	p += put_tlv(pdu + p, 0x02, zero, 1);
	p += put_tlv(pdu + p, 0x30, list, l);
	m += put_tlv(msg + m, 0x02, zero, 1);
	m += put_tlv(msg + m, 0x04, (const uint8_t *)"public", 6);
	m += put_tlv(msg + m, 0xa2, pdu, p);
	return put_tlv(out, 0x30, msg, m);
}

static const uint8_t oid_name[] = { 0x2b, 6, 1, 2, 1, 1, 5, 0 }, oid_descr[] = { 0x2b, 6, 1, 2, 1, 1, 1, 0 },
		     oid_loc[] = { 0x2b, 6, 1, 2, 1, 1, 6, 0 }, oid_contact[] = { 0x2b, 6, 1, 2, 1, 1, 4, 0 };

/* ------------------------------------------------------------ SNMP agent */

struct agent {
	int fd;
	int requests;
};

/* Answers the system group, the serial number and ifPhysAddress.1; refuses
 * .2 with noSuchName and leaves .3 unanswered, where the probe must stop. */
static void *serve_snmp(void *arg)
{
	struct agent *a = arg;
	for (;;) {
		uint8_t req[200], out[900];
		struct sockaddr_in from;
		socklen_t flen = sizeof(from);
		struct pollfd p = { .fd = a->fd, .events = POLLIN };
		if (poll(&p, 1, 3000) <= 0)
			return NULL;
		ssize_t n = recvfrom(a->fd, req, sizeof(req), 0, (struct sockaddr *)&from, &flen);
		if (n < 21)
			return NULL;
		a->requests++;
		uint32_t id = (uint32_t)req[17] << 24 | (uint32_t)req[18] << 16 | (uint32_t)req[19] << 8 | req[20];
		size_t len = 0;
		if (n == 85) {
			struct bind b[4] = { { oid_name, 8, 0x04, (const uint8_t *)"printer-1", 9 },
					     { oid_descr, 8, 0x04, (const uint8_t *)"Example Printer\tModel 2", 23 },
					     { oid_loc, 8, 0x04, (const uint8_t *)"Office", 6 },
					     { oid_contact, 8, 0x04, (const uint8_t *)"admin@example.org", 17 } };
			len = snmp_answer(out, id, 0, b, 4);
		} else if (n == 46) {
			struct bind b = { oid_name, 8, 0x04, (const uint8_t *)"SN123", 5 };
			len = snmp_answer(out, id, 0, &b, 1);
		} else if (n == 45 && req[n - 3] == 1) {
			struct bind b = { oid_name, 8, 0x04, (const uint8_t *)"\x02\x00\x00\x00\x00\x01", 6 };
			len = snmp_answer(out, id, 0, &b, 1);
		} else if (n == 45 && req[n - 3] == 2) {
			struct bind b = { oid_name, 8, 0x05, NULL, 0 };
			len = snmp_answer(out, id, 2, &b, 1);
		} else {
			continue;   /* silent */
		}
		sendto(a->fd, out, len, 0, (struct sockaddr *)&from, flen);
	}
}

/* ------------------------------------------------------------ web server */

static void *serve_page(void *arg)
{
	int listen_fd = *(int *)arg;
	char req[512];
	struct pollfd p = { .fd = listen_fd, .events = POLLIN };
	if (poll(&p, 1, 3000) <= 0)
		return NULL;
	int fd = accept(listen_fd, NULL, NULL);
	if (fd < 0)
		return NULL;
	recv(fd, req, sizeof(req), 0);
	static const char page[] = "HTTP/1.1 200 OK\r\nServer: ExampleServer/1.0\r\nContent-Type: text/html\r\n\r\n"
				   "<html><head><title>\n  Example &amp; NAS\n</title></head><body>x</body></html>";
	send(fd, page, sizeof(page) - 1, MSG_NOSIGNAL);
	close(fd);
	return NULL;
}

void test_web_snmp(void)
{
	char buf[256], d[MNM_DETAIL_LEN];

	/* the requests of the desktop program */
	size_t n = mnm_web_identity_request(ip("192.0.2.1"), buf, sizeof(buf));
	CHECK(n == sizeof(pkt_webidentity_http_get) && memcmp(buf, pkt_webidentity_http_get, n) == 0);
	n = mnm_web_https_request(ip("192.0.2.1"), buf, sizeof(buf));
	CHECK(n == sizeof(pkt_webservices_https_get) && memcmp(buf, pkt_webservices_https_get, n) == 0);
	uint8_t http[512];
	n = mnm_probe_find("webservices")->build(ip("192.0.2.1"), http, sizeof(http));
	CHECK(n == sizeof(pkt_webservices_http_get) && memcmp(http, pkt_webservices_http_get, n) == 0);

	/* the detail of a web page with its certificate */
	static const char page[] = "HTTP/1.1 200 OK\r\nServer: lighttpd\r\n\r\n<title>Router &lt;1&gt;</title>";
	struct mnm_cert c = { .subject = "nas.example", .issuer = "nas.example", .names = "nas.example, 192.0.2.10",
			      .not_after = "2025-06-30 12:00", .self_signed = 1 };
	mnm_web_identity_describe(page, sizeof(page) - 1, &c, "2026-11-01", d, sizeof(d));
	CHECK(strcmp(d, "Title: Router <1> · Server: lighttpd · Certificate for: nas.example · Issued by: nas.example · "
			"Valid until: 2025-06-30 - EXPIRED · Self-signed certificate · Also named: nas.example, 192.0.2.10") == 0);
	memcpy(c.not_after, "2027-01-01 00:00", 17);
	c.self_signed = 0, c.names[0] = '\0';
	mnm_web_identity_describe(NULL, 0, &c, "2026-11-01", d, sizeof(d));
	CHECK(strcmp(d, "Certificate for: nas.example · Issued by: nas.example · Valid until: 2027-01-01") == 0);

	/* SNMP answers */
	uint8_t r[900];
	struct mnm_snmp_value v[4];
	struct bind b[2] = { { oid_name, 8, 0x04, (const uint8_t *)"\xc3\xa4rger", 6 },
			     { oid_descr, 8, 0x80, NULL, 0 } };
	size_t len = snmp_answer(r, 0x12345678, 0, b, 2);
	CHECK(mnm_snmp_parse(r, len, 0x12345678, v, 4) == 2);
	CHECK(v[0].present && strcmp(v[0].text, "\xc3\xa4rger") == 0 && !v[1].present);   /* UTF-8 text; noSuchObject */
	CHECK(mnm_snmp_parse(r, len, 0x12345679, v, 4) == -1);                              /* another request */
	len = snmp_answer(r, 7, 2, b, 1);
	CHECK(mnm_snmp_parse(r, len, 7, v, 4) == -1);                                       /* noSuchName */
	static const uint8_t ticks[] = { 0x01, 0x02, 0x03 }, binary[] = { 0x00, 0x1b, 0xff };
	struct bind c2[2] = { { oid_name, 8, 0x43, ticks, 3 }, { oid_descr, 8, 0x04, binary, 3 } };
	len = snmp_answer(r, 9, 0, c2, 2);
	CHECK(mnm_snmp_parse(r, len, 9, v, 4) == 2 && strcmp(v[0].text, "66051") == 0 && strcmp(v[1].text, "00 1b ff") == 0);
	for (size_t cut = 0; cut < len; cut++)
		mnm_snmp_parse(r, cut, 9, v, 4);

	/* a complete SNMP run against an agent on the loopback interface */
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(0x7f000001u) };
	socklen_t sl = sizeof(sa);
	struct agent a = { .fd = socket(AF_INET, SOCK_DGRAM, 0) };
	pthread_t thread;
	CHECK(a.fd >= 0 && bind(a.fd, (struct sockaddr *)&sa, sizeof(sa)) == 0 &&
	      getsockname(a.fd, (struct sockaddr *)&sa, &sl) == 0 && pthread_create(&thread, NULL, serve_snmp, &a) == 0);
	int found = mnm_probe_find("snmp")->run(0x7f000001u, ntohs(sa.sin_port), 500, d, sizeof(d));
	pthread_join(thread, NULL);
	close(a.fd);
	CHECK(found == 1);
	CHECK(strcmp(d, "Name: printer-1 · Descr: Example Printer Model 2 · Location: Office · "
			"Contact: admin@example.org · Serial: SN123 · MAC: 02-00-00-00-00-01") == 0);
	CHECK(a.requests == 5);   /* system, serial, MAC 1, 2 and 3 - then the probe stops */

	/* the web page check against an HTTP server */
	uint16_t port;
	int listen_fd = listen_local(&port);
	CHECK(listen_fd >= 0 && pthread_create(&thread, NULL, serve_page, &listen_fd) == 0);
	found = mnm_probe_find("webidentity")->run(0x7f000001u, port, 500, d, sizeof(d));
	pthread_join(thread, NULL);
	close(listen_fd);
	CHECK(found == 1 && strcmp(d, "Title: Example & NAS · Server: ExampleServer/1.0") == 0);
}
