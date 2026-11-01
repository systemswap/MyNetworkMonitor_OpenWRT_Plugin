/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Names of devices by reverse DNS lookup (PTR records, RFC 1035): the router
 * or the home router before it usually knows the names of the devices it
 * handed addresses to. Building the query and reading the answer are pure
 * functions, tested on the build machine; the lookup sends all queries over
 * one UDP socket and matches the answers by their ID.
 */
#ifndef MNM_DNSNAME_H
#define MNM_DNSNAME_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#include "devices.h"

/* Builds a PTR query for "d.c.b.a.in-addr.arpa" of address (host byte order)
 * with the given ID and recursion desired. Returns the length (at most 64). */
size_t mnm_ptr_query(uint32_t address, uint16_t id, uint8_t *buf, size_t cap);

/*
 * Reads the answer to a PTR query: header with the ID and the response bit,
 * no error, then the first PTR record of the answer section. Names with
 * compression pointers are followed (RFC 1035, section 4.1.4), with a limit
 * against loops. Returns 1 and the name in dotted form (without the final
 * dot, printable characters only), else 0.
 */
int mnm_ptr_answer(const uint8_t *msg, size_t len, uint16_t id, char *name, size_t namelen);

/* The first IPv4 name server of a resolv.conf; 127.0.0.1 if none. */
uint32_t mnm_resolver(const char *resolv_conf);

/* The IPv4 default gateway from a route table in the format of /proc/net/route
 * (host byte order); 0 if there is none. */
uint32_t mnm_default_gateway(const char *proc_route);

/* Looks up the names of all devices without a host name and fills them in.
 * Waits at most timeout_ms after the last query. */
void mnm_discover_dns_names(struct mnm_devices *devices, uint32_t resolver, int timeout_ms,
			    volatile sig_atomic_t *stop);

#endif
