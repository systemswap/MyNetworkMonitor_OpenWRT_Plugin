/* SPDX-License-Identifier: GPL-2.0-only */
#include "probes_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bc_parse.h"
#include "netio.h"
#include "probes_steps.h"
#include "targets.h"

/* The requests of the desktop program, byte for byte; only the address in the
 * Host header changes (compared with its source in the tests). */
size_t mnm_web_https_request(uint32_t address, char *buf, size_t cap)
{
	char host[16];
	mnm_format_ipv4(address, host);
	int n = snprintf(buf, cap, "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
	return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

size_t mnm_web_identity_request(uint32_t address, char *buf, size_t cap)
{
	char host[16];
	mnm_format_ipv4(address, host);
	int n = snprintf(buf, cap,
			 "GET / HTTP/1.1\r\nHost: %s\r\nUser-Agent: MyNetworkMonitor\r\nAccept: text/html\r\n"
			 "Connection: close\r\n\r\n", host);
	return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

/* Reads until the title is complete, the server closes, the time is up or
 * 64 KiB have arrived; with t != NULL over TLS, else from fd. */
static size_t read_page(struct mnm_tls *t, int fd, char *buf, size_t cap, int64_t end)
{
	size_t len = 0;
	while (len + 1 < cap) {
		int64_t left = end - mnm_now_ms();
		if (left <= 0)
			break;
		ssize_t n = t ? mnm_tls_read(t, buf + len, cap - 1 - len, (int)left)
			      : mnm_recv_once(fd, buf + len, cap - 1 - len, (int)left);
		if (n <= 0)
			break;
		len += (size_t)n;
		buf[len] = '\0';
		if (strstr(buf, "</title>") || strstr(buf, "</TITLE>") || strstr(buf, "</Title>"))
			break;
	}
	buf[len] = '\0';
	return len;
}

/* ------------------------------------------------------- web over TLS */

/*
 * The second half of the desktop program's web probe: the same port over
 * TLS, with its short request (Host and "Connection: close"); 1 s for the
 * connection and 2 s for handshake, request and answer there. A port that
 * answers with TLS but allows no handshake with TLS 1.2 or 1.3 (old devices)
 * still speaks TLS and is reported as such.
 */
static int run_https(uint32_t address, uint16_t port, int timeout_ms, char *detail, size_t detail_len)
{
	enum mnm_tls_result result;
	char request[128], answer[4096];
	(void)timeout_ms;
	struct mnm_tls *t = mnm_tls_open(address, port, NULL, 3000, &result);
	if (!t) {
		if (result != MNM_TLS_REFUSED)
			return 0;
		snprintf(detail, detail_len, "TLS answered, but only with older versions or ciphers");
		return 1;
	}
	size_t rlen = mnm_web_https_request(address, request, sizeof(request));
	size_t len = 0;
	if (rlen && mnm_tls_write(t, request, rlen, 2000) == 0)
		len = read_page(t, -1, answer, sizeof(answer), mnm_now_ms() + 2000);
	char version[16];
	snprintf(version, sizeof(version), "%s", mnm_tls_version(t));
	mnm_tls_close(t);
	if (!len || !mnm_http_identify((const uint8_t *)answer, len))
		return 0;
	char server[128] = "";
	detail[0] = '\0';
	if (mnm_http_describe((const uint8_t *)answer, len, server, sizeof(server)))
		mnm_detail_add(detail, detail_len, server);
	mnm_detail_add(detail, detail_len, version);
	return 1;
}

/* --------------------------------------------------- web page identity */

void mnm_web_identity_describe(const char *answer, size_t len, const struct mnm_cert *cert, const char *today,
			       char *out, size_t outlen)
{
	char text[200], part[260];
	out[0] = '\0';
	if (answer && mnm_xml_text(answer, len, "title", text, sizeof(text))) {
		snprintf(part, sizeof(part), "Title: %s", text);
		mnm_detail_add(out, outlen, part);
	}
	if (answer && mnm_ssdp_header(answer, len, "Server", text, sizeof(text))) {
		snprintf(part, sizeof(part), "Server: %s", text);
		mnm_detail_add(out, outlen, part);
	}
	if (!cert)
		return;
	if (cert->subject[0]) {
		snprintf(part, sizeof(part), "Certificate for: %s", cert->subject);
		mnm_detail_add(out, outlen, part);
	}
	if (cert->issuer[0]) {
		snprintf(part, sizeof(part), "Issued by: %s", cert->issuer);
		mnm_detail_add(out, outlen, part);
	}
	if (cert->not_after[0]) {
		snprintf(part, sizeof(part), "Valid until: %.10s%s", cert->not_after,
			 strncmp(cert->not_after, today, 10) < 0 ? " - EXPIRED" : "");
		mnm_detail_add(out, outlen, part);
	}
	if (cert->self_signed)
		mnm_detail_add(out, outlen, "Self-signed certificate");
	if (cert->names[0]) {
		snprintf(part, sizeof(part), "Also named: %s", cert->names);
		mnm_detail_add(out, outlen, part);
	}
}

/*
 * What a web interface tells about itself, as the desktop program asks it:
 * TLS on 443 and 8443, plain HTTP on the other ports; 2.5 s for the
 * connection and again for handshake, request and answer. Over TLS the
 * certificate counts even if the page does not come.
 */
static int run_identity(uint32_t address, uint16_t port, int timeout_ms, char *detail, size_t detail_len)
{
	const int wait = 2500;
	int tls = port == 443 || port == 8443, fd = -1;
	struct mnm_tls *t = NULL;
	struct mnm_cert cert;
	enum mnm_tls_result result;
	char request[192], today[16];
	char *answer = malloc(64 * 1024);
	size_t len = 0;
	(void)timeout_ms;

	if (!answer)
		return 0;
	if (tls) {
		t = mnm_tls_open(address, port, NULL, 2 * wait, &result);
		if (!t) {
			free(answer);
			return 0;
		}
	} else if (mnm_tcp_connect(address, port, wait, &fd) != MNM_CONNECTED) {
		free(answer);
		return 0;
	}
	int64_t end = mnm_now_ms() + wait;
	size_t rlen = mnm_web_identity_request(address, request, sizeof(request));
	if (rlen && (t ? mnm_tls_write(t, request, rlen, wait) : mnm_send_all(fd, request, rlen, wait)) == 0)
		len = read_page(t, fd, answer, 64 * 1024, end);
	int has_cert = t && mnm_tls_certificate(t, &cert);
	if (t)
		mnm_tls_close(t);
	if (fd >= 0)
		close(fd);

	time_t now = time(NULL);
	struct tm utc;
	gmtime_r(&now, &utc);
	strftime(today, sizeof(today), "%Y-%m-%d", &utc);
	mnm_web_identity_describe(len ? answer : NULL, len, has_cert ? &cert : NULL, today, detail, detail_len);
	free(answer);
	return detail[0] != '\0';
}

/* -------------------------------------------------------------- table */

const struct mnm_probe mnm_probes_web[] = {
	{ "webservices_tls", "HTTPS", NULL, 0, NULL, mnm_http_identify, mnm_http_describe, run_https, MNM_TCP, NULL, NULL },
	{ "webidentity", "Web page", NULL, 0, NULL, mnm_http_identify, NULL, run_identity, MNM_TCP, NULL, NULL },
	{ NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, MNM_TCP, NULL, NULL },
};
