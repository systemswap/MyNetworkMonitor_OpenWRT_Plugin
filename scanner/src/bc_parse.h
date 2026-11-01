/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Reading the answers of the discovery methods: SSDP headers and UPnP device
 * descriptions, WS-Discovery and ONVIF probe matches, mDNS responses and
 * NetBIOS node status. Pure functions on the received bytes, tested on the
 * build machine; nothing here touches the network.
 */
#ifndef MNM_BC_PARSE_H
#define MNM_BC_PARSE_H

#include <stddef.h>
#include <stdint.h>

/* The value of a header line "NAME: value" of an SSDP answer (name without
 * regard to case, at the start of a line), without spaces around it; 0 if
 * there is none. */
int mnm_ssdp_header(const char *answer, size_t len, const char *name, char *out, size_t outlen);

/* Splits an http:// URL into host, port (80 if none) and path ("/" if none).
 * Returns 0 for anything else (https, user info, a host name over 63 bytes). */
int mnm_url_split(const char *url, char *host, size_t hostlen, uint16_t *port, char *path, size_t pathlen);

/* The text content of the first element whose local name (namespace prefix
 * ignored, case ignored) is name: the text of all its descendants, entities
 * decoded, spaces around it removed. 0 if there is no such element or it is
 * empty. */
int mnm_xml_text(const char *xml, size_t len, const char *name, char *out, size_t outlen);

/* The detail of a UPnP device description: friendly name, manufacturer, model
 * name and number. 0 if none of them is there. */
int mnm_upnp_describe(const char *xml, size_t len, char *out, size_t outlen);

/* The detail of a WS-Discovery answer: what the types say (printer, scanner,
 * Windows computer, camera) and the types themselves; "Replied, but the
 * response could not be read." for an answer without them. */
void mnm_wsd_describe(const char *xml, size_t len, char *out, size_t outlen);

/* An ONVIF probe match: needs endpoint address, service addresses, scopes and
 * metadata version. The detail is the name from the scope ".../name/<name>" and
 * the device service address without "/onvif/device_service". Returns 0 for
 * anything else. */
int mnm_onvif_describe(const char *xml, size_t len, char *out, size_t outlen);

/* What an mDNS response tells about the device that sent it. */
struct mnm_mdns_info {
	char services[8][40];    /* service types from PTR records, e.g. "_http._tcp" */
	int service_count;
	char instance[64];       /* the first instance name, e.g. "Living Room" */
	char host[64];           /* host name of an address record for the sender, without ".local" */
};

/* Reads an mDNS response (RFC 6762: QR bit set; questions are skipped by their
 * count) of the device at source (host byte order) and adds to info. Returns 1
 * if it was a response with at least one record, else 0 - queries of other
 * devices are no answer. */
int mnm_mdns_parse(const uint8_t *r, size_t len, uint32_t source, struct mnm_mdns_info *info);

/* The detail of an mDNS device: its services and instance name. */
void mnm_mdns_describe(const struct mnm_mdns_info *info, char *out, size_t outlen);

/* Reads a NetBIOS node status response (RFC 1002, 4.2.18) to the request with
 * this transaction ID: the host name (unique name with suffix 0x20, else 0x00)
 * and the workgroup or domain (group name with suffix 0x00, else 0x1E).
 * Returns 1 if it was a valid answer with a host name. */
int mnm_netbios_parse(const uint8_t *r, size_t len, uint16_t id, char *host, size_t hostlen, char *workgroup,
		      size_t grouplen);

/* A random GUID in the form of .NET "D" (version 4, lowercase hex with dashes),
 * written without NUL into 36 bytes. */
void mnm_guid(uint8_t out[36]);

#endif
