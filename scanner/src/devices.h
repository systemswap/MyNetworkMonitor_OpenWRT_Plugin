/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The devices found in one scan, keyed by their IPv4 address, and their
 * output as JSON. The scanner writes this list again and again while it runs,
 * so that the page shows every device as soon as it is found; merging with the
 * devices of earlier scans is done by the LuCI backend.
 */
#ifndef MNM_DEVICES_H
#define MNM_DEVICES_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <netinet/in.h>

#define MNM_MAX_IPV6 8
#define MNM_MAX_SERVICES 16
#define MNM_NAME_LEN 64
#define MNM_VENDOR_LEN 96

/* How the device was noticed, as bits. */
enum {
	MNM_SEEN_ARP = 1,        /* answered an ARP request */
	MNM_SEEN_PING = 2,       /* answered a ping */
	MNM_SEEN_NEIGHBOR = 4,   /* in the neighbour table of the router */
	MNM_SEEN_LEASE = 8,      /* has a DHCP lease of the router */
	MNM_SEEN_TCP = 16,       /* answered on a TCP port */
};

/* A service recognised by its answer, not only by an open port. */
struct mnm_service {
	const char *id;          /* probe id, a static string of the probe table */
	const char *name;        /* its display name, also static */
	uint16_t port;
	char detail[96];
};

struct mnm_device {
	uint32_t ipv4;
	uint8_t mac[6];
	int has_mac;
	struct in6_addr ipv6[MNM_MAX_IPV6];
	int ipv6_count;
	char hostname[MNM_NAME_LEN];
	char vendor[MNM_VENDOR_LEN];
	uint8_t ttl;
	unsigned int seen;       /* MNM_SEEN_* */
	int ports_scanned;       /* the TCP ports of this device were tried in this scan */
	uint16_t *tcp;
	size_t tcp_count, tcp_capacity;
	struct mnm_service services[MNM_MAX_SERVICES];
	int service_count;
	int services_checked;    /* the probes ran on this device in this scan */
};

struct mnm_devices {
	struct mnm_device *list;
	size_t count, capacity;
	int changed;   /* set by every change, cleared when written */
};

/* The device with this IPv4 address, created if missing; NULL when out of memory. */
struct mnm_device *mnm_devices_get(struct mnm_devices *devices, uint32_t ipv4);

/* The device with this address if it exists, else NULL. */
struct mnm_device *mnm_devices_find(struct mnm_devices *devices, uint32_t ipv4);

void mnm_devices_set_mac(struct mnm_devices *devices, struct mnm_device *device, const uint8_t mac[6]);
void mnm_devices_add_ipv6(struct mnm_devices *devices, const uint8_t mac[6], const struct in6_addr *address);
void mnm_devices_add_tcp(struct mnm_devices *devices, struct mnm_device *device, uint16_t port);
/* Records a recognised service (once per service and port). */
void mnm_devices_add_service(struct mnm_devices *devices, struct mnm_device *device, const char *id, const char *name,
			     uint16_t port, const char *detail);
void mnm_devices_mark(struct mnm_devices *devices, struct mnm_device *device, unsigned int seen);

/* Writes the devices as a JSON array, ordered by address (the list itself
 * keeps its order). Pointers into the list stay valid until the next
 * mnm_devices_get, which may move the list when it grows. */
void mnm_devices_write_json(struct mnm_devices *devices, FILE *out);

void mnm_devices_free(struct mnm_devices *devices);

#endif
