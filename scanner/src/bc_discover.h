/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Finding devices by the multicast searches of SSDP/UPnP, WS-Discovery, ONVIF
 * and mDNS in every local network of the targets, and their names by NetBIOS
 * node status. Packets, repetitions and waiting times are those of the
 * desktop program; answers count only from addresses inside the targets.
 */
#ifndef MNM_BC_DISCOVER_H
#define MNM_BC_DISCOVER_H

#include <signal.h>
#include <stddef.h>

#include "devices.h"
#include "targets.h"

enum {
	MNM_BC_SSDP = 1,
	MNM_BC_WSD = 2,
	MNM_BC_ONVIF = 4,
	MNM_BC_MDNS = 8,
	MNM_BC_NETBIOS = 16,
};

/* "ssdp,wsd,onvif,mdns,netbios" (any of them) as MNM_BC_* bits; -1 with a
 * message for an unknown name. */
int mnm_bc_parse_methods(const char *text, unsigned int *methods, char *err, size_t errlen);

struct mnm_bc_options {
	unsigned int methods;           /* MNM_BC_* */
	volatile sig_atomic_t *stop;
	void (*on_change)(void *context);
	void *context;
};

/* The multicast searches (about 6.5 s), then the UPnP descriptions of the
 * devices that answered SSDP. Devices that answer are added to the list and
 * get a service entry with what they told. Returns 1 if stopped. */
int mnm_bc_discover(const struct mnm_targets *targets, struct mnm_devices *devices, const struct mnm_bc_options *o);

/* NetBIOS node status to every device in the list (UDP 137, up to two
 * attempts of 5 s, 50 devices at a time). The name becomes the host name of a
 * device that has none yet. Returns 1 if stopped. */
int mnm_bc_netbios(struct mnm_devices *devices, const struct mnm_bc_options *o);

#endif
