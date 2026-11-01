/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Target list: IPv4 addresses given as single addresses, ranges or networks.
 * Pure functions without network access, tested on the build machine.
 */
#ifndef MNM_TARGETS_H
#define MNM_TARGETS_H

#include <stddef.h>
#include <stdint.h>

/* Upper bound of addresses in one scan. A /16 is the largest network a router
 * can sweep in reasonable time; anything above is almost always a typing
 * error that would otherwise run for days. */
#define MNM_MAX_TARGETS 65536u

/* One contiguous block of addresses, both ends included, in host byte order. */
struct mnm_range {
	uint32_t first;
	uint32_t last;
};

struct mnm_targets {
	struct mnm_range *ranges;
	size_t count;
	uint32_t total;  /* number of addresses over all ranges */
};

/*
 * Parses a comma-separated list. Accepted forms:
 *   192.0.2.7               one address
 *   192.0.2.1-254           range within the last octet
 *   192.0.2.1-192.0.2.254   range between two addresses
 *   192.0.2.0/24            network; without network and broadcast address for /0 to /30
 * Spaces around entries are ignored. Returns 0 on success; on error -1 and a
 * message in err (at most errlen bytes), and targets is left empty.
 */
int mnm_targets_parse(const char *text, struct mnm_targets *targets, char *err, size_t errlen);

/*
 * Whether every address of the list lies in a private or local range:
 * 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16 (RFC 1918), 169.254.0.0/16
 * (link-local, RFC 3927) and 127.0.0.0/8 (the router itself). The scanner
 * refuses other addresses unless asked otherwise, so that it only examines the
 * own networks and never hosts on the internet. On failure the first public
 * range is described in err.
 */
int mnm_targets_all_private(const struct mnm_targets *targets, char *err, size_t errlen);

/* The address at position index (0 <= index < total), in host byte order. */
uint32_t mnm_targets_at(const struct mnm_targets *targets, uint32_t index);

void mnm_targets_free(struct mnm_targets *targets);

/* Parses a dotted IPv4 address strictly: four decimal parts 0-255, no leading
 * zeros beyond a single "0", nothing after it. Returns 0 on success. */
int mnm_parse_ipv4(const char *text, size_t len, uint32_t *out);

/* Writes the address as dotted text into buf (at least 16 bytes). */
void mnm_format_ipv4(uint32_t address, char *buf);

#endif
