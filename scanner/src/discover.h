/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Finding devices quickly, before any port is tried:
 *  - ARP requests in every network the router itself is part of; they find
 *    every device in that segment, including those that ignore ping;
 *  - ping (ICMP echo) to all targets, for devices behind another router;
 *  - the neighbour table of the router (IPv4 and IPv6, with MAC addresses);
 *  - the DHCP leases the router has handed out (host names);
 *  - the MAC vendor list.
 * ARP and ping need raw sockets and thus root, which the scanner has when the
 * LuCI backend starts it.
 */
#ifndef MNM_DISCOVER_H
#define MNM_DISCOVER_H

#include <signal.h>

#include "devices.h"
#include "targets.h"

struct mnm_discover_options {
	int timeout_ms;                  /* how long to wait for answers after the last request */
	int ping;                        /* also ping all targets (ARP always runs) */
	volatile sig_atomic_t *stop;
	void (*on_change)(void *context);   /* called when devices were added or changed */
	void *context;
};

/* ARP in the local networks, then (if options->ping) ping to all targets.
 * Returns 1 if stopped early, else 0; missing rights are not an error, the
 * method is then skipped (see the warnings written to stderr). */
int mnm_discover_arp_ping(const struct mnm_targets *targets, struct mnm_devices *devices,
			  const struct mnm_discover_options *options);

/* Adds MAC and IPv6 addresses from the neighbour table of the router (netlink),
 * for devices that are targets. */
void mnm_discover_neighbors(const struct mnm_targets *targets, struct mnm_devices *devices);

/* Adds host names from a dnsmasq lease file for the devices found. */
void mnm_discover_leases(const char *path, struct mnm_devices *devices);

/* Fills in the vendor of every device with a MAC address from a vendor list in
 * the "manuf" format (one pass over the file, longest prefix wins). */
void mnm_discover_vendors(const char *path, struct mnm_devices *devices);

/* Whether address lies in one of the target ranges. */
int mnm_targets_contain(const struct mnm_targets *targets, uint32_t address);

#endif
