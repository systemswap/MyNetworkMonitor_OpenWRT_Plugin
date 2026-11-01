/* SPDX-License-Identifier: GPL-2.0-only */
#include "dnsname.h"

#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "netio.h"
#include "targets.h"

size_t mnm_ptr_query(uint32_t address, uint16_t id, uint8_t *buf, size_t cap)
{
	char name[40];
	size_t n = 12;

	if (cap < 64)
		return 0;
	memset(buf, 0, 12);
	buf[0] = (uint8_t)(id >> 8);
	buf[1] = (uint8_t)id;
	buf[2] = 0x01;          /* recursion desired */
	buf[5] = 1;             /* one question */
	snprintf(name, sizeof(name), "%u.%u.%u.%u.in-addr.arpa", address & 0xffu, (address >> 8) & 0xffu,
		 (address >> 16) & 0xffu, (address >> 24) & 0xffu);
	for (char *label = name; *label;) {
		char *dot = strchr(label, '.');
		size_t l = dot ? (size_t)(dot - label) : strlen(label);
		buf[n++] = (uint8_t)l;
		memcpy(buf + n, label, l);
		n += l;
		label += l + (dot ? 1 : 0);
	}
	buf[n++] = 0;
	buf[n++] = 0;
	buf[n++] = 12;          /* type PTR */
	buf[n++] = 0;
	buf[n++] = 1;           /* class IN */
	return n;
}

/* Skips a name at off; returns the offset after it, or 0 if damaged. */
static size_t skip_name(const uint8_t *msg, size_t len, size_t off)
{
	for (int labels = 0; labels < 128 && off < len; labels++) {
		uint8_t l = msg[off];
		if (l == 0)
			return off + 1;
		if ((l & 0xc0) == 0xc0)
			return off + 2 <= len ? off + 2 : 0;
		if (l & 0xc0)
			return 0;
		off += 1u + l;
	}
	return 0;
}

/* Reads a name at off into out, following compression pointers. */
static int read_name(const uint8_t *msg, size_t len, size_t off, char *out, size_t outlen)
{
	size_t n = 0;
	for (int jumps = 0; jumps < 16 && off < len;) {
		uint8_t l = msg[off];
		if (l == 0) {
			if (n > 0 && out[n - 1] == '.')
				n--;
			out[n] = '\0';
			return n > 0;
		}
		if ((l & 0xc0) == 0xc0) {
			if (off + 1 >= len)
				return 0;
			off = (size_t)(l & 0x3f) << 8 | msg[off + 1];
			jumps++;
			continue;
		}
		if ((l & 0xc0) || off + 1 + l > len)
			return 0;
		for (size_t i = 0; i < l; i++) {
			uint8_t c = msg[off + 1 + i];
			if (n + 2 >= outlen)
				return 0;
			/* only characters that belong in a host name; anything else is dropped */
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
				out[n++] = (char)c;
		}
		out[n++] = '.';
		off += 1u + l;
	}
	return 0;
}

int mnm_ptr_answer(const uint8_t *msg, size_t len, uint16_t id, char *name, size_t namelen)
{
	if (len < 12 || msg[0] != (uint8_t)(id >> 8) || msg[1] != (uint8_t)id)
		return 0;
	if (!(msg[2] & 0x80) || (msg[3] & 0x0f) != 0)   /* a response, RCODE 0 */
		return 0;
	unsigned int qd = (unsigned int)msg[4] << 8 | msg[5], an = (unsigned int)msg[6] << 8 | msg[7];
	size_t off = 12;
	for (unsigned int i = 0; i < qd; i++) {
		off = skip_name(msg, len, off);
		if (!off || off + 4 > len)
			return 0;
		off += 4;
	}
	for (unsigned int i = 0; i < an; i++) {
		off = skip_name(msg, len, off);
		if (!off || off + 10 > len)
			return 0;
		unsigned int type = (unsigned int)msg[off] << 8 | msg[off + 1];
		size_t rdlen = (size_t)msg[off + 8] << 8 | msg[off + 9];
		off += 10;
		if (off + rdlen > len)
			return 0;
		if (type == 12)
			return read_name(msg, len, off, name, namelen);
		off += rdlen;
	}
	return 0;
}

uint32_t mnm_resolver(const char *resolv_conf)
{
	FILE *f = fopen(resolv_conf, "r");
	char line[256];
	uint32_t found = 0x7f000001u;

	if (!f)
		return found;
	while (fgets(line, sizeof(line), f)) {
		char addr[64];
		uint32_t v;
		if (sscanf(line, " nameserver %63s", addr) == 1 && mnm_parse_ipv4(addr, strlen(addr), &v) == 0) {
			found = v;
			break;
		}
	}
	fclose(f);
	return found;
}

uint32_t mnm_default_gateway(const char *proc_route)
{
	FILE *f = fopen(proc_route, "r");
	char line[256];
	uint32_t found = 0;

	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		char iface[32];
		unsigned int destination, gateway, flags;
		/* The kernel prints the addresses as they lie in memory, i.e. in network
		 * byte order read as a native number: ntohl gives the address on every CPU. */
		if (sscanf(line, "%31s %x %x %x", iface, &destination, &gateway, &flags) == 4 && destination == 0 &&
		    (flags & 0x3) == 0x3 && gateway != 0) {   /* RTF_UP | RTF_GATEWAY */
			found = ntohl(gateway);
			break;
		}
	}
	fclose(f);
	return found;
}

static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void take_answers(int fd, struct mnm_devices *devices, uint16_t base)
{
	uint8_t msg[512];
	char name[MNM_NAME_LEN];
	for (;;) {
		ssize_t n = recv(fd, msg, sizeof(msg), MSG_DONTWAIT);
		if (n < 12)
			return;
		uint16_t id = (uint16_t)(msg[0] << 8 | msg[1]);
		size_t index = (uint16_t)(id - base);
		if (index >= devices->count)
			continue;
		if (mnm_ptr_answer(msg, (size_t)n, id, name, sizeof(name)) && !devices->list[index].hostname[0]) {
			snprintf(devices->list[index].hostname, sizeof(devices->list[index].hostname), "%s", name);
			devices->changed = 1;
		}
	}
}

void mnm_discover_dns_names(struct mnm_devices *devices, uint32_t resolver, int timeout_ms,
			    volatile sig_atomic_t *stop)
{
	int fd = mnm_udp_socket(resolver, 53);
	uint8_t query[64];
	/* The ID carries the position in the device list, from a random start, so
	 * that answers to an earlier scan cannot be mistaken for this one. */
	uint16_t base = (uint16_t)((unsigned int)time(NULL) * 2654435761u >> 16);

	if (fd < 0)
		return;
	int asked = 0;
	for (size_t i = 0; i < devices->count && i < 65535; i++) {
		if (devices->list[i].hostname[0])
			continue;
		size_t n = mnm_ptr_query(devices->list[i].ipv4, (uint16_t)(base + i), query, sizeof(query));
		if (n && send(fd, query, n, 0) == (ssize_t)n)
			asked = 1;
		take_answers(fd, devices, base);
		usleep(1000);   /* a thousand per second: gentle with small name servers */
	}
	int64_t end = now_ms() + timeout_ms;
	while (asked && !(stop && *stop)) {
		int64_t left = end - now_ms();
		if (left <= 0)
			break;
		struct pollfd p = { .fd = fd, .events = POLLIN };
		if (poll(&p, 1, (int)(left > 200 ? 200 : left)) > 0)
			take_answers(fd, devices, base);
	}
	close(fd);
}
