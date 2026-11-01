/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tests of the TLS connections against servers on the loopback interface: one
 * with a self-signed certificate made at run time (made-up name, documentation
 * address), one that answers without TLS, one that stays silent.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>
#include <psa/crypto.h>

#include "../src/probes.h"
#include "../src/tlsconn.h"
#include "check.h"

enum tls_role { TLS_SERVER, PLAIN_SERVER, SILENT_SERVER };

struct tls_server {
	int listen_fd;
	enum tls_role role;
	mbedtls_x509_crt *crt;
	mbedtls_pk_context *key;
	mbedtls_ctr_drbg_context *drbg;
	char request[256];
};

static int net_send(void *ctx, const unsigned char *buf, size_t len)
{
	ssize_t n = send(*(int *)ctx, buf, len, MSG_NOSIGNAL);
	return n < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)n;
}

static int net_recv(void *ctx, unsigned char *buf, size_t len)
{
	struct pollfd p = { .fd = *(int *)ctx, .events = POLLIN };
	if (poll(&p, 1, 3000) <= 0)
		return MBEDTLS_ERR_SSL_TIMEOUT;
	ssize_t n = recv(*(int *)ctx, buf, len, 0);
	return n < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : n == 0 ? MBEDTLS_ERR_SSL_CONN_EOF : (int)n;
}

static void *serve_tls(void *arg)
{
	struct tls_server *s = arg;
	struct pollfd p = { .fd = s->listen_fd, .events = POLLIN };
	if (poll(&p, 1, 3000) <= 0)
		return NULL;
	int fd = accept(s->listen_fd, NULL, NULL);
	if (fd < 0)
		return NULL;
	if (s->role == PLAIN_SERVER) {
		char buf[512];
		static const char bad[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
		recv(fd, buf, sizeof(buf), 0);
		send(fd, bad, sizeof(bad) - 1, MSG_NOSIGNAL);
	} else if (s->role == SILENT_SERVER) {
		poll(&p, 0, 1500);
	} else {
		mbedtls_ssl_config conf;
		mbedtls_ssl_context ssl;
		mbedtls_ssl_config_init(&conf);
		mbedtls_ssl_init(&ssl);
		if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
						MBEDTLS_SSL_PRESET_DEFAULT) == 0 &&
		    mbedtls_ssl_conf_own_cert(&conf, s->crt, s->key) == 0) {
			mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, s->drbg);
			if (mbedtls_ssl_setup(&ssl, &conf) == 0) {
				mbedtls_ssl_set_bio(&ssl, &fd, net_send, net_recv, NULL);
				if (mbedtls_ssl_handshake(&ssl) == 0) {
					static const char ok[] = "HTTP/1.1 200 OK\r\nContent-Length: 39\r\n\r\n"
								 "<html><title>Example NAS</title></html>";
					int n = mbedtls_ssl_read(&ssl, (unsigned char *)s->request, sizeof(s->request) - 1);
					if (n > 0)
						s->request[n] = '\0';
					mbedtls_ssl_write(&ssl, (const unsigned char *)ok, sizeof(ok) - 1);
					mbedtls_ssl_close_notify(&ssl);
				}
			}
		}
		mbedtls_ssl_free(&ssl);
		mbedtls_ssl_config_free(&conf);
	}
	close(fd);
	return NULL;
}

/* A self-signed certificate for "nas.example" with an EC key. */
static int make_certificate(mbedtls_x509_crt *crt, mbedtls_pk_context *key, mbedtls_ctr_drbg_context *drbg)
{
	mbedtls_x509write_cert w;
	unsigned char der[2048];
	static const unsigned char serial[] = { 1 };
	unsigned char address[4] = { 192, 0, 2, 10 };
	mbedtls_x509_san_list ip = { .node = { .type = MBEDTLS_X509_SAN_IP_ADDRESS,
					       .san.unstructured_name = { .p = address, .len = 4 } }, .next = NULL };
	mbedtls_x509_san_list dns = { .node = { .type = MBEDTLS_X509_SAN_DNS_NAME,
						.san.unstructured_name = { .p = (unsigned char *)"nas.example", .len = 11 } },
				      .next = &ip };
	int r = mbedtls_pk_setup(key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
	if (r == 0)
		r = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(*key), mbedtls_ctr_drbg_random, drbg);
	if (r != 0)
		return r;
	mbedtls_x509write_crt_init(&w);
	mbedtls_x509write_crt_set_subject_key(&w, key);
	mbedtls_x509write_crt_set_issuer_key(&w, key);
	mbedtls_x509write_crt_set_md_alg(&w, MBEDTLS_MD_SHA256);
	r = mbedtls_x509write_crt_set_subject_name(&w, "CN=nas.example,O=Example");
	if (r == 0)
		r = mbedtls_x509write_crt_set_issuer_name(&w, "CN=nas.example,O=Example");
	if (r == 0)
		r = mbedtls_x509write_crt_set_serial_raw(&w, (unsigned char *)serial, sizeof(serial));
	if (r == 0)
		r = mbedtls_x509write_crt_set_validity(&w, "20260101000000", "20360101000000");
	if (r == 0)
		r = mbedtls_x509write_crt_set_subject_alternative_name(&w, &dns);
	int n = r == 0 ? mbedtls_x509write_crt_der(&w, der, sizeof(der), mbedtls_ctr_drbg_random, drbg) : r;
	mbedtls_x509write_crt_free(&w);
	if (n <= 0)
		return n ? n : -1;
	/* the DER is written at the end of the buffer */
	return mbedtls_x509_crt_parse_der(crt, der + sizeof(der) - n, (size_t)n);
}

static void run_server(enum tls_role role, struct tls_server *s, pthread_t *thread, uint16_t *port)
{
	s->role = role;
	s->listen_fd = listen_local(port);
	CHECK(s->listen_fd >= 0 && pthread_create(thread, NULL, serve_tls, s) == 0);
}

void test_tls(void)
{
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context drbg;
	mbedtls_x509_crt crt;
	mbedtls_pk_context key;
	struct tls_server s;
	pthread_t thread;
	uint16_t port;
	enum mnm_tls_result result;

	CHECK(psa_crypto_init() == PSA_SUCCESS);
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&drbg);
	mbedtls_x509_crt_init(&crt);
	mbedtls_pk_init(&key);
	CHECK(mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, NULL, 0) == 0);
	CHECK(make_certificate(&crt, &key, &drbg) == 0);

	/* a TLS server: handshake, certificate, a request and its answer */
	memset(&s, 0, sizeof(s));
	s.crt = &crt, s.key = &key, s.drbg = &drbg;
	run_server(TLS_SERVER, &s, &thread, &port);
	struct mnm_tls *t = mnm_tls_open(0x7f000001u, port, "nas.example", 3000, &result);
	CHECK(t != NULL && result == MNM_TLS_OK);
	if (t) {
		struct mnm_cert c;
		char answer[256] = "";
		CHECK(strncmp(mnm_tls_version(t), "TLSv1.", 6) == 0);
		CHECK(mnm_tls_certificate(t, &c) == 1);
		CHECK(strcmp(c.subject, "nas.example") == 0 && strcmp(c.issuer, "nas.example") == 0 && c.self_signed);
		CHECK(strcmp(c.not_before, "2026-01-01 00:00") == 0 && strcmp(c.not_after, "2036-01-01 00:00") == 0);
		/* in the order of the certificate; the writer of mbedTLS stores the list backwards */
		CHECK(strcmp(c.names, "192.0.2.10, nas.example") == 0);
		CHECK(strcmp(c.key, "EC 256") == 0 && strlen(c.sha256) == 64);
		CHECK(mnm_tls_write(t, "GET / HTTP/1.1\r\nHost: nas.example\r\n\r\n", 37, 3000) == 0);
		ssize_t n = mnm_tls_read(t, answer, sizeof(answer) - 1, 3000);
		if (n > 0)
			answer[n] = '\0';
		CHECK(n > 0 && strstr(answer, "<title>Example NAS</title>"));
		mnm_tls_close(t);
	}
	pthread_join(thread, NULL);
	close(s.listen_fd);
	CHECK(strncmp(s.request, "GET / HTTP/1.1\r\n", 16) == 0);

	/* the web probe over TLS against the same server: recognised, with the TLS version */
	char d[MNM_DETAIL_LEN];
	memset(s.request, 0, sizeof(s.request));
	run_server(TLS_SERVER, &s, &thread, &port);
	CHECK(mnm_probe_find("webservices_tls")->run(0x7f000001u, port, 500, d, sizeof(d)) == 1);
	CHECK(strncmp(d, "TLSv1.", 6) == 0);
	pthread_join(thread, NULL);
	close(s.listen_fd);
	CHECK(strcmp(s.request, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n") == 0);

	/* a server that answers, but without TLS */
	run_server(PLAIN_SERVER, &s, &thread, &port);
	CHECK(mnm_tls_open(0x7f000001u, port, NULL, 3000, &result) == NULL && result == MNM_TLS_NOT_TLS);
	pthread_join(thread, NULL);
	close(s.listen_fd);

	/* a server that stays silent */
	run_server(SILENT_SERVER, &s, &thread, &port);
	CHECK(mnm_tls_open(0x7f000001u, port, NULL, 500, &result) == NULL && result == MNM_TLS_TIMEOUT);
	pthread_join(thread, NULL);
	close(s.listen_fd);

	mbedtls_pk_free(&key);
	mbedtls_x509_crt_free(&crt);
	mbedtls_ctr_drbg_free(&drbg);
	mbedtls_entropy_free(&entropy);
}
