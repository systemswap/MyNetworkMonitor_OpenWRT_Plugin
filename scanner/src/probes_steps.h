/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Probes of databases and industrial controllers whose procedure has several
 * steps: a second connection for the details, a search for the endpoint of a
 * controller (S7), a second attempt with other packets (SECS/GEM) or a read
 * loop (WAGO). Each follows the procedure of the desktop program step by step,
 * with the same packets byte for byte. The evaluation of the answers is kept
 * in pure functions, tested on the build machine.
 */
#ifndef MNM_PROBES_STEPS_H
#define MNM_PROBES_STEPS_H

#include <stddef.h>
#include <stdint.h>

#include "probes.h"

/* The probes of this file, terminated by an entry with id NULL. */
extern const struct mnm_probe mnm_probes_steps[];

/* What the two S7 system status lists 0x0011 (module) and 0x001C (names)
 * tell about the controller, from the complete answers including the TPKT
 * header; either may be NULL. Appends to out, separated by " · ". */
void mnm_s7_describe_szl(const uint8_t *modules, size_t modules_len, const uint8_t *components,
			 size_t components_len, char *out, size_t outlen);

/* The fields of a Modbus "Read Device Identification" answer (function 0x2B,
 * MEI 0x0E); returns 1 if anything was written. */
int mnm_modbus_device_id(const uint8_t *r, size_t len, char *out, size_t outlen);

/* The text of a Modbus "Report Server ID" answer (function 0x11); returns 1
 * if anything was written. */
int mnm_modbus_server_id(const uint8_t *r, size_t len, char *out, size_t outlen);

/* Text from Modbus registers whose two bytes are swapped (WAGO): at least two
 * printable characters, else 0. */
int mnm_swapped_text(const uint8_t *data, size_t len, char *out, size_t outlen);

/* Appends a part to a detail line, separated by " · ". */
void mnm_detail_add(char *out, size_t outlen, const char *part);

#endif
