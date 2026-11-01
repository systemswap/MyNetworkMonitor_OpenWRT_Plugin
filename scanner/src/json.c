/* SPDX-License-Identifier: GPL-2.0-only */
#include "json.h"

#include <stdint.h>

/* Length of the valid UTF-8 sequence at s (1 to 4), or 0 if the bytes are not
 * a valid, shortest-form sequence (RFC 3629, section 4). */
static size_t utf8_length(const unsigned char *s, size_t avail)
{
	unsigned char c = s[0];
	size_t need;
	uint32_t cp;

	if (c < 0x80)
		return 1;
	if (c >= 0xc2 && c <= 0xdf) {
		need = 2;
		cp = c & 0x1fu;
	} else if (c >= 0xe0 && c <= 0xef) {
		need = 3;
		cp = c & 0x0fu;
	} else if (c >= 0xf0 && c <= 0xf4) {
		need = 4;
		cp = c & 0x07u;
	} else {
		return 0;
	}
	if (avail < need)
		return 0;
	for (size_t i = 1; i < need; i++) {
		if ((s[i] & 0xc0) != 0x80)
			return 0;
		cp = (cp << 6) | (s[i] & 0x3fu);
	}
	if ((need == 3 && cp < 0x800) || (need == 4 && (cp < 0x10000 || cp > 0x10ffff)))
		return 0;
	if (cp >= 0xd800 && cp <= 0xdfff)   /* UTF-16 surrogates are not characters */
		return 0;
	return need;
}

void mnm_json_string(FILE *out, const char *text, size_t len)
{
	const unsigned char *s = (const unsigned char *)text;

	fputc('"', out);
	for (size_t i = 0; i < len;) {
		unsigned char c = s[i];

		if (c == '"' || c == '\\') {
			fputc('\\', out);
			fputc(c, out);
			i++;
		} else if (c == '\n') {
			fputs("\\n", out);
			i++;
		} else if (c == '\r') {
			fputs("\\r", out);
			i++;
		} else if (c == '\t') {
			fputs("\\t", out);
			i++;
		} else if (c < 0x20 || c == 0x7f) {
			fprintf(out, "\\u%04x", c);
			i++;
		} else {
			size_t n = utf8_length(s + i, len - i);
			if (n == 0) {
				fprintf(out, "\\u%04x", c);
				i++;
			} else {
				fwrite(s + i, 1, n, out);
				i += n;
			}
		}
	}
	fputc('"', out);
}
