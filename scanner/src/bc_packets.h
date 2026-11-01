/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The packets of SSDP, WS-Discovery, ONVIF discovery, NetBIOS node status and
 * mDNS, and where values of the run go into them.
 */
#ifndef MNM_BC_PACKETS_H
#define MNM_BC_PACKETS_H

#include <stddef.h>
#include <stdint.h>

struct mnm_packet {
	const uint8_t *data;
	size_t len;
};

extern const uint8_t pkt_ssdp_msearch[94];
extern const uint8_t pkt_wsd_probe_2009[587];
extern const uint8_t pkt_wsd_probe_2005[587];
extern const uint8_t pkt_onvif_probe[817];
extern const uint8_t pkt_netbios_nbstat[50];

/* Where the message ID (a GUID as 36 characters, "xxxxxxxx-xxxx-...") stands. */
#define MNM_WSD_2009_GUID 276
#define MNM_WSD_2005_GUID 282
#define MNM_ONVIF_GUID 383
#define MNM_GUID_LEN 36

/* The transaction ID of the NetBIOS request: bytes 0 and 1, big-endian. */
#define MNM_NETBIOS_ID 0

#define MNM_MDNS_QUERIES 27
extern const struct mnm_packet mnm_mdns_queries[MNM_MDNS_QUERIES];

#endif
