/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Reading the text files the router keeps about its devices: the DHCP leases
 * of dnsmasq and the MAC vendor list. Pure functions on single lines, tested
 * on the build machine; the files may contain anything, every field is
 * checked.
 */
#ifndef MNM_TEXTDATA_H
#define MNM_TEXTDATA_H

#include <stddef.h>
#include <stdint.h>

/* Parses a MAC address "aa:bb:cc:dd:ee:ff" (also with "-") from exactly len
 * characters. Returns 0 on success. */
int mnm_parse_mac(const char *text, size_t len, uint8_t mac[6]);

/* Writes "aa:bb:cc:dd:ee:ff" into buf (at least 18 bytes). */
void mnm_format_mac(const uint8_t mac[6], char *buf);

/*
 * One line of the dnsmasq lease file (/tmp/dhcp.leases):
 *   <expiry> <mac> <ipv4> <hostname or *> <client id or *>
 * Returns 1 and fills mac, ip and hostname (empty when "*") for an IPv4 lease,
 * 0 for anything else (comments, IPv6 lines, damaged lines).
 */
int mnm_lease_parse_line(const char *line, uint8_t mac[6], uint32_t *ip, char *hostname, size_t hostlen);

/*
 * One line of the vendor list in the format of the file "manuf" that the
 * Wireshark project generates from the IEEE registers:
 *   <prefix>[/<bits>] <tab> <short name> [<tab> <full name>]
 * The prefix is 3 bytes (MA-L, 24 bits) or a full address with /28 (MA-M) or
 * /36 (MA-S). Returns 1 and fills prefix (the leading bits of the address in
 * the upper bits of a 48-bit value), bits and name (the full name, else the
 * short one); 0 for comments and damaged lines.
 */
int mnm_vendor_parse_line(const char *line, uint64_t *prefix, unsigned int *bits, char *name, size_t namelen);

/* The 48-bit value of a MAC address, for comparing with a vendor prefix. */
uint64_t mnm_mac_value(const uint8_t mac[6]);

#endif
