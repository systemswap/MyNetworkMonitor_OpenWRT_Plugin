/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SNMP: name, description, location, contact, serial number and hardware
 * addresses of a device, asked with community "public" as the desktop program
 * asks them. The reading of the answers (BER, X.690) is kept in pure
 * functions, tested on the build machine.
 */
#ifndef MNM_PROBES_SNMP_H
#define MNM_PROBES_SNMP_H

#include <stddef.h>
#include <stdint.h>

#include "probes.h"

/* The probe of this file, terminated by an entry with id NULL. */
extern const struct mnm_probe mnm_probes_snmp[];

/* One value of an answer. */
struct mnm_snmp_value {
	uint8_t oid[32];          /* the object identifier as encoded */
	size_t oid_len;
	char text[160];           /* the value as text */
	uint8_t raw[16];          /* the first bytes of an octet string (hardware addresses) */
	size_t raw_len;
	int present;              /* 0 for NULL, noSuchObject, noSuchInstance, endOfMibView */
};

/* Reads a Response (or Report) to the request with this ID: error status 0,
 * then up to max values. Returns their number, or -1 for anything else. */
int mnm_snmp_parse(const uint8_t *r, size_t len, uint32_t id, struct mnm_snmp_value *values, size_t max);

/* What a device told. */
struct mnm_snmp_info {
	char name[96], descr[160], location[96], contact[96], serial[64], macs[160];
};

/* The detail: "Name: … · Descr: … · Location: … · Contact: … · Serial: … · MAC: …",
 * empty parts left out. */
void mnm_snmp_describe(const struct mnm_snmp_info *s, char *out, size_t outlen);

#endif
