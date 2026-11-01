/* SPDX-License-Identifier: GPL-2.0-only */
#include "probes_udp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "discover.h"
#include "netio.h"
#include "probes_steps.h"

/* The packets, byte for byte those of the desktop program (taken from its
 * source by a script and compared with it before every change, see
 * docs/PRUEFUNGEN.md). */
static const uint8_t pkt_dns_query[48] = {
	0xaa, 0xaa, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x05, 0x70, 0x72, 0x6f, 0x62, 0x65, 0x10, 0x6d, 0x79, 0x6e, 0x65, 0x74,
	0x77, 0x6f, 0x72, 0x6b, 0x6d, 0x6f, 0x6e, 0x69, 0x74, 0x6f, 0x72, 0x07,
	0x69, 0x6e, 0x76, 0x61, 0x6c, 0x69, 0x64, 0x00, 0x00, 0x01, 0x00, 0x01,
};
static const uint8_t pkt_bacnet_read_object_name[17] = {
	0x81, 0x0a, 0x00, 0x11, 0x01, 0x04, 0x00, 0x05, 0x01, 0x0c, 0x0c, 0x02,
	0x3f, 0xff, 0xff, 0x19, 0x4d,
};
static const uint8_t pkt_bacnet_read_property_template[17] = {
	0x81, 0x0a, 0x00, 0x11, 0x01, 0x04, 0x00, 0x05, 0x01, 0x0c, 0x0c, 0x02,
	0x3f, 0xff, 0xff, 0x19, 0x00,
};
static const uint8_t pkt_dhcp_discover[310] = {
	0x01, 0x01, 0x06, 0x00, 0x60, 0xe7, 0xc5, 0x78, 0x00, 0x00, 0x80, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xde, 0xad, 0xc0, 0xde, 0xca, 0xfe, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x82, 0x53, 0x63,
	0x35, 0x01, 0x01, 0x37, 0x40, 0xfc, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
	0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
	0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
	0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
	0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
	0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x43, 0x42, 0xff,
};
static const uint8_t pkt_mssql_browser_request[1] = {
	0x02,
};

/* Waits up to wait_ms for a datagram on a connected UDP socket. Returns its
 * length, -2 if none came, -1 if the receive failed (on a connected socket an
 * ICMP "port unreachable" of the target arrives as an error). */
static ssize_t receive(int fd, uint8_t *buf, size_t cap, int wait_ms)
{
	if (wait_ms <= 0)
		return -2;
	ssize_t n = mnm_recv_once(fd, buf, cap, wait_ms);
	return n == 0 ? -1 : n;
}

/* ---------------------------------------------------------- DNS (UDP) */

/* The answer to the query: the same ID (0xAAAA) and the QR bit. Any answer
 * counts, also NXDOMAIN, because the name asked for never exists. */
static int identify_dns_udp(const uint8_t *r, size_t len)
{
	return len >= 3 && r[0] == pkt_dns_query[0] && r[1] == pkt_dns_query[1] && (r[2] & 0x80);
}

/* Up to three attempts, each waiting a second, 200 ms apart. A datagram that is
 * not the answer ends the probe (something else listens there). */
static int run_dns_udp(uint32_t address, uint16_t port, int timeout_ms, char *detail, size_t detail_len)
{
	uint8_t answer[512];
	int fd = mnm_udp_socket(address, port), found = 0;
	(void)timeout_ms;
	(void)detail;
	(void)detail_len;
	if (fd < 0)
		return 0;
	for (int attempt = 1; attempt <= 3; attempt++) {
		if (send(fd, pkt_dns_query, sizeof(pkt_dns_query), 0) == (ssize_t)sizeof(pkt_dns_query)) {
			ssize_t n = receive(fd, answer, sizeof(answer), 1000);
			if (n > 0) {
				found = identify_dns_udp(answer, (size_t)n);
				break;
			}
		}
		if (attempt < 3)
			usleep(200 * 1000);
	}
	close(fd);
	return found;
}

/* ------------------------------------------------------------- BACnet */

/* BACnet/IP (ASHRAE 135, Annex J): BVLC type 0x81 and a total length that
 * fits the datagram. */
static int identify_bacnet(const uint8_t *r, size_t len)
{
	if (len < 4 || r[0] != 0x81)
		return 0;
	size_t reported = (size_t)r[2] << 8 | r[3];
	return reported >= 4 && reported <= len;
}

static const struct {
	uint8_t id;
	const char *name;
} bacnet_properties[MNM_BACNET_PROPERTIES] = {
	{ 0x4b, "ObjectID" }, { 0x78, "VendorID" }, { 0x79, "Vendor Name" }, { 0x2c, "Firmware Revision" },
	{ 0x0c, "Application Software" }, { 0x4d, "Object Name" }, { 0x46, "Model Name" }, { 0x1c, "Description" },
	{ 0x3a, "Location" },
};

/* Whether a BACnet answer carries a value: after BVLC (4 bytes) and the NPDU
 * (version, control, optional destination and source, hop count; ASHRAE 135,
 * 6.2) the APDU must be a ComplexACK (type 3, 20.1.5). A Reject or an Error
 * has no property value, whatever its bytes look like. */
static int bacnet_is_ack(const uint8_t *d, size_t len)
{
	size_t at = 6;
	if (len < 7 || d[0] != 0x81)
		return 0;
	uint8_t control = d[5];
	if (control & 0x20) {             /* destination: network, length, address */
		if (at + 3 > len)
			return 0;
		at += 3u + d[at + 2];
	}
	if (control & 0x08) {             /* source: network, length, address */
		if (at + 3 > len)
			return 0;
		at += 3u + d[at + 2];
	}
	if (control & 0x20)               /* hop count */
		at++;
	return at < len && (d[at] >> 4) == 3;
}

void mnm_bacnet_describe(const uint8_t *const answers[MNM_BACNET_PROPERTIES], const size_t lens[MNM_BACNET_PROPERTIES],
			 char *out, size_t outlen)
{
	out[0] = '\0';
	for (size_t i = 0; i < MNM_BACNET_PROPERTIES; i++) {
		const uint8_t *d = bacnet_is_ack(answers[i], lens[i]) ? answers[i] : NULL;
		size_t len = d ? lens[i] : 0;
		char value[64] = "", part[96];
		if (i == 0 || i == 1) {
			/* ObjectID: the 22-bit instance in bytes 19-21; VendorID: the
			 * second-to-last byte. 0 (or too short) says nothing. */
			unsigned int id = 0;
			if (d && i == 0 && len >= 22)
				id = (unsigned int)(d[19] & 0x3f) << 16 | (unsigned int)d[20] << 8 | d[21];
			if (d && i == 1 && len >= 2)
				id = d[len - 2];
			if (id)
				snprintf(value, sizeof(value), "%u", id);
		} else if (d && len >= 20) {
			/* A character string: tag 0x75, length (including the character
			 * set byte), character set, text. */
			const uint8_t *tag = memchr(d, 0x75, len);
			size_t at = tag ? (size_t)(tag - d) : len;
			if (at + 2 < len) {
				size_t l = d[at + 1];
				if (l >= 1 && at + 2 + l <= len) {
					size_t n = 0;
					for (size_t k = 0; k + 1 < l && n + 1 < sizeof(value); k++) {
						uint8_t c = d[at + 3 + k];
						value[n++] = (c >= 0x20 && c <= 0x7e) ? (char)c : (c == 0 ? '\0' : '?');
						if (c == 0)
							n--;
					}
					value[n] = '\0';
					while (n > 0 && value[n - 1] == ' ')
						value[--n] = '\0';
				}
			}
		}
		const char *v = value;
		while (*v == ' ')
			v++;
		if (!*v)
			continue;
		snprintf(part, sizeof(part), "%s: %s", bacnet_properties[i].name, v);
		mnm_detail_add(out, outlen, part);
	}
}

/* ReadProperty Object_Name of "this device"; if the answer is BACnet, the nine
 * properties one after the other, each waiting up to two seconds. */
static int run_bacnet(uint32_t address, uint16_t port, int timeout_ms, char *detail, size_t detail_len)
{
	uint8_t first[1500], answers[MNM_BACNET_PROPERTIES][600], req[sizeof(pkt_bacnet_read_property_template)];
	const uint8_t *got[MNM_BACNET_PROPERTIES];
	size_t lens[MNM_BACNET_PROPERTIES];
	int fd = mnm_udp_socket(address, port);
	(void)timeout_ms;
	if (fd < 0)
		return 0;
	ssize_t n = -1;
	if (send(fd, pkt_bacnet_read_object_name, sizeof(pkt_bacnet_read_object_name), 0) > 0)
		n = receive(fd, first, sizeof(first), 2000);
	if (n <= 0 || !identify_bacnet(first, (size_t)n)) {
		close(fd);
		return 0;
	}
	for (size_t i = 0; i < MNM_BACNET_PROPERTIES; i++) {
		memcpy(req, pkt_bacnet_read_property_template, sizeof(req));
		req[sizeof(req) - 1] = bacnet_properties[i].id;
		got[i] = NULL;
		lens[i] = 0;
		if (send(fd, req, sizeof(req), 0) < 0)
			continue;
		n = receive(fd, answers[i], sizeof(answers[i]), 2000);
		if (n > 0) {
			got[i] = answers[i];
			lens[i] = (size_t)n;
		}
	}
	close(fd);
	mnm_bacnet_describe(got, lens, detail, detail_len);
	return 1;
}

/* -------------------------------------------------- SQL Server Browser */

/* Search for needle (with its case) in the len bytes at hay. */
static const uint8_t *find_text(const uint8_t *hay, size_t len, const char *needle)
{
	size_t n = strlen(needle);
	for (size_t i = 0; n && i + n <= len; i++)
		if (memcmp(hay + i, needle, n) == 0)
			return hay + i;
	return NULL;
}

/* The value after "key;" up to the next ';' in one block of the answer,
 * printable characters only, at most 40. */
static void browser_value(const uint8_t *block, size_t len, const char *key, char *out, size_t outlen)
{
	size_t at = 0, klen = strlen(key);
	out[0] = '\0';
	while (at < len) {
		size_t end = at;
		while (end < len && block[end] != ';')
			end++;
		if (end - at == klen && strncasecmp((const char *)block + at, key, klen) == 0 && end < len) {
			size_t v = end + 1, n = 0;
			while (v < len && block[v] != ';' && n < 40) {
				if (block[v] >= 0x20 && block[v] <= 0x7e && n + 1 < outlen)
					out[n++] = (char)block[v];
				v++;
			}
			out[n] = '\0';
			while (n > 0 && out[n - 1] == ' ')
				out[--n] = '\0';
			size_t s = 0;
			while (out[s] == ' ')
				s++;
			memmove(out, out + s, strlen(out + s) + 1);
			return;
		}
		at = end + 1;
	}
}

size_t mnm_mssql_browser(const uint8_t *r, size_t len, uint16_t *ports, size_t max, char *out, size_t outlen)
{
	size_t count = 0;
	char part[160];
	out[0] = '\0';
	/* every "tcp;<number>" of the answer */
	for (const uint8_t *p = r; (p = find_text(p, len - (size_t)(p - r), "tcp;")) != NULL; p += 4) {
		const uint8_t *q = p + 4;
		unsigned long port = 0;
		size_t digits = 0;
		while (q < r + len && *q >= '0' && *q <= '9' && digits < 6)
			port = port * 10 + (unsigned long)(*q++ - '0'), digits++;
		if (digits && port > 0 && port <= 65535 && count < max)
			ports[count++] = (uint16_t)port;
	}
	if (!count)
		return 0;
	/* one block per instance, each starting with "ServerName;" */
	for (const uint8_t *b = find_text(r, len, "ServerName;"); b;) {
		const uint8_t *next = find_text(b + 11, len - (size_t)(b + 11 - r), "ServerName;");
		size_t blen = (size_t)((next ? next : r + len) - b);
		char instance[48], version[48], clustered[8];
		browser_value(b, blen, "InstanceName", instance, sizeof(instance));
		browser_value(b, blen, "Version", version, sizeof(version));
		browser_value(b, blen, "IsClustered", clustered, sizeof(clustered));
		if (instance[0] || version[0]) {
			snprintf(part, sizeof(part), "Instance: %s%s%s%s%s", instance, instance[0] && version[0] ? ", " : "",
				 version[0] ? "version " : "", version, strcasecmp(clustered, "Yes") == 0 ? ", clustered" : "");
			mnm_detail_add(out, outlen, part);
		}
		b = next;
	}
	size_t used = 0;
	used += (size_t)snprintf(part, sizeof(part), "TCP port%s", count > 1 ? "s" : "");
	for (size_t i = 0; i < count && used < sizeof(part); i++)
		used += (size_t)snprintf(part + used, sizeof(part) - used, "%s%u", i ? ", " : " ", ports[i]);
	mnm_detail_add(out, outlen, part);
	return count;
}

/* Asked only where port 1433 did not answer as MS-SQL: named instances listen
 * on other ports, which the browser on UDP 1434 names. Up to three requests,
 * each waiting up to two seconds, within three seconds altogether. */
static int run_mssql_browser(uint32_t address, uint16_t port, int timeout_ms, char *detail, size_t detail_len)
{
	uint8_t answer[2048];
	uint16_t ports[16];
	int fd = mnm_udp_socket(address, port), found = 0;
	(void)timeout_ms;
	if (fd < 0)
		return 0;
	int64_t end = mnm_now_ms() + 3000;
	for (int attempt = 1; attempt <= 3 && !found; attempt++) {
		int64_t left = end - mnm_now_ms();
		if (left <= 0)
			break;
		if (send(fd, pkt_mssql_browser_request, sizeof(pkt_mssql_browser_request), 0) < 0)
			continue;
		ssize_t n = receive(fd, answer, sizeof(answer), (int)(left < 2000 ? left : 2000));
		if (n > 0 && mnm_mssql_browser(answer, (size_t)n, ports, 16, detail, detail_len) > 0)
			found = 1;
	}
	close(fd);
	return found;
}

/* --------------------------------------------------------------- DHCP */

int mnm_dhcp_server(const uint8_t *r, size_t len, uint32_t *server)
{
	/* a BOOTP reply (RFC 2131, section 2); the server identifier is option 54
	 * (RFC 2132, 9.7), read from the option list after the magic cookie;
	 * without it the relay (giaddr), then the next server (siaddr) */
	if (len < 28 || r[0] != 2)
		return 0;
	if (len >= 240 && r[236] == 0x63 && r[237] == 0x82 && r[238] == 0x53 && r[239] == 0x63) {
		for (size_t at = 240; at < len && r[at] != 0xff;) {
			if (r[at] == 0) {
				at++;
				continue;
			}
			if (at + 1 >= len || at + 2 + r[at + 1] > len)
				break;
			if (r[at] == 54 && r[at + 1] == 4) {
				*server = (uint32_t)r[at + 2] << 24 | (uint32_t)r[at + 3] << 16 | (uint32_t)r[at + 4] << 8 | r[at + 5];
				return *server != 0;
			}
			at += 2u + r[at + 1];
		}
	}
	uint32_t giaddr = (uint32_t)r[24] << 24 | (uint32_t)r[25] << 16 | (uint32_t)r[26] << 8 | r[27];
	uint32_t siaddr = (uint32_t)r[16] << 24 | (uint32_t)r[17] << 16 | (uint32_t)r[18] << 8 | r[19];
	*server = giaddr ? giaddr : siaddr;
	return *server != 0;
}

/* One DHCPDISCOVER per local network of the targets, from port 68 to the
 * broadcast address, then two seconds for the offers. The socket is bound to
 * the interface and to any address: Linux hands broadcast answers only to
 * sockets not bound to a single address. */
static size_t ask_dhcp(const struct mnm_targets *targets, uint32_t *found, size_t max)
{
	struct mnm_local_net nets[16];
	struct pollfd pf[16];
	size_t count = mnm_local_networks_of(targets, nets, 16), open = 0, n = 0;
	struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(67), .sin_addr.s_addr = htonl(INADDR_BROADCAST) };
	struct sockaddr_in from = { .sin_family = AF_INET, .sin_port = htons(68), .sin_addr.s_addr = htonl(INADDR_ANY) };

	for (size_t i = 0; i < count; i++) {
		int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0), on = 1;
		if (fd < 0)
			continue;
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0 ||
		    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) != 0 ||
		    setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, nets[i].name, (socklen_t)strlen(nets[i].name)) != 0 ||
		    bind(fd, (struct sockaddr *)&from, sizeof(from)) != 0 ||
		    sendto(fd, pkt_dhcp_discover, sizeof(pkt_dhcp_discover), 0, (struct sockaddr *)&to, sizeof(to)) < 0) {
			fprintf(stderr, "dhcp: %s: %s\n", nets[i].name, strerror(errno));
			close(fd);
			continue;
		}
		pf[open++] = (struct pollfd){ .fd = fd, .events = POLLIN };
	}
	int64_t end = mnm_now_ms() + 2000;
	while (open > 0) {
		int64_t left = end - mnm_now_ms();
		if (left <= 0 || poll(pf, (nfds_t)open, (int)left) <= 0)
			break;
		for (size_t i = 0; i < open; i++) {
			uint8_t buf[1024];
			uint32_t server;
			if (!(pf[i].revents & POLLIN))
				continue;
			ssize_t len = recv(pf[i].fd, buf, sizeof(buf), MSG_DONTWAIT);
			if (len <= 0 || !mnm_dhcp_server(buf, (size_t)len, &server))
				continue;
			int known = 0;
			for (size_t k = 0; k < n; k++)
				known |= found[k] == server;
			if (!known && n < max)
				found[n++] = server;
		}
	}
	for (size_t i = 0; i < open; i++)
		close(pf[i].fd);
	return n;
}

/* -------------------------------------------------------------- table */

static int identify_any(const uint8_t *r, size_t len)
{
	(void)r;
	return len > 0;
}

const struct mnm_probe mnm_probes_udp[] = {
	{ "dns_udp", "DNS", pkt_dns_query, sizeof(pkt_dns_query), NULL, identify_dns_udp, NULL, run_dns_udp,
	  MNM_UDP, NULL, NULL },
	{ "bacnet", "BACnet", pkt_bacnet_read_object_name, sizeof(pkt_bacnet_read_object_name), NULL, identify_bacnet,
	  NULL, run_bacnet, MNM_UDP, NULL, NULL },
	{ "mssqlbrowser", "MS-SQL", pkt_mssql_browser_request, sizeof(pkt_mssql_browser_request), NULL, identify_any,
	  NULL, run_mssql_browser, MNM_UDP, "mssqlserver", NULL },
	{ "dhcp", "DHCP", pkt_dhcp_discover, sizeof(pkt_dhcp_discover), NULL, identify_any, NULL, NULL,
	  MNM_BROADCAST, NULL, ask_dhcp },
	{ NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, MNM_TCP, NULL, NULL },
};
