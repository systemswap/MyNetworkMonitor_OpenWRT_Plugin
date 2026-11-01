/* SPDX-License-Identifier: GPL-2.0-only */
#include "ports.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *err, size_t errlen, const char *entry, size_t len, const char *why)
{
	if (err && errlen) {
		if (len)
			snprintf(err, errlen, "\"%.*s\": %s", (int)(len > 40 ? 40 : len), entry, why);
		else
			snprintf(err, errlen, "%s", why);
	}
	return -1;
}

/* Reads a decimal port number 1-65535 from exactly len characters. */
static int parse_port(const char *s, size_t len, unsigned int *out)
{
	unsigned int value = 0;

	if (len == 0 || len > 5)
		return -1;
	for (size_t i = 0; i < len; i++) {
		if (s[i] < '0' || s[i] > '9')
			return -1;
		value = value * 10u + (unsigned int)(s[i] - '0');
	}
	if (value < 1 || value > 65535)
		return -1;
	*out = value;
	return 0;
}

int mnm_ports_parse(const char *text, struct mnm_ports *ports, char *err, size_t errlen)
{
	/* One bit per port: 8 KiB, merges duplicates and keeps the order for free. */
	uint8_t *seen = calloc(65536 / 8, 1);
	size_t count = 0;

	ports->list = NULL;
	ports->count = 0;
	if (err && errlen)
		err[0] = '\0';
	if (!seen) {
		fail(err, errlen, "", 0, "out of memory");
		return -1;
	}

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
			const char *dash = memchr(s, '-', len);
			unsigned int first, last;

			if (dash) {
				size_t alen = (size_t)(dash - s);
				if (parse_port(s, alen, &first) != 0 || parse_port(dash + 1, len - alen - 1, &last) != 0) {
					fail(err, errlen, s, len, "invalid port range (ports are 1 to 65535)");
					goto error;
				}
				if (last < first) {
					fail(err, errlen, s, len, "range ends before it starts");
					goto error;
				}
			} else {
				if (parse_port(s, len, &first) != 0) {
					fail(err, errlen, s, len, "invalid port (ports are 1 to 65535)");
					goto error;
				}
				last = first;
			}
			for (unsigned int port = first; port <= last; port++) {
				if (!(seen[port >> 3] & (1u << (port & 7)))) {
					seen[port >> 3] |= (uint8_t)(1u << (port & 7));
					count++;
				}
			}
		}
		if (!end)
			break;
		p = end + 1;
	}

	if (count == 0) {
		fail(err, errlen, "", 0, "no ports given");
		goto error;
	}
	ports->list = malloc(count * sizeof(*ports->list));
	if (!ports->list) {
		fail(err, errlen, "", 0, "out of memory");
		goto error;
	}
	for (unsigned int port = 1; port <= 65535; port++)
		if (seen[port >> 3] & (1u << (port & 7)))
			ports->list[ports->count++] = (uint16_t)port;
	free(seen);
	return 0;

error:
	free(seen);
	ports->list = NULL;
	ports->count = 0;
	return -1;
}

void mnm_ports_free(struct mnm_ports *ports)
{
	free(ports->list);
	ports->list = NULL;
	ports->count = 0;
}
