/* SPDX-License-Identifier: GPL-2.0-only */
#include "http_get.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "netio.h"

size_t mnm_http_request(const char *host, uint16_t port, const char *path, char *buf, size_t cap)
{
	int n = port == 80 ? snprintf(buf, cap, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", path, host)
			   : snprintf(buf, cap, "GET %s HTTP/1.1\r\nHost: %s:%u\r\n\r\n", path, host, port);
	return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

/* The end of the header (after the empty line), or NULL. */
static char *header_end(const char *buf, size_t len)
{
	for (size_t i = 0; i + 3 < len; i++)
		if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
			return (char *)buf + i + 4;
	return NULL;
}

/* The value of a header field in the header [buf, end), or NULL. */
static const char *field(const char *buf, const char *end, const char *name)
{
	size_t n = strlen(name);
	for (const char *line = buf; line < end;) {
		const char *eol = memchr(line, '\n', (size_t)(end - line));
		if (!eol)
			break;
		if ((size_t)(eol - line) > n && strncasecmp(line, name, n) == 0 && line[n] == ':') {
			const char *v = line + n + 1;
			while (v < eol && (*v == ' ' || *v == '\t'))
				v++;
			return v;
		}
		line = eol + 1;
	}
	return NULL;
}

/* Joins the chunks of a chunked body in place (RFC 9112, 7.1). Returns the
 * length of what arrived (*complete tells whether the last chunk was there),
 * or -1 if a chunk size is damaged. */
static ssize_t dechunk(char *body, size_t len, int *complete)
{
	size_t in = 0, out = 0;
	*complete = 0;
	while (in < len) {
		char *end;
		char *eol = memchr(body + in, '\n', len - in);
		if (!eol)
			return (ssize_t)out;
		unsigned long size = strtoul(body + in, &end, 16);
		if (end == body + in || size > len)
			return -1;
		in = (size_t)(eol - body) + 1;
		if (size == 0) {
			*complete = 1;
			return (ssize_t)out;
		}
		if (in + size > len)
			return (ssize_t)out;
		memmove(body + out, body + in, size);
		out += size;
		in += size;
		if (in < len && body[in] == '\r')
			in++;
		if (in < len && body[in] == '\n')
			in++;
	}
	return (ssize_t)out;
}

int mnm_http_complete(const char *buf, size_t len)
{
	char *end = header_end(buf, len);
	if (!end)
		return 0;
	const char *cl = field(buf, end, "Content-Length"), *te = field(buf, end, "Transfer-Encoding");
	if (te && strncasecmp(te, "chunked", 7) == 0) {
		/* the last chunk "0" followed by the empty line (no trailer fields) */
		size_t blen = len - (size_t)(end - buf);
		if (blen >= 5 && memcmp(end, "0\r\n\r\n", 5) == 0)
			return 1;
		for (size_t i = 0; i + 7 <= blen; i++)
			if (memcmp(end + i, "\r\n0\r\n\r\n", 7) == 0)
				return 1;
		return 0;
	}
	if (cl)
		return (size_t)(end - buf) + strtoul(cl, NULL, 10) <= len;
	return 0;   /* without a length the answer ends when the server closes */
}

ssize_t mnm_http_body(char *buf, size_t len, char **body)
{
	char *end = header_end(buf, len);
	if (len < 12 || strncmp(buf, "HTTP/1.", 7) != 0 || buf[9] != '2' || !end)
		return -1;
	size_t blen = len - (size_t)(end - buf);
	const char *cl = field(buf, end, "Content-Length"), *te = field(buf, end, "Transfer-Encoding");
	*body = end;
	if (te && strncasecmp(te, "chunked", 7) == 0) {
		int complete;
		return dechunk(end, blen, &complete);
	}
	if (cl) {
		unsigned long n = strtoul(cl, NULL, 10);
		if (n < blen)
			blen = n;
	}
	return (ssize_t)blen;
}

ssize_t mnm_http_get(uint32_t address, uint16_t port, const char *host, const char *path, int timeout_ms, char *out,
		     size_t cap)
{
	char request[600];
	size_t rlen = mnm_http_request(host, port, path, request, sizeof(request)), len = 0;
	int fd;
	if (!rlen || cap < 2)
		return -1;
	int64_t end = mnm_now_ms() + timeout_ms;
	if (mnm_tcp_connect(address, port, timeout_ms, &fd) != MNM_CONNECTED)
		return -1;
	if (mnm_send_all(fd, request, rlen, timeout_ms) != 0) {
		close(fd);
		return -1;
	}
	while (len + 1 < cap && !mnm_http_complete(out, len)) {
		int64_t left = end - mnm_now_ms();
		ssize_t n = left > 0 ? mnm_recv_once(fd, out + len, cap - 1 - len, (int)left) : -2;
		if (n <= 0)
			break;   /* closed (the end of an answer without length), timeout or error */
		len += (size_t)n;
	}
	close(fd);
	out[len] = '\0';
	char *body;
	ssize_t blen = mnm_http_body(out, len, &body);
	if (blen < 0)
		return -1;
	memmove(out, body, (size_t)blen);
	out[blen] = '\0';
	return blen;
}
