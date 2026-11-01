/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Web services over TLS and the identity of a web interface (title, server,
 * certificate), with the requests of the desktop program.
 */
#ifndef MNM_PROBES_WEB_H
#define MNM_PROBES_WEB_H

#include <stddef.h>
#include <stdint.h>

#include "probes.h"
#include "tlsconn.h"

/* The probes of this file, terminated by an entry with id NULL. */
extern const struct mnm_probe mnm_probes_web[];

/* The request of the web service probe over TLS: request line, Host (the
 * address) and "Connection: close". Returns its length, 0 if it does not fit. */
size_t mnm_web_https_request(uint32_t address, char *buf, size_t cap);

/* The request of the web page check: request line, Host (the address),
 * User-Agent, Accept and "Connection: close". */
size_t mnm_web_identity_request(uint32_t address, char *buf, size_t cap);

/* The detail of a web page: its title, the Server header and - with cert not
 * NULL - who the certificate is for, who issued it, until when it is valid
 * (with " - EXPIRED" if today, given as "YYYY-MM-DD", is past it), whether it is
 * self-signed and its further names. */
void mnm_web_identity_describe(const char *answer, size_t len, const struct mnm_cert *cert, const char *today,
			       char *out, size_t outlen);

#endif
