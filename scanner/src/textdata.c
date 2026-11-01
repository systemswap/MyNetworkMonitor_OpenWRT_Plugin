/* SPDX-License-Identifier: GPL-2.0-only */
#include "textdata.h"

#include <stdio.h>
#include <string.h>

#include "targets.h"

static int hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int mnm_parse_mac(const char *text, size_t len, uint8_t mac[6])
{
	if (len != 17)
		return -1;
	for (int i = 0; i < 6; i++) {
		int hi = hexval(text[i * 3]), lo = hexval(text[i * 3 + 1]);
		if (hi < 0 || lo < 0)
			return -1;
		if (i < 5 && text[i * 3 + 2] != ':' && text[i * 3 + 2] != '-')
			return -1;
		mac[i] = (uint8_t)(hi << 4 | lo);
	}
	return 0;
}

void mnm_format_mac(const uint8_t mac[6], char *buf)
{
	snprintf(buf, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

uint64_t mnm_mac_value(const uint8_t mac[6])
{
	uint64_t v = 0;
	for (int i = 0; i < 6; i++)
		v = v << 8 | mac[i];
	return v;
}

/* Splits off the next field separated by any of the characters in seps. */
static const char *field(const char *p, const char *seps, size_t *len)
{
	while (*p && strchr(seps, *p))
		p++;
	const char *start = p;
	while (*p && !strchr(seps, *p) && *p != '\n' && *p != '\r')
		p++;
	*len = (size_t)(p - start);
	return start;
}

/* Copies a name, keeping only printable ASCII and UTF-8 bytes: a name from a
 * device or a downloaded file ends up in JSON and on a web page. */
static void copy_name(char *out, size_t outlen, const char *in, size_t len)
{
	size_t n = 0;
	for (size_t i = 0; i < len && n + 1 < outlen; i++) {
		unsigned char c = (unsigned char)in[i];
		if (c >= 0x20 && c != 0x7f)
			out[n++] = (char)c;
	}
	out[n] = '\0';
}

int mnm_lease_parse_line(const char *line, uint8_t mac[6], uint32_t *ip, char *hostname, size_t hostlen)
{
	size_t len;
	const char *p = field(line, " \t", &len);   /* expiry */

	if (len == 0 || *p == '#')
		return 0;
	for (size_t i = 0; i < len; i++)
		if (p[i] < '0' || p[i] > '9')
			return 0;
	p = field(p + len, " \t", &len);
	if (mnm_parse_mac(p, len, mac) != 0)
		return 0;
	p = field(p + len, " \t", &len);
	if (mnm_parse_ipv4(p, len, ip) != 0)
		return 0;
	p = field(p + len, " \t", &len);
	if (len == 0)
		return 0;
	if (len == 1 && p[0] == '*')
		hostname[0] = '\0';
	else
		copy_name(hostname, hostlen, p, len);
	return 1;
}

int mnm_vendor_parse_line(const char *line, uint64_t *prefix, unsigned int *bits, char *name, size_t namelen)
{
	size_t len, slen, flen;
	const char *p = field(line, " \t", &len);
	uint8_t mac[6] = { 0 };
	unsigned int mask_bits = 24;

	if (len == 0 || *p == '#')
		return 0;

	const char *slash = memchr(p, '/', len);
	size_t alen = slash ? (size_t)(slash - p) : len;
	if (alen == 8) {
		/* "aa:bb:cc": three bytes */
		char full[18];
		memcpy(full, p, 8);
		memcpy(full + 8, ":00:00:00", 9);
		if (mnm_parse_mac(full, 17, mac) != 0)
			return 0;
	} else if (alen == 17) {
		if (mnm_parse_mac(p, 17, mac) != 0)
			return 0;
	} else {
		return 0;
	}
	if (slash) {
		size_t blen = len - alen - 1;
		if (blen != 2 || slash[1] < '0' || slash[1] > '9' || slash[2] < '0' || slash[2] > '9')
			return 0;
		mask_bits = (unsigned int)((slash[1] - '0') * 10 + (slash[2] - '0'));
		if (mask_bits != 24 && mask_bits != 28 && mask_bits != 36)
			return 0;
	} else if (alen == 17) {
		return 0;   /* a full address without length is no prefix */
	}

	const char *sname = field(p + len, "\t", &slen);
	if (slen == 0)
		return 0;
	const char *fname = field(sname + slen, "\t", &flen);
	if (flen)
		copy_name(name, namelen, fname, flen);
	else
		copy_name(name, namelen, sname, slen);
	if (!name[0])
		return 0;

	uint64_t value = mnm_mac_value(mac);
	*bits = mask_bits;
	*prefix = value & (~0ull << (48 - mask_bits)) & 0xffffffffffffull;
	return 1;
}
