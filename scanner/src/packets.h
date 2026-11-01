/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ARP and ICMP packets: building requests and reading answers. Pure functions
 * on byte buffers, without sockets, tested on the build machine. Every length
 * and field is checked against the data before it is read.
 */
#ifndef MNM_PACKETS_H
#define MNM_PACKETS_H

#include <stddef.h>
#include <stdint.h>

#define MNM_ARP_FRAME_LEN 42   /* Ethernet header (14) + ARP for IPv4 over Ethernet (28) */
#define MNM_ICMP_ECHO_LEN 16   /* ICMP header (8) + 8 bytes payload */

/*
 * Builds a broadcast ARP request "who has target? tell sender" as a complete
 * Ethernet frame (RFC 826; Ethernet type 0x0806). Addresses in host byte order.
 */
void mnm_arp_build_request(uint8_t frame[MNM_ARP_FRAME_LEN], const uint8_t sender_mac[6], uint32_t sender_ip,
			   uint32_t target_ip);

/*
 * Reads an ARP reply from a received frame. Returns 1 and fills ip and mac of
 * the answering device if the frame is an ARP reply for IPv4 over Ethernet,
 * 0 otherwise.
 */
int mnm_arp_parse_reply(const uint8_t *frame, size_t len, uint32_t *ip, uint8_t mac[6]);

/* Builds an ICMP echo request (RFC 792) with identifier and sequence number and
 * fills in the checksum. */
void mnm_icmp_build_echo(uint8_t packet[MNM_ICMP_ECHO_LEN], uint16_t identifier, uint16_t sequence);

/*
 * Reads an echo reply from what a raw ICMP socket delivers: the IPv4 header
 * followed by the ICMP message. Returns 1 if it is an echo reply carrying the
 * given identifier, with the sender in ip and the time-to-live of the packet
 * in ttl; 0 otherwise.
 */
int mnm_icmp_parse_reply(const uint8_t *data, size_t len, uint16_t identifier, uint32_t *ip, uint8_t *ttl);

/* The Internet checksum (RFC 1071) over len bytes. */
uint16_t mnm_inet_checksum(const uint8_t *data, size_t len);

/*
 * A rough guess of the operating system family from the TTL of an answer:
 * systems start at 64 (Linux, macOS, Android, most embedded devices), 128
 * (Windows) or 255 (many routers, switches and printers), and every router on
 * the way subtracts one. Returns a short English text, or NULL for TTL 0.
 */
const char *mnm_ttl_guess(uint8_t ttl);

#endif
