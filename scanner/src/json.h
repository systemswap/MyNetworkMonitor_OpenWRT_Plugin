/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Minimal JSON output. Only writing is needed: the scanner reports results,
 * it never reads JSON.
 */
#ifndef MNM_JSON_H
#define MNM_JSON_H

#include <stddef.h>
#include <stdio.h>

/*
 * Writes text as a JSON string including the quotes. Quote, backslash and
 * control characters are escaped (RFC 8259, section 7). Bytes that are not
 * valid UTF-8 - device answers may contain anything - are written as
 * \u00XX of the byte value, so the output is always valid JSON and valid
 * UTF-8.
 */
void mnm_json_string(FILE *out, const char *text, size_t len);

#endif
