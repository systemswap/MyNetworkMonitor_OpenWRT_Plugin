/* SPDX-License-Identifier: GPL-2.0-only */
#include "targets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mnm_parse_ipv4(const char *text, size_t len, uint32_t *out)
{
	uint32_t value = 0;
	size_t pos = 0;

	for (int part = 0; part < 4; part++) {
		size_t start = pos;
		unsigned int octet = 0;

		while (pos < len && text[pos] >= '0' && text[pos] <= '9') {
			octet = octet * 10u + (unsigned int)(text[pos] - '0');
			if (octet > 255u)
				return -1;
			pos++;
		}
		size_t digits = pos - start;
		if (digits == 0 || digits > 3)
			return -1;
		/* "010" is ambiguous (octal in some tools) and therefore refused. */
		if (digits > 1 && text[start] == '0')
			return -1;
		value = (value << 8) | octet;

		if (part < 3) {
			if (pos >= len || text[pos] != '.')
				return -1;
			pos++;
		}
	}
	if (pos != len)
		return -1;
	*out = value;
	return 0;
}

void mnm_format_ipv4(uint32_t address, char *buf)
{
	snprintf(buf, 16, "%u.%u.%u.%u", (address >> 24) & 0xffu, (address >> 16) & 0xffu,
		 (address >> 8) & 0xffu, address & 0xffu);
}

static int fail(char *err, size_t errlen, const char *entry, size_t len, const char *why)
{
	if (err && errlen)
		snprintf(err, errlen, "\"%.*s\": %s", (int)(len > 60 ? 60 : len), entry, why);
	return -1;
}

/* Parses one entry (without the separating commas) into a range. */
static int parse_entry(const char *s, size_t len, struct mnm_range *range, char *err, size_t errlen)
{
	const char *slash = memchr(s, '/', len);
	const char *dash = memchr(s, '-', len);
	uint32_t first, last;

	if (slash) {
		size_t alen = (size_t)(slash - s);
		const char *p = slash + 1;
		size_t plen = len - alen - 1;
		unsigned int prefix = 0;

		if (mnm_parse_ipv4(s, alen, &first) != 0)
			return fail(err, errlen, s, len, "invalid address");
		if (plen == 0 || plen > 2)
			return fail(err, errlen, s, len, "invalid prefix length");
		for (size_t i = 0; i < plen; i++) {
			if (p[i] < '0' || p[i] > '9')
				return fail(err, errlen, s, len, "invalid prefix length");
			prefix = prefix * 10u + (unsigned int)(p[i] - '0');
		}
		if (prefix > 32)
			return fail(err, errlen, s, len, "prefix length above 32");

		uint32_t mask = prefix == 0 ? 0 : 0xffffffffu << (32 - prefix);
		first &= mask;
		last = first | ~mask;
		/* Network and broadcast address are no hosts, except in /31 and /32
		 * (RFC 3021: both addresses of a /31 are usable). */
		if (prefix <= 30) {
			first++;
			last--;
		}
	} else if (dash) {
		size_t alen = (size_t)(dash - s);
		const char *b = dash + 1;
		size_t blen = len - alen - 1;

		if (mnm_parse_ipv4(s, alen, &first) != 0)
			return fail(err, errlen, s, len, "invalid start address");
		if (memchr(b, '.', blen)) {
			if (mnm_parse_ipv4(b, blen, &last) != 0)
				return fail(err, errlen, s, len, "invalid end address");
		} else {
			/* Short form: only the last octet of the end address. */
			unsigned int octet = 0;
			if (blen == 0 || blen > 3)
				return fail(err, errlen, s, len, "invalid end of range");
			for (size_t i = 0; i < blen; i++) {
				if (b[i] < '0' || b[i] > '9')
					return fail(err, errlen, s, len, "invalid end of range");
				octet = octet * 10u + (unsigned int)(b[i] - '0');
			}
			if (octet > 255u)
				return fail(err, errlen, s, len, "end of range above 255");
			last = (first & 0xffffff00u) | octet;
		}
		if (last < first)
			return fail(err, errlen, s, len, "range ends before it starts");
	} else {
		if (mnm_parse_ipv4(s, len, &first) != 0)
			return fail(err, errlen, s, len, "invalid address");
		last = first;
	}
	range->first = first;
	range->last = last;
	return 0;
}

int mnm_targets_parse(const char *text, struct mnm_targets *targets, char *err, size_t errlen)
{
	size_t capacity = 0;
	uint64_t total = 0;

	targets->ranges = NULL;
	targets->count = 0;
	targets->total = 0;
	if (err && errlen)
		err[0] = '\0';

	const char *p = text;
	for (;;) {
		const char *end = strchr(p, ',');
		size_t len = end ? (size_t)(end - p) : strlen(p);
		const char *s = p;

		while (len && (*s == ' ' || *s == '\t')) {
			s++;
			len--;
		}
		while (len && (s[len - 1] == ' ' || s[len - 1] == '\t'))
			len--;

		if (len) {
			struct mnm_range range;
			if (parse_entry(s, len, &range, err, errlen) != 0)
				goto error;
			if (range.last >= range.first) {
				total += (uint64_t)range.last - range.first + 1;
				if (total > MNM_MAX_TARGETS) {
					fail(err, errlen, s, len, "too many addresses (at most 65536 per scan)");
					goto error;
				}
				if (targets->count == capacity) {
					size_t grown = capacity ? capacity * 2 : 8;
					struct mnm_range *r = realloc(targets->ranges, grown * sizeof(*r));
					if (!r) {
						fail(err, errlen, s, len, "out of memory");
						goto error;
					}
					targets->ranges = r;
					capacity = grown;
				}
				targets->ranges[targets->count++] = range;
			}
		}
		if (!end)
			break;
		p = end + 1;
	}
	if (total == 0) {
		if (err && errlen && !err[0])
			snprintf(err, errlen, "no addresses given");
		goto error;
	}
	targets->total = (uint32_t)total;
	return 0;

error:
	mnm_targets_free(targets);
	return -1;
}

uint32_t mnm_targets_at(const struct mnm_targets *targets, uint32_t index)
{
	for (size_t i = 0; i < targets->count; i++) {
		uint32_t size = targets->ranges[i].last - targets->ranges[i].first + 1;
		if (index < size)
			return targets->ranges[i].first + index;
		index -= size;
	}
	return 0;
}

void mnm_targets_free(struct mnm_targets *targets)
{
	free(targets->ranges);
	targets->ranges = NULL;
	targets->count = 0;
	targets->total = 0;
}
