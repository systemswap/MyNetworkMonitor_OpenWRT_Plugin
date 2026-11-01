/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Service probes: which packet a service is greeted with and how its answer
 * is recognised. The packets were determined by trying them against real
 * services; they are not tidied up and change only for a protocol reason.
 * The checks are pure functions on the received bytes, tested on the build
 * machine.
 */
#ifndef MNM_PROBES_H
#define MNM_PROBES_H

#include <stddef.h>
#include <stdint.h>

/* Length of the detail a probe reports, including the terminating NUL; long
 * enough for the identification of a controller (order number, firmware, name). */
#define MNM_DETAIL_LEN 160

struct mnm_probe {
	const char *id;            /* catalog id, e.g. "ssh" */
	const char *name;          /* shown in the table, e.g. "SSH" */
	const uint8_t *hello;      /* sent after connecting; NULL when the service speaks first */
	size_t hello_len;
	/* Instead of hello: builds a request that depends on the target (e.g. the
	 * Host header of HTTP) into buf; returns its length. NULL for fixed packets. */
	size_t (*build)(uint32_t address, uint8_t *buf, size_t cap);
	/* Whether the answer belongs to this service. */
	int (*identify)(const uint8_t *answer, size_t len);
	/* Optional: what the answer tells about the device (software, version),
	 * written to detail; returns 1 if something was written. */
	int (*describe)(const uint8_t *answer, size_t len, char *detail, size_t detail_len);
	/* Instead of hello and identify: a procedure of its own (several steps, or
	 * UDP). Talks to address:port with each step limited to timeout_ms and
	 * returns 1 if the service was recognised, with an optional detail. Runs
	 * in a worker thread, so it may block within its time limits. */
	int (*run)(uint32_t address, uint16_t port, int timeout_ms, char *detail, size_t detail_len);
};

/* The probe with this catalog id, or NULL. */
const struct mnm_probe *mnm_probe_find(const char *id);

/* All probes, terminated by an entry with id NULL. */
extern const struct mnm_probe mnm_probes[];

/* Copies printable characters of an answer into out (for details). */
void mnm_printable(const uint8_t *in, size_t len, char *out, size_t outlen);

/* Whether needle occurs in the len bytes at hay, ignoring ASCII case; NUL bytes
 * in hay are searched through, unlike strstr. */
int mnm_contains_ci(const uint8_t *hay, size_t len, const char *needle);

#endif
