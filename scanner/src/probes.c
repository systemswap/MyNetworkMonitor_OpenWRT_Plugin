/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The service probes. Each greeting is exactly the byte sequence the service
 * is known to answer to - also the parts that look arbitrary: some services
 * stay silent when a single byte differs. Do not edit the arrays by hand.
 *
 * Every probe reads the answer once (one recv of up to 4096 bytes, see
 * probe_engine.c), and its check sees what arrived in that single read.
 */
#include "probes.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "probes_steps.h"
#include "targets.h"

/* ------------------------------------------------------------- helpers */

void mnm_printable(const uint8_t *in, size_t len, char *out, size_t outlen)
{
	size_t n = 0;
	for (size_t i = 0; i < len && n + 1 < outlen; i++)
		if (in[i] >= 0x20 && in[i] < 0x7f)
			out[n++] = (char)in[i];
	out[n] = '\0';
}

int mnm_contains_ci(const uint8_t *hay, size_t len, const char *needle)
{
	size_t n = strlen(needle);
	if (n == 0 || n > len)
		return n == 0;
	for (size_t i = 0; i + n <= len; i++) {
		size_t k = 0;
		while (k < n && tolower(hay[i + k]) == tolower((unsigned char)needle[k]))
			k++;
		if (k == n)
			return 1;
	}
	return 0;
}

static int starts_with(const uint8_t *r, size_t len, const char *prefix)
{
	size_t n = strlen(prefix);
	return len >= n && memcmp(r, prefix, n) == 0;
}

static uint32_t u32le(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The first line of a text answer, printable characters only. */
static void first_line(const uint8_t *r, size_t len, size_t skip, char *out, size_t outlen)
{
	size_t end = skip;
	while (end < len && r[end] != '\r' && r[end] != '\n')
		end++;
	mnm_printable(r + skip, end > skip ? end - skip : 0, out, outlen);
}

/* ---------------------------------------------------- greeting packets */

static const uint8_t hello_anydesk[188] = {
	0x16, 0x03, 0x01, 0x00, 0xb7, 0x01, 0x00, 0x00, 0xb3, 0x03, 0x03, 0xf9,
	0x37, 0x7f, 0xaa, 0xd3, 0xa2, 0x95, 0x53, 0x76, 0xeb, 0xf1, 0x63, 0x7c,
	0xa9, 0x23, 0x80, 0x4e, 0x48, 0x92, 0xc8, 0x90, 0x8d, 0x6c, 0x03, 0x4c,
	0xc5, 0xe3, 0x83, 0x79, 0xec, 0xb3, 0x8b, 0x00, 0x00, 0x38, 0xc0, 0x2c,
	0xc0, 0x30, 0x00, 0x9f, 0xcc, 0xa9, 0xcc, 0xa8, 0xcc, 0xaa, 0xc0, 0x2b,
	0xc0, 0x2f, 0x00, 0x9e, 0xc0, 0x24, 0xc0, 0x28, 0x00, 0x6b, 0xc0, 0x23,
	0xc0, 0x27, 0x00, 0x67, 0xc0, 0x0a, 0xc0, 0x14, 0x00, 0x39, 0xc0, 0x09,
	0xc0, 0x13, 0x00, 0x33, 0x00, 0x9d, 0x00, 0x9c, 0x00, 0x3d, 0x00, 0x3c,
	0x00, 0x35, 0x00, 0x2f, 0x00, 0xff, 0x01, 0x00, 0x00, 0x52, 0x00, 0x0b,
	0x00, 0x04, 0x03, 0x00, 0x01, 0x02, 0x00, 0x0a, 0x00, 0x0c, 0x00, 0x0a,
	0x00, 0x1d, 0x00, 0x17, 0x00, 0x1e, 0x00, 0x19, 0x00, 0x18, 0x00, 0x23,
	0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x0d,
	0x00, 0x2a, 0x00, 0x28, 0x04, 0x03, 0x05, 0x03, 0x06, 0x03, 0x08, 0x07,
	0x08, 0x08, 0x08, 0x09, 0x08, 0x0a, 0x08, 0x0b, 0x08, 0x04, 0x08, 0x05,
	0x08, 0x06, 0x04, 0x01, 0x05, 0x01, 0x06, 0x01, 0x03, 0x03, 0x03, 0x01,
	0x03, 0x02, 0x04, 0x02, 0x05, 0x02, 0x06, 0x02,
};

static const uint8_t hello_bigfixremote[12] = {
	0x14, 0x2b, 0xb4, 0x91, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t hello_dns_tcp[50] = {
	0x00, 0x30, 0xaa, 0xaa, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x05, 0x70, 0x72, 0x6f, 0x62, 0x65, 0x10, 0x6d, 0x79, 0x6e,
	0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x6d, 0x6f, 0x6e, 0x69, 0x74, 0x6f,
	0x72, 0x07, 0x69, 0x6e, 0x76, 0x61, 0x6c, 0x69, 0x64, 0x00, 0x00, 0x01,
	0x00, 0x01,
};

static const uint8_t hello_influxdb2[59] = {
	0x47, 0x45, 0x54, 0x20, 0x2f, 0x68, 0x65, 0x61, 0x6c, 0x74, 0x68, 0x20,
	0x48, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x31, 0x0d, 0x0a, 0x48, 0x6f,
	0x73, 0x74, 0x3a, 0x20, 0x69, 0x6e, 0x66, 0x6c, 0x75, 0x78, 0x64, 0x62,
	0x0d, 0x0a, 0x43, 0x6f, 0x6e, 0x6e, 0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e,
	0x3a, 0x20, 0x63, 0x6c, 0x6f, 0x73, 0x65, 0x0d, 0x0a, 0x0d, 0x0a,
};

static const uint8_t hello_mongodb[68] = {
	0x44, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xd4, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x64, 0x6d, 0x69,
	0x6e, 0x2e, 0x24, 0x63, 0x6d, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
	0xff, 0xff, 0xff, 0x1d, 0x00, 0x00, 0x00, 0x10, 0x69, 0x73, 0x6d, 0x61,
	0x73, 0x74, 0x65, 0x72, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x68, 0x65,
	0x6c, 0x6c, 0x6f, 0x4f, 0x6b, 0x00, 0x01, 0x00,
};

static const uint8_t hello_oracledb[185] = {
	0x00, 0xb9, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x3a, 0x01, 0x2c,
	0x08, 0x01, 0x08, 0x00, 0x08, 0x00, 0x7f, 0x08, 0x00, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x44, 0x45, 0x53,
	0x43, 0x52, 0x49, 0x50, 0x54, 0x49, 0x4f, 0x4e, 0x3d, 0x28, 0x43, 0x4f,
	0x4e, 0x4e, 0x45, 0x43, 0x54, 0x5f, 0x44, 0x41, 0x54, 0x41, 0x3d, 0x28,
	0x53, 0x45, 0x52, 0x56, 0x49, 0x43, 0x45, 0x5f, 0x4e, 0x41, 0x4d, 0x45,
	0x3d, 0x6f, 0x72, 0x63, 0x6c, 0x29, 0x28, 0x43, 0x49, 0x44, 0x3d, 0x28,
	0x50, 0x52, 0x4f, 0x47, 0x52, 0x41, 0x4d, 0x3d, 0x29, 0x28, 0x48, 0x4f,
	0x53, 0x54, 0x3d, 0x29, 0x28, 0x55, 0x53, 0x45, 0x52, 0x3d, 0x29, 0x29,
	0x29, 0x28, 0x41, 0x44, 0x44, 0x52, 0x45, 0x53, 0x53, 0x3d, 0x28, 0x50,
	0x52, 0x4f, 0x54, 0x4f, 0x43, 0x4f, 0x4c, 0x3d, 0x54, 0x43, 0x50, 0x29,
	0x28, 0x48, 0x4f, 0x53, 0x54, 0x3d, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x68,
	0x6f, 0x73, 0x74, 0x29, 0x28, 0x50, 0x4f, 0x52, 0x54, 0x3d, 0x31, 0x35,
	0x32, 0x31, 0x29, 0x29, 0x29,
};

static const uint8_t hello_postgresql[8] = {
	0x00, 0x00, 0x00, 0x08, 0x04, 0xd2, 0x16, 0x2f,
};

static const uint8_t hello_rdp[19] = {
	0x03, 0x00, 0x00, 0x13, 0x0e, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x08, 0x00, 0x03, 0x00, 0x00, 0x00,
};

static const uint8_t hello_rustdeskclient[91] = {
	0x59, 0x01, 0x3a, 0x54, 0x0a, 0x0c, 0x31, 0x39, 0x38, 0x2e, 0x35, 0x31,
	0x2e, 0x31, 0x30, 0x30, 0x2e, 0x31, 0x22, 0x09, 0x31, 0x32, 0x33, 0x34,
	0x35, 0x36, 0x37, 0x38, 0x39, 0x2a, 0x06, 0x61, 0x62, 0x63, 0x64, 0x65,
	0x66, 0x32, 0x14, 0x48, 0x02, 0x52, 0x10, 0x08, 0x01, 0x10, 0x01, 0x18,
	0x01, 0x28, 0x01, 0x30, 0x01, 0x3a, 0x04, 0x10, 0x01, 0x18, 0x01, 0x50,
	0xfa, 0x8e, 0xf4, 0xbd, 0xdd, 0x8f, 0x88, 0xf0, 0x9f, 0x01, 0x5a, 0x05,
	0x31, 0x2e, 0x33, 0x2e, 0x37, 0x62, 0x00, 0x6a, 0x07, 0x57, 0x69, 0x6e,
	0x64, 0x6f, 0x77, 0x73, 0x08, 0x2a, 0x00,
};

static const uint8_t hello_rustdeskserver[6] = {
	0x14, 0xa2, 0x01, 0x02, 0x08, 0x03,
};

static const uint8_t hello_ssh[21] = {
	0x53, 0x53, 0x48, 0x2d, 0x32, 0x2e, 0x30, 0x2d, 0x4d, 0x79, 0x53, 0x53,
	0x48, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x0d, 0x0a,
};

static const uint8_t hello_teamviewer[37] = {
	0x17, 0x24, 0x0a, 0x20, 0x00, 0xe1, 0xbf, 0xe5, 0x2a, 0x88, 0x13, 0x80,
	0x00, 0x48, 0x00, 0x80, 0x00, 0x01, 0x00, 0x00, 0x00, 0x14, 0x80, 0x00,
	0x00, 0x4f, 0xb3, 0x80, 0x80, 0x6e, 0xbd, 0xf3, 0x9b, 0x8e, 0xdf, 0xa9,
	0x03,
};

static const uint8_t hello_ultravnc[7] = {
	0x52, 0x46, 0x42, 0x20, 0x30, 0x30, 0x33,
};


/* ------------------------------------------------------------ AnyDesk */

static int identify_anydesk(const uint8_t *r, size_t len)
{
	return mnm_contains_ci(r, len, "anydesk client");
}

/* ------------------------------------------------------------- BigFix */

static int identify_bigfix(const uint8_t *r, size_t len)
{
	static const uint8_t expected[12] = { 0x04, 0x2b, 0xb4, 0x90, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	return len == 12 && memcmp(r, expected, 12) == 0;
}

/* ---------------------------------------------------------------- FTP */

/* "220 " with a space; a multi-line greeting "220-" is not taken. */
static int identify_ftp(const uint8_t *r, size_t len)
{
	return starts_with(r, len, "220 ");
}

static int describe_ftp(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	first_line(r, len, 4, out, outlen);
	return out[0] != '\0';
}

/* ----------------------------------------------------------- InfluxDB */

static int identify_influxdb(const uint8_t *r, size_t len)
{
	return mnm_contains_ci(r, len, "influxdb");
}

/* ---------------------------------------------------- MySQL / MariaDB */

/* The server version of a MySQL greeting (HandshakeV10), or 0 if the answer is
 * not one: sequence 0, protocol 10, a printable version text ended by a zero,
 * and the filler zero 13 bytes later if that much was read. */
static int mysql_version(const uint8_t *r, size_t len, size_t *start, size_t *vlen)
{
	if (len < 5 || r[3] != 0x00 || r[4] != 0x0a)
		return 0;
	size_t payload = (size_t)r[0] | (size_t)r[1] << 8 | (size_t)r[2] << 16;
	if (payload < 16)
		return 0;
	size_t end = 5;
	while (end < len && r[end] != 0)
		end++;
	if (end >= len || end == 5 || end - 5 > 64)
		return 0;
	for (size_t i = 5; i < end; i++)
		if (r[i] < 0x20 || r[i] > 0x7e)
			return 0;
	size_t filler = end + 13;
	if (filler < len && r[filler] != 0x00)
		return 0;
	*start = 5;
	*vlen = end - 5;
	return 1;
}

static int is_mariadb(const uint8_t *v, size_t n)
{
	return mnm_contains_ci(v, n, "mariadb") || (n >= 6 && memcmp(v, "5.5.5-", 6) == 0);
}

static int identify_mysql_or_maria(const uint8_t *r, size_t len, int want_maria)
{
	size_t s, n;
	if (mysql_version(r, len, &s, &n))
		return want_maria ? is_mariadb(r + s, n) : !is_mariadb(r + s, n);
	if (len >= 7 && r[3] == 0x00 && r[4] == 0xff) {   /* ERR packet instead of the greeting */
		int maria = mnm_contains_ci(r, len, "mariadb");
		return want_maria ? maria : (mnm_contains_ci(r, len, "mysql") && !maria);
	}
	return 0;
}

static int identify_mysql(const uint8_t *r, size_t len)
{
	return identify_mysql_or_maria(r, len, 0);
}

static int identify_mariadb(const uint8_t *r, size_t len)
{
	return identify_mysql_or_maria(r, len, 1);
}

static int describe_mysql(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	size_t s, n;
	if (!mysql_version(r, len, &s, &n))
		return 0;
	if (n > 6 && memcmp(r + s, "5.5.5-", 6) == 0) {   /* MariaDB prefixes this for old clients */
		s += 6;
		n -= 6;
	}
	char v[72];
	mnm_printable(r + s, n, v, sizeof(v));
	snprintf(out, outlen, "Version %s", v);
	return 1;
}

/* ------------------------------------------------------------ MongoDB */

/* The first occurrence of name followed by a zero, and the BSON type byte in
 * front of it; only the first match is looked at. */
static int bson_field(const uint8_t *r, size_t len, const char *name, size_t *value, uint8_t *type)
{
	size_t n = strlen(name) + 1;
	for (size_t i = 1; i + n <= len; i++) {
		if (memcmp(r + i, name, n) == 0) {
			*type = r[i - 1];
			*value = i + n;
			return *value < len;
		}
	}
	return 0;
}

static int bson_has_bool(const uint8_t *r, size_t len, const char *name)
{
	size_t v;
	uint8_t t;
	return bson_field(r, len, name, &v, &t) && t == 0x08 && v < len;
}

static int bson_int32(const uint8_t *r, size_t len, const char *name, int32_t *out)
{
	size_t v;
	uint8_t t;
	if (!bson_field(r, len, name, &v, &t) || t != 0x10 || v + 4 > len)
		return 0;
	*out = (int32_t)u32le(r + v);
	return 1;
}

static int identify_mongodb(const uint8_t *r, size_t len)
{
	if (len < 21)
		return 0;
	int32_t message_length = (int32_t)u32le(r), response_to = (int32_t)u32le(r + 8), op = (int32_t)u32le(r + 12);
	int32_t wire;
	if (message_length < 21 || (size_t)message_length < len)
		return 0;
	if (response_to != 1 || (op != 1 && op != 2013))
		return 0;
	int role = bson_has_bool(r, len, "ismaster") || bson_has_bool(r, len, "isWritablePrimary");
	return role && bson_int32(r, len, "maxWireVersion", &wire);
}

static int describe_mongodb(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	static const struct { int wire; const char *release; } releases[] = {
		{ 6, "3.6" }, { 7, "4.0" }, { 8, "4.2" }, { 9, "4.4" }, { 13, "5.0" }, { 14, "5.1" },
		{ 17, "6.0" }, { 21, "7.0" }, { 25, "8.0" },
	};
	int32_t wire;
	if (!bson_int32(r, len, "maxWireVersion", &wire))
		return 0;
	for (size_t i = 0; i < sizeof(releases) / sizeof(releases[0]); i++)
		if (releases[i].wire == wire) {
			snprintf(out, outlen, "Version %s (wire protocol %d)", releases[i].release, (int)wire);
			return 1;
		}
	snprintf(out, outlen, "Wire protocol %d", (int)wire);
	return 1;
}

/* ------------------------------------------------------------- Oracle */

/* Accept (2), Refuse (4) or Redirect (11) of the TNS listener. */
static int identify_oracle(const uint8_t *r, size_t len)
{
	return len >= 8 && (r[4] == 0x02 || r[4] == 0x04 || r[4] == 0x0b);
}

/* --------------------------------------------------------- PostgreSQL */

static int identify_postgresql(const uint8_t *r, size_t len)
{
	if (len == 1 && (r[0] == 'S' || r[0] == 'N'))
		return 1;
	return len >= 8 && r[0] == 'R' && r[1] == 0x00 && r[2] == 0x00;
}

static int describe_postgresql(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	if (len != 1)
		return 0;
	snprintf(out, outlen, "%s", r[0] == 'S' ? "Encryption offered (TLS)" : "Encryption not offered");
	return 1;
}

/* ---------------------------------------------------------------- RDP */

static int identify_rdp(const uint8_t *r, size_t len)
{
	return len >= 6 && r[0] == 0x03 && r[1] == 0x00 && r[5] == 0xd0;
}

static int describe_rdp(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	if (len >= 19 && r[11] == 0x02) {
		uint32_t p = u32le(r + 15);
		const char *what = p == 0 ? "Standard RDP" : p == 1 ? "TLS" : p == 2 ? "CredSSP (NLA)" : p == 8 ? "RDSTLS"
			: p == 0x10 ? "CredSSP with early authorisation" : NULL;
		if (what)
			snprintf(out, outlen, "%s", what);
		else
			snprintf(out, outlen, "Protocol %u", p);
	} else if (len >= 16 && r[11] == 0x03) {
		snprintf(out, outlen, "Negotiation refused (code %u)", r[15]);
	} else {
		snprintf(out, outlen, "Standard RDP (no NLA)");
	}
	return 1;
}

/* ---------------------------------------------------------- RustDesk */

static int identify_rustdesk_client(const uint8_t *r, size_t len)
{
	if (len < 6 || r[1] != 0x4a || r[3] != 0x0a || r[4] == 0)
		return 0;
	if ((int)r[2] < (int)r[4] + 2 || len < 5u + r[4])
		return 0;
	int single_byte_frame = (r[0] & 0x03) == 0;
	int framed = r[0] >> 2;
	return !single_byte_frame || (len >= (size_t)framed + 1 && (int)r[2] == framed - 2);
}

static int identify_rustdesk_server(const uint8_t *r, size_t len)
{
	if (len < 7 || len > 9 || r[0] != 0x1c || r[1] != 0xaa || r[2] != 0x01)
		return 0;
	if (r[3] != len - 4 || r[4] != 0x08)
		return 0;
	uint32_t value = 0;
	unsigned int shift = 0;
	int ok = 0;
	for (size_t i = 5; i < len; i++) {
		if (shift > 28)
			break;
		value |= (uint32_t)(r[i] & 0x7f) << shift;
		shift += 7;
		if (!(r[i] & 0x80)) {
			ok = i == len - 1;
			break;
		}
	}
	return ok && value > 0 && value <= 65535;
}

/* ---------------------------------------------------------------- SSH */

static int identify_ssh(const uint8_t *r, size_t len)
{
	return starts_with(r, len, "SSH-2.0");
}

/* "SSH-2.0-OpenSSH_9.2p1 Debian-2" -> "OpenSSH_9.2p1 Debian-2". */
static int describe_ssh(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	first_line(r, len, 8, out, outlen);
	return out[0] != '\0';
}

/* --------------------------------------------------------- TeamViewer */

static int identify_teamviewer(const uint8_t *r, size_t len)
{
	static const uint8_t h1[4] = { 0x17, 0x24, 0x0a, 0x20 }, h2[4] = { 0x11, 0x30, 0x36, 0x00 };
	return len >= 41 && memcmp(r, h1, 4) == 0 && memcmp(r + 37, h2, 4) == 0;
}

/* ---------------------------------------------------------------- VNC */

static int identify_vnc(const uint8_t *r, size_t len)
{
	return len >= 3 && r[0] == 'R' && r[1] == 'F' && r[2] == 'B';
}

static int describe_vnc(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	char line[16];
	first_line(r, len < 11 ? len : 11, 0, line, sizeof(line));
	snprintf(out, outlen, "Protocol %s", line);
	return 1;
}

/* ---------------------------------------------------------------- Web */

/* The request of the plain HTTP check, with the target as Host. */
static size_t build_http(uint32_t address, uint8_t *buf, size_t cap)
{
	char host[16];
	mnm_format_ipv4(address, host);
	int n = snprintf((char *)buf, cap,
			 "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
			 "User-Agent: Mozilla/5.0 (compatible; MyScanner/1.0)\r\nAccept: */*\r\n"
			 "Accept-Encoding: identity\r\n\r\n", host);
	return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

/* A status line "HTTP/x.y NNN" (2xx to 5xx) at the start, or "<html" anywhere. */
static int identify_http(const uint8_t *r, size_t len)
{
	if (len >= 12 && memcmp(r, "HTTP/", 5) == 0 && isdigit(r[5]) && r[6] == '.' && isdigit(r[7]) && r[8] == ' ' &&
	    r[9] >= '2' && r[9] <= '5' && isdigit(r[10]) && isdigit(r[11]))
		return 1;
	return mnm_contains_ci(r, len, "<html");
}

/* The value of the Server header, if any. */
static int describe_http(const uint8_t *r, size_t len, char *out, size_t outlen)
{
	for (size_t i = 0; i + 8 < len; i++) {
		if ((i == 0 || r[i - 1] == '\n') && mnm_contains_ci(r + i, 7, "server:")) {
			size_t s = i + 7;
			while (s < len && r[s] == ' ')
				s++;
			first_line(r, len, s, out, outlen);
			return out[0] != '\0';
		}
	}
	return 0;
}

/* ---------------------------------------------------------------- DNS */

/* The answer over TCP starts with its 2-byte length, then the DNS header: the
 * same ID as the query (0xAAAA) and the QR bit. Any answer counts, also NXDOMAIN,
 * because the name asked for never exists. */
static int identify_dns_tcp(const uint8_t *r, size_t len)
{
	return len >= 5 && r[2] == 0xaa && r[3] == 0xaa && (r[4] & 0x80);
}

/* -------------------------------------------------------------- table */

#define FIXED(a) a, sizeof(a), NULL

const struct mnm_probe mnm_probes[] = {
	{ "anydesk", "AnyDesk", FIXED(hello_anydesk), identify_anydesk, NULL, NULL },
	{ "bigfixremote", "BigFix", FIXED(hello_bigfixremote), identify_bigfix, NULL, NULL },
	{ "ftp", "FTP", NULL, 0, NULL, identify_ftp, describe_ftp, NULL },
	{ "influxdb2", "InfluxDB", FIXED(hello_influxdb2), identify_influxdb, NULL, NULL },
	{ "mysql", "MySQL", NULL, 0, NULL, identify_mysql, describe_mysql, NULL },
	{ "mariadb", "MariaDB", NULL, 0, NULL, identify_mariadb, describe_mysql, NULL },
	{ "mongodb", "MongoDB", FIXED(hello_mongodb), identify_mongodb, describe_mongodb, NULL },
	{ "oracledb", "Oracle", FIXED(hello_oracledb), identify_oracle, NULL, NULL },
	{ "postgresql", "PostgreSQL", FIXED(hello_postgresql), identify_postgresql, describe_postgresql, NULL },
	{ "rdp", "RDP", FIXED(hello_rdp), identify_rdp, describe_rdp, NULL },
	{ "rustdeskclient", "RustDesk", FIXED(hello_rustdeskclient), identify_rustdesk_client, NULL, NULL },
	{ "rustdeskserver", "RustDesk server", FIXED(hello_rustdeskserver), identify_rustdesk_server, NULL, NULL },
	{ "ssh", "SSH", FIXED(hello_ssh), identify_ssh, describe_ssh, NULL },
	{ "teamviewer", "TeamViewer", FIXED(hello_teamviewer), identify_teamviewer, NULL, NULL },
	{ "ultravnc", "VNC", FIXED(hello_ultravnc), identify_vnc, describe_vnc, NULL },
	{ "webservices", "HTTP", NULL, 0, build_http, identify_http, describe_http, NULL },
	{ "dns_tcp", "DNS", FIXED(hello_dns_tcp), identify_dns_tcp, NULL, NULL },
	{ NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL },
};

const struct mnm_probe *mnm_probe_find(const char *id)
{
	for (const struct mnm_probe *p = mnm_probes; p->id; p++)
		if (strcmp(p->id, id) == 0)
			return p;
	for (const struct mnm_probe *p = mnm_probes_steps; p->id; p++)
		if (strcmp(p->id, id) == 0)
			return p;
	return NULL;
}
