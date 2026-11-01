/* SPDX-License-Identifier: GPL-2.0-only */
#include "packets.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static uint16_t get16(const uint8_t *p)
{
	return (uint16_t)(p[0] << 8 | p[1]);
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

void mnm_arp_build_request(uint8_t frame[MNM_ARP_FRAME_LEN], const uint8_t sender_mac[6], uint32_t sender_ip,
			   uint32_t target_ip)
{
	memset(frame, 0xff, 6);              /* destination: broadcast */
	memcpy(frame + 6, sender_mac, 6);
	put16(frame + 12, 0x0806);           /* EtherType ARP */
	put16(frame + 14, 1);                /* hardware type Ethernet */
	put16(frame + 16, 0x0800);           /* protocol IPv4 */
	frame[18] = 6;                       /* hardware address length */
	frame[19] = 4;                       /* protocol address length */
	put16(frame + 20, 1);                /* operation: request */
	memcpy(frame + 22, sender_mac, 6);
	put32(frame + 28, sender_ip);
	memset(frame + 32, 0, 6);            /* target hardware address: unknown */
	put32(frame + 38, target_ip);
}

int mnm_arp_parse_reply(const uint8_t *frame, size_t len, uint32_t *ip, uint8_t mac[6])
{
	if (len < MNM_ARP_FRAME_LEN)
		return 0;
	if (get16(frame + 12) != 0x0806 || get16(frame + 14) != 1 || get16(frame + 16) != 0x0800)
		return 0;
	if (frame[18] != 6 || frame[19] != 4 || get16(frame + 20) != 2)
		return 0;
	/* A sender address of all zeros or broadcast is no device (probes, gratuitous junk). */
	static const uint8_t zero[6], all[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	if (memcmp(frame + 22, zero, 6) == 0 || memcmp(frame + 22, all, 6) == 0)
		return 0;
	memcpy(mac, frame + 22, 6);
	*ip = get32(frame + 28);
	return *ip != 0;
}

uint16_t mnm_inet_checksum(const uint8_t *data, size_t len)
{
	uint32_t sum = 0;

	for (size_t i = 0; i + 1 < len; i += 2)
		sum += get16(data + i);
	if (len & 1)
		sum += (uint32_t)data[len - 1] << 8;
	while (sum >> 16)
		sum = (sum & 0xffffu) + (sum >> 16);
	return (uint16_t)~sum;
}

void mnm_icmp_build_echo(uint8_t packet[MNM_ICMP_ECHO_LEN], uint16_t identifier, uint16_t sequence)
{
	memset(packet, 0, MNM_ICMP_ECHO_LEN);
	packet[0] = 8;                       /* type: echo request */
	packet[1] = 0;                       /* code */
	put16(packet + 4, identifier);
	put16(packet + 6, sequence);
	memcpy(packet + 8, "MNMscan", 8);    /* payload, including the terminating zero */
	put16(packet + 2, mnm_inet_checksum(packet, MNM_ICMP_ECHO_LEN));
}

int mnm_icmp_parse_reply(const uint8_t *data, size_t len, uint16_t identifier, uint32_t *ip, uint8_t *ttl)
{
	if (len < 20 || (data[0] >> 4) != 4)
		return 0;
	size_t header = (size_t)(data[0] & 0x0f) * 4;
	if (header < 20 || len < header + 8 || data[9] != 1)   /* protocol 1: ICMP */
		return 0;
	const uint8_t *icmp = data + header;
	if (icmp[0] != 0 || icmp[1] != 0)                      /* echo reply, code 0 */
		return 0;
	if (get16(icmp + 4) != identifier)
		return 0;
	*ip = get32(data + 12);
	*ttl = data[8];
	return 1;
}

const char *mnm_ttl_guess(uint8_t ttl)
{
	if (ttl == 0)
		return NULL;
	if (ttl <= 64)
		return "Linux, macOS or embedded";
	if (ttl <= 128)
		return "Windows";
	return "Network device";
}
