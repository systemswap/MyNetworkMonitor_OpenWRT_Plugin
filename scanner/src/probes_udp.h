/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Probes over UDP: DNS, BACnet and the SQL Server Browser are asked of every
 * device in the table (UDP has no port scan that tells where a service
 * listens), DHCP servers answer one broadcast per local network. Packets and
 * waiting times are those of the desktop program; the evaluation of the
 * answers is kept in pure functions, tested on the build machine.
 */
#ifndef MNM_PROBES_UDP_H
#define MNM_PROBES_UDP_H

#include <stddef.h>
#include <stdint.h>

#include "probes.h"

/* The probes of this file, terminated by an entry with id NULL. */
extern const struct mnm_probe mnm_probes_udp[];

/* Number of BACnet properties read after the device answered. */
#define MNM_BACNET_PROPERTIES 9

/* The detail of a BACnet device from the answers to the nine ReadProperty
 * requests (ObjectID, VendorID, Vendor Name, Firmware Revision, Application
 * Software, Object Name, Model Name, Description, Location); an answer may be
 * NULL. */
void mnm_bacnet_describe(const uint8_t *const answers[MNM_BACNET_PROPERTIES], const size_t lens[MNM_BACNET_PROPERTIES],
			 char *out, size_t outlen);

/* The answer of an SQL Server Browser: the TCP ports of the instances
 * ("tcp;<port>", at most max) and a detail with instance names and versions.
 * Returns the number of ports; 0 means no instance with a TCP port. */
size_t mnm_mssql_browser(const uint8_t *r, size_t len, uint16_t *ports, size_t max, char *out, size_t outlen);

/* The server of a DHCP answer: option 54, else the relay (giaddr), else the
 * next server (siaddr). Returns 1 and the address in host byte order, else 0. */
int mnm_dhcp_server(const uint8_t *r, size_t len, uint32_t *server);

#endif
