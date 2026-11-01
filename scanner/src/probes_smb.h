/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Which SMB versions a file server offers: one negotiate request per dialect,
 * each on its own connection to port 445, as the desktop program asks.
 */
#ifndef MNM_PROBES_SMB_H
#define MNM_PROBES_SMB_H

#include <stddef.h>
#include <stdint.h>

#include "probes.h"

/* The probe of this file, terminated by an entry with id NULL. */
extern const struct mnm_probe mnm_probes_smb[];

/* Whether the answer to a negotiate request accepts the requested dialect
 * (smb1: the SMB1 request, else an SMB2 request for one dialect). */
int mnm_smb_accepted(const uint8_t *r, size_t len, int smb1);

#endif
