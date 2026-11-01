/* SPDX-License-Identifier: GPL-2.0-only */
#include "bc_parse.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <unistd.h>

#include "probes_steps.h"

static unsigned int u16be(const uint8_t *p)
{
	return (unsigned int)p[0] << 8 | p[1];
}

/* Case-insensitive search for needle in the len bytes at hay. */
static const char *find_ci(const char *hay, size_t len, const char *needle)
{
	size_t n = strlen(needle);
	for (size_t i = 0; n && i + n <= len; i++)
		if (strncasecmp(hay + i, needle, n) == 0)
			return hay + i;
	return NULL;
}

/* Copies text, keeping printable ASCII and the bytes of UTF-8 characters,
 * turning every run of white space into one space and dropping it at both ends. */
static void clean_copy(const char *in, size_t len, char *out, size_t outlen)
{
	size_t n = 0;
	int space = 0;
	if (!outlen)
		return;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)in[i];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			space = n > 0;
			continue;
		}
		if (c < 0x20 || c == 0x7f)
			continue;
		if (space && n + 1 < outlen)
			out[n++] = ' ';
		space = 0;
		if (n + 1 < outlen)
			out[n++] = (char)c;
	}
	/* never end in the middle of a UTF-8 character */
	while (n > 0 && ((unsigned char)out[n - 1] & 0xc0) == 0x80)
		n--;
	if (n > 0 && ((unsigned char)out[n - 1] & 0xc0) == 0xc0)
		n--;
	out[n] = '\0';
}

/* ---------------------------------------------------------------- SSDP */

int mnm_ssdp_header(const char *answer, size_t len, const char *name, char *out, size_t outlen)
{
	size_t n = strlen(name);
	out[0] = '\0';
	for (size_t line = 0; line < len;) {
		size_t end = line;
		while (end < len && answer[end] != '\n')
			end++;
		if (end - line > n && strncasecmp(answer + line, name, n) == 0 && answer[line + n] == ':') {
			clean_copy(answer + line + n + 1, end - line - n - 1, out, outlen);
			return out[0] != '\0';
		}
		line = end + 1;
	}
	return 0;
}

int mnm_url_split(const char *url, char *host, size_t hostlen, uint16_t *port, char *path, size_t pathlen)
{
	if (strncasecmp(url, "http://", 7) != 0)
		return 0;
	const char *h = url + 7, *e = h;
	while (*e && *e != ':' && *e != '/' && *e != '?' && *e != '#')
		e++;
	if (e == h || (size_t)(e - h) >= hostlen || (size_t)(e - h) > 63 || memchr(h, '@', (size_t)(e - h)) ||
	    memchr(h, '[', (size_t)(e - h)))
		return 0;
	memcpy(host, h, (size_t)(e - h));
	host[e - h] = '\0';
	*port = 80;
	if (*e == ':') {
		unsigned long p = 0;
		const char *d = ++e;
		while (*e >= '0' && *e <= '9' && e - d < 6)
			p = p * 10 + (unsigned long)(*e++ - '0');
		if (e == d || p == 0 || p > 65535)
			return 0;
		*port = (uint16_t)p;
	}
	if (*e && *e != '/')
		return 0;
	snprintf(path, pathlen, "%s", *e ? e : "/");
	/* no control characters or spaces in the request line */
	for (const char *c = path; *c; c++)
		if ((unsigned char)*c <= ' ' || *c == 0x7f)
			return 0;
	return 1;
}

/* ----------------------------------------------------------------- XML */

/* The local name of the tag starting at xml[i] ('<' already passed): its
 * position and length; the name ends at white space, '>' or '/'. */
static size_t tag_name(const char *xml, size_t len, size_t i, size_t *local)
{
	size_t e = i;
	*local = i;
	while (e < len && xml[e] != ' ' && xml[e] != '\t' && xml[e] != '\r' && xml[e] != '\n' && xml[e] != '>' &&
	       xml[e] != '/') {
		if (xml[e] == ':')
			*local = e + 1;
		e++;
	}
	return e;
}

static int name_is(const char *xml, size_t start, size_t end, const char *name)
{
	size_t n = strlen(name);
	return end - start == n && strncasecmp(xml + start, name, n) == 0;
}

/* Appends the text between start and end with the five predefined entities
 * (XML 1.0, 4.6) and character references (4.1) decoded. */
static size_t append_text(const char *xml, size_t start, size_t end, char *buf, size_t n, size_t cap)
{
	static const struct { const char *entity; char c; } entities[] = {
		{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' },
	};
	for (size_t i = start; i < end && n + 1 < cap;) {
		if (xml[i] == '&') {
			size_t k;
			for (k = 0; k < 5; k++) {
				size_t l = strlen(entities[k].entity);
				if (i + l <= end && memcmp(xml + i, entities[k].entity, l) == 0) {
					buf[n++] = entities[k].c;
					i += l;
					break;
				}
			}
			if (k < 5)
				continue;
			if (i + 3 < end && xml[i + 1] == '#') {
				const char *semi = memchr(xml + i, ';', end - i);
				if (semi && semi - (xml + i) < 10) {
					long v = xml[i + 2] == 'x' ? strtol(xml + i + 3, NULL, 16) : strtol(xml + i + 2, NULL, 10);
					buf[n++] = (v >= 0x20 && v < 0x7f) ? (char)v : '?';
					i = (size_t)(semi - xml) + 1;
					continue;
				}
			}
		}
		buf[n++] = xml[i++];
	}
	return n;
}

int mnm_xml_text(const char *xml, size_t len, const char *name, char *out, size_t outlen)
{
	char text[1024];
	out[0] = '\0';
	for (size_t i = 0; i + 1 < len; i++) {
		if (xml[i] != '<' || xml[i + 1] == '/' || xml[i + 1] == '?' || xml[i + 1] == '!')
			continue;
		size_t local, end = tag_name(xml, len, i + 1, &local);
		if (!name_is(xml, local, end, name))
			continue;
		const char *gt = memchr(xml + end, '>', len - end);
		if (!gt || gt[-1] == '/')
			return 0;   /* <name/>: empty */
		size_t n = 0, at = (size_t)(gt - xml) + 1, text_start = at;
		int depth = 1;
		while (at < len && depth > 0) {
			if (xml[at] != '<') {
				at++;
				continue;
			}
			n = append_text(xml, text_start, at, text, n, sizeof(text));
			int closing = at + 1 < len && xml[at + 1] == '/';
			size_t l, e = tag_name(xml, len, at + 1 + (size_t)closing, &l);
			const char *close = memchr(xml + e, '>', len - e);
			if (!close)
				break;
			if (name_is(xml, l, e, name)) {
				if (closing)
					depth--;
				else if (close[-1] != '/')
					depth++;
			}
			at = (size_t)(close - xml) + 1;
			text_start = at;
		}
		clean_copy(text, n, out, outlen);
		return out[0] != '\0';
	}
	return 0;
}

/* ----------------------------------------------------------- UPnP, WSD */

int mnm_upnp_describe(const char *xml, size_t len, char *out, size_t outlen)
{
	char name[96], maker[96], model[96], number[48], part[200];
	out[0] = '\0';
	if (mnm_xml_text(xml, len, "friendlyName", name, sizeof(name))) {
		snprintf(part, sizeof(part), "Name: %s", name);
		mnm_detail_add(out, outlen, part);
	}
	if (mnm_xml_text(xml, len, "manufacturer", maker, sizeof(maker))) {
		snprintf(part, sizeof(part), "Manufacturer: %s", maker);
		mnm_detail_add(out, outlen, part);
	}
	int has_model = mnm_xml_text(xml, len, "modelName", model, sizeof(model));
	int has_number = mnm_xml_text(xml, len, "modelNumber", number, sizeof(number));
	if (has_model || has_number) {
		snprintf(part, sizeof(part), "Model: %s%s%s", has_model ? model : "", has_model && has_number ? " " : "",
			 has_number ? number : "");
		mnm_detail_add(out, outlen, part);
	}
	return out[0] != '\0';
}

void mnm_wsd_describe(const char *xml, size_t len, char *out, size_t outlen)
{
	char types[256], kind[80] = "", part[300];
	out[0] = '\0';
	if (!mnm_xml_text(xml, len, "Types", types, sizeof(types))) {
		snprintf(out, outlen, "%s", memchr(xml, '<', len) ? "Replied." : "Replied, but the response could not be read.");
		return;
	}
	size_t tl = strlen(types);
	static const struct { const char *a, *b, *kind; } kinds[] = {
		{ "PrintDeviceType", "Print", "printer" }, { "ScanDeviceType", NULL, "scanner" },
		{ "Computer", NULL, "Windows computer" }, { "NetworkVideoTransmitter", NULL, "camera" },
	};
	for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
		if (!find_ci(types, tl, kinds[k].a) && !(kinds[k].b && find_ci(types, tl, kinds[k].b)))
			continue;
		size_t used = strlen(kind);
		snprintf(kind + used, sizeof(kind) - used, "%s%s", used ? ", " : "", kinds[k].kind);
	}
	mnm_detail_add(out, outlen, kind);
	snprintf(part, sizeof(part), "Types: %s", types);
	mnm_detail_add(out, outlen, part);
}

/* --------------------------------------------------------------- ONVIF */

static int hex_value(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int mnm_onvif_describe(const char *xml, size_t len, char *out, size_t outlen)
{
	char address[128], xaddrs[512], scopes[1024], version[32], name[96] = "Unknown";
	out[0] = '\0';
	if (!mnm_xml_text(xml, len, "Address", address, sizeof(address)) ||
	    !mnm_xml_text(xml, len, "XAddrs", xaddrs, sizeof(xaddrs)) ||
	    !mnm_xml_text(xml, len, "Scopes", scopes, sizeof(scopes)) ||
	    !mnm_xml_text(xml, len, "MetadataVersion", version, sizeof(version)))
		return 0;
	/* the name scope, e.g. "onvif://www.onvif.org/name/Camera%20Garden": its last
	 * part, percent-decoded */
	char *save = NULL;
	for (char *s = strtok_r(scopes, " ", &save); s; s = strtok_r(NULL, " ", &save)) {
		if (!strstr(s, "/name/"))
			continue;
		const char *last = strrchr(s, '/') + 1;
		size_t n = 0;
		for (size_t i = 0; last[i] && n + 1 < sizeof(name); i++) {
			if (last[i] == '%' && hex_value(last[i + 1]) >= 0 && hex_value(last[i + 2]) >= 0) {
				int c = hex_value(last[i + 1]) * 16 + hex_value(last[i + 2]);
				name[n++] = (c >= 0x20 && c != 0x7f) ? (char)c : '?';
				i += 2;
			} else {
				name[n++] = last[i];
			}
		}
		name[n] = '\0';
		break;
	}
	/* the first service address, without the usual path of the device service */
	char *first = strtok_r(xaddrs, " ", &save);
	char *cut = first ? strstr(first, "/onvif/device_service") : NULL;
	if (cut)
		memmove(cut, cut + 21, strlen(cut + 21) + 1);
	snprintf(out, outlen, "%s%s%s", name, first ? " " : "", first ? first : "");
	return 1;
}

/* ---------------------------------------------------------------- mDNS */

/* Reads a DNS name at off into out (dotted, without the final dot; labels may
 * contain spaces), following compression pointers (at most 16). Returns the
 * offset after the name in the record, or 0 if it is damaged. */
static size_t read_name(const uint8_t *r, size_t len, size_t off, char *out, size_t outlen)
{
	size_t n = 0, after = 0;
	for (int jumps = 0; off < len;) {
		uint8_t l = r[off];
		if (l == 0) {
			out[n] = '\0';
			return after ? after : off + 1;
		}
		if ((l & 0xc0) == 0xc0) {
			if (off + 1 >= len || ++jumps > 16)
				break;
			if (!after)
				after = off + 2;
			off = (size_t)(l & 0x3f) << 8 | r[off + 1];
			continue;
		}
		if ((l & 0xc0) || off + 1 + l > len)
			break;
		if (n && n + 1 < outlen)
			out[n++] = '.';
		for (size_t i = 0; i < l; i++) {
			uint8_t c = r[off + 1 + i];
			if (n + 1 < outlen && c >= 0x20 && c != 0x7f)
				out[n++] = (char)c;
		}
		off += 1u + l;
	}
	out[0] = '\0';
	return 0;
}

static int ends_with(const char *s, const char *suffix)
{
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && strcasecmp(s + a - b, suffix) == 0;
}

static void add_service(struct mnm_mdns_info *info, const char *type)
{
	char t[40];
	snprintf(t, sizeof(t), "%s", type);
	if (ends_with(t, ".local"))
		t[strlen(t) - 6] = '\0';
	if (t[0] != '_')
		return;
	for (int i = 0; i < info->service_count; i++)
		if (strcasecmp(info->services[i], t) == 0)
			return;
	if (info->service_count < 8)
		snprintf(info->services[info->service_count++], sizeof(info->services[0]), "%s", t);
}

int mnm_mdns_parse(const uint8_t *r, size_t len, uint32_t source, struct mnm_mdns_info *info)
{
	char owner[256], target[256], srv_host[64] = "";
	int records = 0;
	if (len < 12 || !(r[2] & 0x80))
		return 0;
	unsigned int questions = u16be(r + 4), count = u16be(r + 6) + u16be(r + 8) + u16be(r + 10);
	size_t off = 12;
	for (unsigned int i = 0; i < questions; i++) {
		off = read_name(r, len, off, owner, sizeof(owner));
		if (!off || off + 4 > len)
			return 0;
		off += 4;
	}
	for (unsigned int i = 0; i < count; i++) {
		off = read_name(r, len, off, owner, sizeof(owner));
		if (!off || off + 10 > len)
			break;
		unsigned int type = u16be(r + off);
		size_t rdlen = u16be(r + off + 8), data = off + 10;
		if (data + rdlen > len)
			break;
		off = data + rdlen;
		records++;
		if (type == 12 && read_name(r, len, data, target, sizeof(target))) {
			/* the list of all service types answers with the types as targets */
			if (strcasecmp(owner, "_services._dns-sd._udp.local") == 0) {
				add_service(info, target);
			} else {
				add_service(info, owner);
				const char *dot = strchr(target, '.');
				if (!info->instance[0] && target[0] != '_' && dot && dot > target)
					snprintf(info->instance, sizeof(info->instance), "%.*s", (int)(dot - target), target);
			}
		} else if (type == 1 && rdlen == 4) {
			uint32_t a = (uint32_t)r[data] << 24 | (uint32_t)r[data + 1] << 16 | (uint32_t)r[data + 2] << 8 | r[data + 3];
			if (a == source && !info->host[0]) {
				snprintf(info->host, sizeof(info->host), "%.63s", owner);
				if (ends_with(info->host, ".local"))
					info->host[strlen(info->host) - 6] = '\0';
			}
		} else if (type == 33 && rdlen > 6 && !srv_host[0] && read_name(r, len, data + 6, target, sizeof(target))) {
			snprintf(srv_host, sizeof(srv_host), "%.63s", target);
		}
	}
	/* without an address record for the sender, the host of a service record */
	if (!info->host[0] && srv_host[0]) {
		snprintf(info->host, sizeof(info->host), "%s", srv_host);
		if (ends_with(info->host, ".local"))
			info->host[strlen(info->host) - 6] = '\0';
	}
	return records > 0;
}

void mnm_mdns_describe(const struct mnm_mdns_info *info, char *out, size_t outlen)
{
	char part[400] = "Services:", device[96];
	out[0] = '\0';
	for (int i = 0; i < info->service_count; i++) {
		size_t used = strlen(part);
		snprintf(part + used, sizeof(part) - used, "%s %s", i ? "," : "", info->services[i]);
	}
	if (info->service_count)
		mnm_detail_add(out, outlen, part);
	if (info->instance[0]) {
		snprintf(device, sizeof(device), "Device: %s", info->instance);
		mnm_detail_add(out, outlen, device);
	}
	if (!out[0])
		snprintf(out, outlen, "Replied.");
}

/* ------------------------------------------------------------- NetBIOS */

/* A 15-byte NetBIOS name: ASCII (other bytes as '?'), without the padding. */
static void netbios_name(const uint8_t *p, char *out, size_t outlen)
{
	size_t n = 0;
	for (size_t i = 0; i < 15 && n + 1 < outlen; i++)
		out[n++] = p[i] >= 0x80 ? '?' : (char)p[i];
	out[n] = '\0';
	while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\0'))
		out[--n] = '\0';
	size_t s = 0;
	while (out[s] == ' ')
		s++;
	memmove(out, out + s, strlen(out + s) + 1);
	for (char *c = out; *c; c++)
		if ((unsigned char)*c < 0x20)
			*c = '?';
}

int mnm_netbios_parse(const uint8_t *r, size_t len, uint16_t id, char *host, size_t hostlen, char *workgroup,
		      size_t grouplen)
{
	host[0] = workgroup[0] = '\0';
	if (len < 12 || u16be(r) != id || !(r[2] & 0x80) || (r[3] & 0x0f) || u16be(r + 6) == 0)
		return 0;
	size_t at = 12;
	for (int labels = 0;; labels++) {   /* the question name, or a pointer */
		if (labels > 64 || at >= len)
			return 0;
		uint8_t b = r[at];
		if (b == 0) {
			at++;
			break;
		}
		if ((b & 0xc0) == 0xc0) {
			if (at + 2 > len)
				return 0;
			at += 2;
			break;
		}
		if (b & 0xc0)
			return 0;
		at += 1u + b;
	}
	if (at + 10 > len || u16be(r + at) != 0x21 || u16be(r + at + 2) != 1 || u16be(r + at + 8) < 1)
		return 0;
	size_t rdlen = u16be(r + at + 8);
	at += 10;
	if (at >= len)
		return 0;
	if (rdlen > len - at)
		rdlen = len - at;   /* a cut-off answer still yields its complete names */
	const uint8_t *d = r + at;
	size_t count = d[0], off = 1;
	char unique20[16] = "", unique00[16] = "", group00[16] = "", group1e[16] = "";
	for (size_t i = 0; i < count && off + 18 <= rdlen; i++, off += 18) {
		char name[16];
		netbios_name(d + off, name, sizeof(name));
		uint8_t suffix = d[off + 15];
		int group = (d[off + 16] & 0x80) != 0;
		if (!name[0])
			continue;
		char *slot = !group && suffix == 0x20 ? unique20 : !group && suffix == 0x00 ? unique00
			   : group && suffix == 0x00 ? group00 : group && suffix == 0x1e ? group1e : NULL;
		if (slot && !slot[0])
			snprintf(slot, 16, "%s", name);
	}
	snprintf(host, hostlen, "%s", unique20[0] ? unique20 : unique00);
	snprintf(workgroup, grouplen, "%s", group00[0] ? group00 : group1e);
	return host[0] != '\0';
}

/* ---------------------------------------------------------------- GUID */

void mnm_guid(uint8_t out[36])
{
	static const char hex[] = "0123456789abcdef";
	uint8_t raw[16];
	if (getrandom(raw, sizeof(raw), 0) != (ssize_t)sizeof(raw)) {
		int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
		if (fd < 0 || read(fd, raw, sizeof(raw)) != (ssize_t)sizeof(raw))
			for (size_t i = 0; i < sizeof(raw); i++)
				raw[i] = (uint8_t)rand();
		if (fd >= 0)
			close(fd);
	}
	raw[6] = (uint8_t)((raw[6] & 0x0f) | 0x40);   /* version 4 (RFC 4122, 4.4) */
	raw[8] = (uint8_t)((raw[8] & 0x3f) | 0x80);   /* variant 10 */
	for (size_t i = 0, o = 0; i < 16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			out[o++] = '-';
		out[o++] = (uint8_t)hex[raw[i] >> 4];
		out[o++] = (uint8_t)hex[raw[i] & 15];
	}
}
