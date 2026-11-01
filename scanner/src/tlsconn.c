/* SPDX-License-Identifier: GPL-2.0-only */
#include "tlsconn.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/oid.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include "netio.h"

struct mnm_tls {
	int fd;
	mbedtls_ssl_context ssl;
	uint8_t first;        /* the first byte the server sent, to tell TLS from other answers */
	int received;
};

/* One configuration and one random generator for all connections, made once:
 * TLS 1.3 in mbedTLS needs PSA crypto, which is set up per process. */
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int ready;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_ssl_config config;

static void setup(void)
{
	static const char personal[] = "mynetworkmonitor-scan";
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&drbg);
	mbedtls_ssl_config_init(&config);
	if (psa_crypto_init() != PSA_SUCCESS ||
	    mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)personal,
				  sizeof(personal) - 1) != 0 ||
	    mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
					MBEDTLS_SSL_PRESET_DEFAULT) != 0)
		return;
	/* read, not check: see tlsconn.h */
	mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_NONE);
	mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &drbg);
	ready = 1;
}

static int send_cb(void *context, const unsigned char *buf, size_t len)
{
	struct mnm_tls *t = context;
	ssize_t n = send(t->fd, buf, len, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (n >= 0)
		return (int)n;
	return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? MBEDTLS_ERR_SSL_WANT_WRITE
									   : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int recv_cb(void *context, unsigned char *buf, size_t len)
{
	struct mnm_tls *t = context;
	ssize_t n = recv(t->fd, buf, len, MSG_DONTWAIT);
	if (n > 0 && !t->received) {
		t->first = buf[0];
		t->received = 1;
	}
	if (n >= 0)
		return n == 0 ? MBEDTLS_ERR_SSL_CONN_EOF : (int)n;
	return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? MBEDTLS_ERR_SSL_WANT_READ
									   : MBEDTLS_ERR_NET_RECV_FAILED;
}

/* Waits until the socket can go on as mbedTLS asked (want), until end. */
static int wait_for(int fd, int want, int64_t end)
{
	int64_t left = end - mnm_now_ms();
	struct pollfd p = { .fd = fd, .events = (short)(want == MBEDTLS_ERR_SSL_WANT_WRITE ? POLLOUT : POLLIN) };
	return left > 0 && poll(&p, 1, (int)left) > 0;
}

static void free_tls(struct mnm_tls *t)
{
	mbedtls_ssl_free(&t->ssl);
	if (t->fd >= 0)
		close(t->fd);
	free(t);
}

struct mnm_tls *mnm_tls_open(uint32_t address, uint16_t port, const char *sni, int timeout_ms,
			     enum mnm_tls_result *result)
{
	struct mnm_tls *t;
	int fd;

	pthread_once(&once, setup);
	*result = MNM_TLS_NO_CONNECTION;
	if (!ready || mnm_tcp_connect(address, port, timeout_ms, &fd) != MNM_CONNECTED)
		return NULL;
	t = calloc(1, sizeof(*t));
	if (!t) {
		close(fd);
		return NULL;
	}
	t->fd = fd;
	mbedtls_ssl_init(&t->ssl);
	if (mbedtls_ssl_setup(&t->ssl, &config) != 0 || (sni && mbedtls_ssl_set_hostname(&t->ssl, sni) != 0)) {
		free_tls(t);
		return NULL;
	}
	mbedtls_ssl_set_bio(&t->ssl, t, send_cb, recv_cb, NULL);

	int64_t end = mnm_now_ms() + timeout_ms;
	int r;
	while ((r = mbedtls_ssl_handshake(&t->ssl)) != 0) {
		if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
			if (wait_for(fd, r, end))
				continue;
			*result = t->received ? MNM_TLS_REFUSED : MNM_TLS_TIMEOUT;
			free_tls(t);
			return NULL;
		}
		/* A TLS record starts with its type (handshake 0x16, alert 0x15):
		 * whatever went wrong, the other side speaks TLS. */
		*result = t->received && (t->first == 0x16 || t->first == 0x15) ? MNM_TLS_REFUSED
			: t->received ? MNM_TLS_NOT_TLS : MNM_TLS_REFUSED;
		free_tls(t);
		return NULL;
	}
	*result = MNM_TLS_OK;
	return t;
}

const char *mnm_tls_version(const struct mnm_tls *t)
{
	return mbedtls_ssl_get_version(&t->ssl);
}

/* The value of the common name in a distinguished name, else the whole name. */
static void dn_text(const mbedtls_x509_name *dn, char *out, size_t outlen)
{
	out[0] = '\0';
	for (const mbedtls_x509_name *n = dn; n; n = n->next) {
		if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid) == 0) {
			snprintf(out, outlen, "%.*s", (int)n->val.len, (const char *)n->val.p);
			break;
		}
	}
	if (!out[0] && mbedtls_x509_dn_gets(out, outlen, dn) < 0)
		out[0] = '\0';
	for (char *c = out; *c; c++)
		if ((unsigned char)*c < 0x20 || (unsigned char)*c == 0x7f)
			*c = '?';
}

static void time_text(const mbedtls_x509_time *t, char *out, size_t outlen)
{
	snprintf(out, outlen, "%04d-%02d-%02d %02d:%02d", t->year, t->mon, t->day, t->hour, t->min);
}

int mnm_tls_certificate(const struct mnm_tls *t, struct mnm_cert *cert)
{
	const mbedtls_x509_crt *crt = mbedtls_ssl_get_peer_cert(&t->ssl);
	unsigned char hash[32];

	memset(cert, 0, sizeof(*cert));
	if (!crt)
		return 0;
	dn_text(&crt->subject, cert->subject, sizeof(cert->subject));
	dn_text(&crt->issuer, cert->issuer, sizeof(cert->issuer));
	time_text(&crt->valid_from, cert->not_before, sizeof(cert->not_before));
	time_text(&crt->valid_to, cert->not_after, sizeof(cert->not_after));
	cert->self_signed = crt->subject_raw.len == crt->issuer_raw.len &&
			    memcmp(crt->subject_raw.p, crt->issuer_raw.p, crt->subject_raw.len) == 0;
	snprintf(cert->key, sizeof(cert->key), "%s %u", mbedtls_pk_get_name(&crt->pk),
		 (unsigned int)mbedtls_pk_get_bitlen(&crt->pk));
	if (mbedtls_sha256(crt->raw.p, crt->raw.len, hash, 0) == 0)
		for (int i = 0; i < 32; i++)
			snprintf(cert->sha256 + 2 * i, 3, "%02x", hash[i]);

	/* the subject alternative names: DNS names and IP addresses (RFC 5280, 4.2.1.6) */
	for (const mbedtls_x509_sequence *s = &crt->subject_alt_names; s && s->buf.p; s = s->next) {
		mbedtls_x509_subject_alternative_name san;
		char one[80] = "";
		if (mbedtls_x509_parse_subject_alt_name(&s->buf, &san) != 0)
			continue;
		if (san.type == MBEDTLS_X509_SAN_DNS_NAME)
			snprintf(one, sizeof(one), "%.*s", (int)san.san.unstructured_name.len,
				 (const char *)san.san.unstructured_name.p);
		else if (san.type == MBEDTLS_X509_SAN_IP_ADDRESS && san.san.unstructured_name.len == 4)
			inet_ntop(AF_INET, san.san.unstructured_name.p, one, sizeof(one));
		else if (san.type == MBEDTLS_X509_SAN_IP_ADDRESS && san.san.unstructured_name.len == 16)
			inet_ntop(AF_INET6, san.san.unstructured_name.p, one, sizeof(one));
		mbedtls_x509_free_subject_alt_name(&san);
		for (char *c = one; *c; c++)
			if ((unsigned char)*c < 0x20 || (unsigned char)*c == 0x7f || *c == ',')
				*c = '?';
		size_t used = strlen(cert->names);
		if (one[0] && used + strlen(one) + 3 < sizeof(cert->names))
			snprintf(cert->names + used, sizeof(cert->names) - used, "%s%s", used ? ", " : "", one);
	}
	return 1;
}

int mnm_tls_write(struct mnm_tls *t, const void *data, size_t len, int timeout_ms)
{
	const unsigned char *p = data;
	int64_t end = mnm_now_ms() + timeout_ms;
	while (len > 0) {
		int n = mbedtls_ssl_write(&t->ssl, p, len);
		if (n > 0) {
			p += n;
			len -= (size_t)n;
			continue;
		}
		if ((n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) && wait_for(t->fd, n, end))
			continue;
		return -1;
	}
	return 0;
}

ssize_t mnm_tls_read(struct mnm_tls *t, void *buf, size_t cap, int timeout_ms)
{
	int64_t end = mnm_now_ms() + timeout_ms;
	for (;;) {
		int n = mbedtls_ssl_read(&t->ssl, buf, cap);
		if (n > 0)
			return n;
		if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == MBEDTLS_ERR_SSL_CONN_EOF)
			return 0;
		/* TLS 1.3 sends tickets after the handshake; they are no data */
		if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
			continue;
		if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
			if (wait_for(t->fd, n, end))
				continue;
			return -2;
		}
		return -1;
	}
}

void mnm_tls_close(struct mnm_tls *t)
{
	if (!t)
		return;
	mbedtls_ssl_close_notify(&t->ssl);
	free_tls(t);
}
