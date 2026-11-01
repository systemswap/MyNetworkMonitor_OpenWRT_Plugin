/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TLS connections for recognising web services and reading their
 * certificates, over mbedTLS (the TLS library of OpenWrt). This file is the
 * only place that knows mbedTLS.
 *
 * The certificate is read, not checked: the scanner wants to know who a device
 * claims to be, and devices in the own network mostly have self-signed
 * certificates. Nothing that is read here is trusted for anything.
 */
#ifndef MNM_TLSCONN_H
#define MNM_TLSCONN_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* How a connection attempt ended. */
enum mnm_tls_result {
	MNM_TLS_OK = 0,
	MNM_TLS_NO_CONNECTION,  /* the TCP connection failed */
	MNM_TLS_NOT_TLS,        /* something answered, but not with TLS */
	MNM_TLS_REFUSED,        /* TLS answered, but no handshake came about (e.g. only TLS 1.0, or no common cipher) */
	MNM_TLS_TIMEOUT,        /* no answer in time */
};

/* What a certificate says. Texts are printable ASCII, cut to their size. */
struct mnm_cert {
	char subject[128];        /* common name, else the whole subject */
	char issuer[128];         /* common name of the issuer, else the whole issuer */
	char names[192];          /* DNS names and addresses of the subject alternative names, comma-separated */
	char not_before[20];      /* "YYYY-MM-DD hh:mm" UTC */
	char not_after[20];
	char key[32];             /* e.g. "RSA 2048", "EC 256" */
	char sha256[65];          /* fingerprint of the whole certificate, lowercase hex */
	int self_signed;          /* subject and issuer are the same */
};

struct mnm_tls;

/* Connects to address:port and makes the TLS handshake (TLS 1.2 or 1.3),
 * with sni as server name if not NULL, within timeout_ms. Returns the
 * connection, or NULL with the reason in *result. */
struct mnm_tls *mnm_tls_open(uint32_t address, uint16_t port, const char *sni, int timeout_ms,
			     enum mnm_tls_result *result);

/* The protocol, e.g. "TLSv1.3". */
const char *mnm_tls_version(const struct mnm_tls *t);

/* The certificate of the server; 0 if it sent none. */
int mnm_tls_certificate(const struct mnm_tls *t, struct mnm_cert *cert);

/* Sends all of data within timeout_ms; 0 or -1. */
int mnm_tls_write(struct mnm_tls *t, const void *data, size_t len, int timeout_ms);

/* Waits up to timeout_ms and reads what arrived (at most cap bytes). Returns
 * the length, 0 if the peer closed, -1 on error, -2 on timeout. */
ssize_t mnm_tls_read(struct mnm_tls *t, void *buf, size_t cap, int timeout_ms);

void mnm_tls_close(struct mnm_tls *t);

#endif
