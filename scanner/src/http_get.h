/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * A minimal HTTP/1.1 GET for device descriptions in the own network: plain
 * HTTP only, one request per connection, the body limited in size and time.
 */
#ifndef MNM_HTTP_GET_H
#define MNM_HTTP_GET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Builds the request as the desktop program sends it: request line and Host
 * header only (":port" left out for port 80). Returns its length, 0 if it does
 * not fit. */
size_t mnm_http_request(const char *host, uint16_t port, const char *path, char *buf, size_t cap);

/* The body of a complete HTTP answer in buf: status 2xx, then up to
 * Content-Length bytes, or the chunks of "Transfer-Encoding: chunked" joined in
 * place, or everything after the header. Returns the body length and its start
 * in *body, or -1 for another status or a damaged answer. */
ssize_t mnm_http_body(char *buf, size_t len, char **body);

/* Whether buf already holds a complete answer (header and the whole body). */
int mnm_http_complete(const char *buf, size_t len);

/* GET http://<address>:<port><path>, with host as Host header, within
 * timeout_ms altogether. Writes the body (NUL-terminated) to out; returns its
 * length or -1. */
ssize_t mnm_http_get(uint32_t address, uint16_t port, const char *host, const char *path, int timeout_ms, char *out,
		     size_t cap);

#endif
