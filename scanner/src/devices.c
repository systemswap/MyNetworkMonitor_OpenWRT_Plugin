/* SPDX-License-Identifier: GPL-2.0-only */
#include "devices.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "packets.h"
#include "targets.h"
#include "textdata.h"

struct mnm_device *mnm_devices_find(struct mnm_devices *devices, uint32_t ipv4)
{
	for (size_t i = 0; i < devices->count; i++)
		if (devices->list[i].ipv4 == ipv4)
			return &devices->list[i];
	return NULL;
}

struct mnm_device *mnm_devices_get(struct mnm_devices *devices, uint32_t ipv4)
{
	struct mnm_device *d = mnm_devices_find(devices, ipv4);
	if (d)
		return d;
	if (devices->count == devices->capacity) {
		size_t grown = devices->capacity ? devices->capacity * 2 : 32;
		struct mnm_device *l = realloc(devices->list, grown * sizeof(*l));
		if (!l)
			return NULL;
		devices->list = l;
		devices->capacity = grown;
	}
	d = &devices->list[devices->count++];
	memset(d, 0, sizeof(*d));
	d->ipv4 = ipv4;
	devices->changed = 1;
	return d;
}

void mnm_devices_set_mac(struct mnm_devices *devices, struct mnm_device *device, const uint8_t mac[6])
{
	if (device->has_mac && memcmp(device->mac, mac, 6) == 0)
		return;
	memcpy(device->mac, mac, 6);
	device->has_mac = 1;
	devices->changed = 1;
}

void mnm_devices_add_ipv6(struct mnm_devices *devices, const uint8_t mac[6], const struct in6_addr *address)
{
	for (size_t i = 0; i < devices->count; i++) {
		struct mnm_device *d = &devices->list[i];
		if (!d->has_mac || memcmp(d->mac, mac, 6) != 0)
			continue;
		for (int k = 0; k < d->ipv6_count; k++)
			if (memcmp(&d->ipv6[k], address, sizeof(*address)) == 0)
				return;
		if (d->ipv6_count < MNM_MAX_IPV6) {
			d->ipv6[d->ipv6_count++] = *address;
			devices->changed = 1;
		}
		return;
	}
}

void mnm_devices_add_tcp(struct mnm_devices *devices, struct mnm_device *device, uint16_t port)
{
	for (size_t i = 0; i < device->tcp_count; i++)
		if (device->tcp[i] == port)
			return;
	if (device->tcp_count == device->tcp_capacity) {
		size_t grown = device->tcp_capacity ? device->tcp_capacity * 2 : 8;
		uint16_t *p = realloc(device->tcp, grown * sizeof(*p));
		if (!p)
			return;
		device->tcp = p;
		device->tcp_capacity = grown;
	}
	device->tcp[device->tcp_count++] = port;
	devices->changed = 1;
}

void mnm_devices_add_service(struct mnm_devices *devices, struct mnm_device *device, const char *id, const char *name,
			     uint16_t port, int udp, const char *detail)
{
	for (int i = 0; i < device->service_count; i++)
		if (device->services[i].port == port && strcmp(device->services[i].id, id) == 0)
			return;
	if (device->service_count >= MNM_MAX_SERVICES)
		return;
	struct mnm_service *s = &device->services[device->service_count++];
	s->id = id;
	s->name = name;
	s->port = port;
	s->udp = udp;
	snprintf(s->detail, sizeof(s->detail), "%s", detail ? detail : "");
	devices->changed = 1;
}

void mnm_devices_mark(struct mnm_devices *devices, struct mnm_device *device, unsigned int seen)
{
	if ((device->seen & seen) == seen)
		return;
	device->seen |= seen;
	devices->changed = 1;
}

static int compare_devices(const void *a, const void *b)
{
	uint32_t x = (*(const struct mnm_device *const *)a)->ipv4, y = (*(const struct mnm_device *const *)b)->ipv4;
	return x < y ? -1 : x > y;
}

static int compare_ports(const void *a, const void *b)
{
	return (int)*(const uint16_t *)a - (int)*(const uint16_t *)b;
}

static void write_string_field(FILE *out, const char *name, const char *value)
{
	fprintf(out, ",\"%s\":", name);
	mnm_json_string(out, value, strlen(value));
}

void mnm_devices_write_json(struct mnm_devices *devices, FILE *out)
{
	static const struct {
		unsigned int bit;
		const char *name;
	} sources[] = {
		{ MNM_SEEN_ARP, "arp" }, { MNM_SEEN_PING, "ping" }, { MNM_SEEN_NEIGHBOR, "neighbor" },
		{ MNM_SEEN_LEASE, "lease" }, { MNM_SEEN_TCP, "tcp" },
	};

	/* Sorted through an index: the list itself keeps its order, so that
	 * callers may keep a position in it while the scan goes on. */
	struct mnm_device **order = malloc((devices->count ? devices->count : 1) * sizeof(*order));
	if (!order) {
		fputs("[]", out);
		return;
	}
	for (size_t i = 0; i < devices->count; i++)
		order[i] = &devices->list[i];
	qsort(order, devices->count, sizeof(*order), compare_devices);

	fputc('[', out);
	for (size_t i = 0; i < devices->count; i++) {
		struct mnm_device *d = order[i];
		char text[INET6_ADDRSTRLEN];

		mnm_format_ipv4(d->ipv4, text);
		fprintf(out, "%s{\"ipv4\":\"%s\"", i ? "," : "", text);
		if (d->has_mac) {
			mnm_format_mac(d->mac, text);
			write_string_field(out, "mac", text);
		}
		if (d->hostname[0])
			write_string_field(out, "hostname", d->hostname);
		if (d->vendor[0])
			write_string_field(out, "vendor", d->vendor);
		if (d->ttl) {
			fprintf(out, ",\"ttl\":%u", d->ttl);
			write_string_field(out, "os_guess", mnm_ttl_guess(d->ttl));
		}
		fputs(",\"ipv6\":[", out);
		for (int k = 0; k < d->ipv6_count; k++) {
			inet_ntop(AF_INET6, &d->ipv6[k], text, sizeof(text));
			fprintf(out, "%s\"%s\"", k ? "," : "", text);
		}
		fputs("],\"seen_by\":[", out);
		int first = 1;
		for (size_t k = 0; k < sizeof(sources) / sizeof(sources[0]); k++) {
			if (d->seen & sources[k].bit) {
				fprintf(out, "%s\"%s\"", first ? "" : ",", sources[k].name);
				first = 0;
			}
		}
		fprintf(out, "],\"ports_scanned\":%s,\"tcp\":[", d->ports_scanned ? "true" : "false");
		if (d->tcp_count > 1)   /* qsort with no array (NULL) is undefined, even for zero elements */
			qsort(d->tcp, d->tcp_count, sizeof(*d->tcp), compare_ports);
		for (size_t k = 0; k < d->tcp_count; k++)
			fprintf(out, "%s%u", k ? "," : "", d->tcp[k]);
		fprintf(out, "],\"services_checked\":%s,\"services\":[", d->services_checked ? "true" : "false");
		for (int k = 0; k < d->service_count; k++) {
			fprintf(out, "%s{\"id\":", k ? "," : "");
			mnm_json_string(out, d->services[k].id, strlen(d->services[k].id));
			write_string_field(out, "name", d->services[k].name);
			fprintf(out, ",\"port\":%u", d->services[k].port);
			if (d->services[k].udp)
				fputs(",\"udp\":true", out);
			if (d->services[k].detail[0])
				write_string_field(out, "detail", d->services[k].detail);
			fputc('}', out);
		}
		fputs("]}", out);
	}
	fputc(']', out);
	free(order);
	devices->changed = 0;
}

void mnm_devices_free(struct mnm_devices *devices)
{
	for (size_t i = 0; i < devices->count; i++)
		free(devices->list[i].tcp);
	free(devices->list);
	devices->list = NULL;
	devices->count = devices->capacity = 0;
}
