/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Port list: single ports and ranges, without duplicates, in ascending order.
 * Pure functions without network access, tested on the build machine.
 */
#ifndef MNM_PORTS_H
#define MNM_PORTS_H

#include <stddef.h>
#include <stdint.h>

struct mnm_ports {
	uint16_t *list;   /* ascending, without duplicates */
	size_t count;
};

/*
 * Parses a comma-separated list such as "22,80,443,8000-8100". Ports are 1 to
 * 65535; duplicates and overlapping ranges are merged. Returns 0 on success;
 * on error -1 and a message in err, and ports is left empty.
 */
int mnm_ports_parse(const char *text, struct mnm_ports *ports, char *err, size_t errlen);

void mnm_ports_free(struct mnm_ports *ports);

#endif
