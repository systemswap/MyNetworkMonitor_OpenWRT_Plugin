/* SPDX-License-Identifier: GPL-2.0-only */
#include "bc_discover.h"

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>

#include "bc_packets.h"
#include "bc_parse.h"
#include "discover.h"
#include "http_get.h"
#include "netio.h"

/* Listening window from the first packet: the last ONVIF probe goes out after
 * 1 s, and the desktop program listens 5 s after its last send. */
#define WINDOW_MS 6500
#define MAX_NETS 16
#define MAX_FOUND 1024
#define DESCRIPTION_MAX (64 * 1024)

#define SSDP_GROUP 0xeffffffau   /* 239.255.255.250 */
#define MDNS_GROUP 0xe00000fbu   /* 224.0.0.251 */

int mnm_bc_parse_methods(const char *text, unsigned int *methods, char *err, size_t errlen)
{
	static const struct { const char *name; unsigned int bit; } names[] = {
		{ "ssdp", MNM_BC_SSDP }, { "wsd", MNM_BC_WSD }, { "onvif", MNM_BC_ONVIF },
		{ "mdns", MNM_BC_MDNS }, { "netbios", MNM_BC_NETBIOS },
	};
	*methods = 0;
	for (const char *p = text; *p;) {
		const char *e = strchr(p, ',');
		size_t n = e ? (size_t)(e - p) : strlen(p), k;
		for (k = 0; k < sizeof(names) / sizeof(names[0]); k++)
			if (n == strlen(names[k].name) && strncmp(p, names[k].name, n) == 0)
				break;
		if (n && k == sizeof(names) / sizeof(names[0])) {
			snprintf(err, errlen, "unknown method \"%.*s\"", (int)(n < 20 ? n : 20), p);
			return -1;
		}
		if (n)
			*methods |= names[k].bit;
		p += n + (e ? 1 : 0);
	}
	return 0;
}

/* ----------------------------------------------------- multicast searches */

enum kind { K_SSDP, K_WSD, K_ONVIF, K_MDNS };

struct sock {
	int fd;
	enum kind kind;
};

/* A packet to send at a time after the start. */
struct event {
	int at_ms;
	int sock;
	const uint8_t *data;
	size_t len;
	size_t guid_at;      /* where the message ID goes; 0 for none */
	int fresh_guid;      /* a new ID for this packet, else the one of the run */
	uint32_t interface;  /* mDNS: the address of the network to send from */
	uint32_t group;
	uint16_t port;
};

/* What one address answered. */
struct found {
	uint32_t address;
	unsigned int seen;
	char location[256], server[128];
	char wsd[MNM_DETAIL_LEN], onvif[MNM_DETAIL_LEN];
	struct mnm_mdns_info mdns;
};

static int multicast_socket(uint32_t bind_address, uint16_t port, uint32_t interface, int loop, int reuse)
{
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(bind_address) };
	struct in_addr ifaddr = { .s_addr = htonl(interface) };
	unsigned char l = (unsigned char)loop;
	int on = 1, fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
	if (reuse) {
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef SO_REUSEPORT
		setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
	}
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
	    (interface && setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ifaddr, sizeof(ifaddr)) != 0) ||
	    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &l, sizeof(l)) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static struct found *found_for(struct found **list, size_t *count, uint32_t address)
{
	for (size_t i = 0; i < *count; i++)
		if ((*list)[i].address == address)
			return &(*list)[i];
	if (*count >= MAX_FOUND)
		return NULL;
	struct found *grown = realloc(*list, (*count + 1) * sizeof(**list));
	if (!grown)
		return NULL;
	*list = grown;
	struct found *f = &grown[(*count)++];
	memset(f, 0, sizeof(*f));
	f->address = address;
	return f;
}

/* Takes one answer. Only addresses inside the targets count, and never the
 * router itself (its own mDNS queries come back to the group). */
static void take(enum kind kind, uint32_t source, const uint8_t *buf, size_t len, const struct mnm_targets *targets,
		 const struct mnm_local_net *nets, size_t net_count, struct found **list, size_t *count)
{
	for (size_t i = 0; i < net_count; i++)
		if (nets[i].address == source)
			return;
	if (!mnm_targets_contain(targets, source))
		return;
	struct mnm_mdns_info probe;
	if (kind == K_MDNS) {
		/* only responses: other devices' queries are no answer */
		memset(&probe, 0, sizeof(probe));
		if (!mnm_mdns_parse(buf, len, source, &probe))
			return;
	}
	struct found *f = found_for(list, count, source);
	if (!f)
		return;
	const char *text = (const char *)buf;
	switch (kind) {
	case K_SSDP:
		/* the first answer of an address counts, as in the desktop program */
		if (!(f->seen & MNM_SEEN_SSDP)) {
			mnm_ssdp_header(text, len, "LOCATION", f->location, sizeof(f->location));
			mnm_ssdp_header(text, len, "SERVER", f->server, sizeof(f->server));
		}
		f->seen |= MNM_SEEN_SSDP;
		break;
	case K_WSD:
		if (!(f->seen & MNM_SEEN_WSD))
			mnm_wsd_describe(text, len, f->wsd, sizeof(f->wsd));
		f->seen |= MNM_SEEN_WSD;
		break;
	case K_ONVIF:
		if (!(f->seen & MNM_SEEN_ONVIF) && mnm_onvif_describe(text, len, f->onvif, sizeof(f->onvif)))
			f->seen |= MNM_SEEN_ONVIF;
		break;
	case K_MDNS:
		mnm_mdns_parse(buf, len, source, &f->mdns);
		f->seen |= MNM_SEEN_MDNS;
		break;
	}
}

/* The detail of an SSDP device: its UPnP description, fetched from the
 * LOCATION URL - only from the device itself (the URL may name any host) and
 * only over plain HTTP; else the SERVER header. */
static void ssdp_detail(const struct found *f, char *detail, size_t len)
{
	char host[64], path[256], dotted[16];
	uint16_t port;
	struct in_addr a = { .s_addr = htonl(f->address) };
	inet_ntop(AF_INET, &a, dotted, sizeof(dotted));
	detail[0] = '\0';
	if (f->location[0] && mnm_url_split(f->location, host, sizeof(host), &port, path, sizeof(path)) &&
	    strcmp(host, dotted) == 0) {
		char *body = malloc(DESCRIPTION_MAX);
		ssize_t n = body ? mnm_http_get(f->address, port, host, path, 5000, body, DESCRIPTION_MAX) : -1;
		if (n > 0)
			mnm_upnp_describe(body, (size_t)n, detail, len);
		free(body);
	}
	if (!detail[0] && f->server[0])
		snprintf(detail, len, "Server: %s", f->server);
	if (!detail[0])
		snprintf(detail, len, "Replied.");
}

static void report(struct mnm_devices *devices, const struct found *f, const struct mnm_bc_options *o)
{
	char ssdp[MNM_DETAIL_LEN] = "", mdns[MNM_DETAIL_LEN] = "";
	if (f->seen & MNM_SEEN_SSDP)
		ssdp_detail(f, ssdp, sizeof(ssdp));
	if (f->seen & MNM_SEEN_MDNS)
		mnm_mdns_describe(&f->mdns, mdns, sizeof(mdns));
	/* only now the device: the list may move while it grows */
	struct mnm_device *d = mnm_devices_get(devices, f->address);
	if (!d)
		return;
	mnm_devices_mark(devices, d, f->seen);
	if (f->seen & MNM_SEEN_SSDP)
		mnm_devices_add_service(devices, d, "ssdp", "UPnP", 1900, 1, ssdp);
	if (f->seen & MNM_SEEN_WSD)
		mnm_devices_add_service(devices, d, "wsd", "WS-Discovery", 3702, 1, f->wsd);
	if (f->seen & MNM_SEEN_ONVIF)
		mnm_devices_add_service(devices, d, "onvif", "ONVIF", 3702, 1, f->onvif);
	if (f->seen & MNM_SEEN_MDNS) {
		mnm_devices_add_service(devices, d, "mdns", "mDNS", 5353, 1, mdns);
		if (!d->hostname[0] && f->mdns.host[0]) {
			snprintf(d->hostname, sizeof(d->hostname), "%s", f->mdns.host);
			devices->changed = 1;
		}
	}
	if (o->on_change)
		o->on_change(o->context);
}

int mnm_bc_discover(const struct mnm_targets *targets, struct mnm_devices *devices, const struct mnm_bc_options *o)
{
	struct mnm_local_net nets[MAX_NETS];
	struct sock socks[MAX_NETS * 3 + 1];
	struct pollfd pf[MAX_NETS * 3 + 1];
	struct event *events;
	struct found *found = NULL;
	size_t net_count, sock_count = 0, event_count = 0, found_count = 0;
	uint8_t run_guid[MNM_GUID_LEN], packet[1024];
	static uint8_t buf[9000];
	int stopped = 0;

	if (!(o->methods & (MNM_BC_SSDP | MNM_BC_WSD | MNM_BC_ONVIF | MNM_BC_MDNS)))
		return 0;
	net_count = mnm_local_networks_of(targets, nets, MAX_NETS);
	events = calloc(MAX_NETS * (1 + 6 + 3 + MNM_MDNS_QUERIES), sizeof(*events));
	if (!net_count || !events) {
		free(events);
		return 0;
	}
	mnm_guid(run_guid);

	int mdns = -1;
	if (o->methods & MNM_BC_MDNS) {
		/* port 5353 shared with a responder of the router, if there is one */
		int fd = multicast_socket(0, 5353, 0, 0, 1);
		if (fd < 0)
			fprintf(stderr, "mdns: port 5353: %s\n", strerror(errno));
		else {
			socks[sock_count] = (struct sock){ fd, K_MDNS };
			mdns = (int)sock_count++;
		}
	}
	for (size_t n = 0; n < net_count; n++) {
		uint32_t a = nets[n].address;
		if (o->methods & MNM_BC_SSDP) {
			int fd = multicast_socket(0, 0, a, 1, 0);
			if (fd >= 0) {
				events[event_count++] = (struct event){ 0, (int)sock_count, pkt_ssdp_msearch,
					sizeof(pkt_ssdp_msearch), 0, 0, 0, SSDP_GROUP, 1900 };
				socks[sock_count++] = (struct sock){ fd, K_SSDP };
			}
		}
		if (o->methods & MNM_BC_WSD) {
			/* bound to the address of the network: the answers come back to it */
			int fd = multicast_socket(a, 0, a, 0, 0);
			if (fd >= 0) {
				for (int i = 0; i < 6; i++)
					events[event_count++] = (struct event){ i * 200, (int)sock_count,
						i < 3 ? pkt_wsd_probe_2009 : pkt_wsd_probe_2005, sizeof(pkt_wsd_probe_2009),
						i < 3 ? MNM_WSD_2009_GUID : MNM_WSD_2005_GUID, 1, 0, SSDP_GROUP, 3702 };
				socks[sock_count++] = (struct sock){ fd, K_WSD };
			}
		}
		if (o->methods & MNM_BC_ONVIF) {
			int fd = multicast_socket(0, 0, a, 0, 0);
			if (fd >= 0) {
				for (int i = 0; i < 3; i++)
					events[event_count++] = (struct event){ i * 500, (int)sock_count, pkt_onvif_probe,
						sizeof(pkt_onvif_probe), MNM_ONVIF_GUID, 0, 0, SSDP_GROUP, 3702 };
				socks[sock_count++] = (struct sock){ fd, K_ONVIF };
			}
		}
		if (mdns >= 0) {
			struct ip_mreq m = { .imr_multiaddr.s_addr = htonl(MDNS_GROUP), .imr_interface.s_addr = htonl(a) };
			setsockopt(socks[mdns].fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m));
			for (int i = 0; i < MNM_MDNS_QUERIES; i++)
				events[event_count++] = (struct event){ i * 100, mdns, mnm_mdns_queries[i].data,
					mnm_mdns_queries[i].len, 0, 0, a, MDNS_GROUP, 5353 };
		}
	}

	int64_t start = mnm_now_ms(), end = start + WINDOW_MS;
	while (!stopped) {
		int64_t now = mnm_now_ms();
		if (o->stop && *o->stop) {
			stopped = 1;
			break;
		}
		if (now >= end)
			break;
		/* the events are in time order per kind, not overall: send all that are due */
		int64_t wait = end - now;
		for (size_t i = 0; i < event_count; i++) {
			struct event *e = &events[i];
			if (!e->data)
				continue;
			if (start + e->at_ms > now) {
				if (start + e->at_ms - now < wait)
					wait = start + e->at_ms - now;
				continue;
			}
			const uint8_t *data = e->data;
			if (e->guid_at && e->len <= sizeof(packet)) {
				memcpy(packet, e->data, e->len);
				if (e->fresh_guid)
					mnm_guid(packet + e->guid_at);
				else
					memcpy(packet + e->guid_at, run_guid, MNM_GUID_LEN);
				data = packet;
			}
			if (e->interface) {
				struct in_addr ifaddr = { .s_addr = htonl(e->interface) };
				setsockopt(socks[e->sock].fd, IPPROTO_IP, IP_MULTICAST_IF, &ifaddr, sizeof(ifaddr));
			}
			struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(e->port),
						  .sin_addr.s_addr = htonl(e->group) };
			sendto(socks[e->sock].fd, data, e->len, 0, (struct sockaddr *)&to, sizeof(to));
			e->data = NULL;
		}
		for (size_t i = 0; i < sock_count; i++)
			pf[i] = (struct pollfd){ .fd = socks[i].fd, .events = POLLIN };
		if (poll(pf, (nfds_t)sock_count, (int)(wait < 100 ? wait : 100)) <= 0)
			continue;
		for (size_t i = 0; i < sock_count; i++) {
			for (int k = 0; k < 64 && (pf[i].revents & POLLIN); k++) {
				struct sockaddr_in from;
				socklen_t flen = sizeof(from);
				ssize_t n = recvfrom(socks[i].fd, buf, sizeof(buf) - 1, MSG_DONTWAIT, (struct sockaddr *)&from, &flen);
				if (n <= 0)
					break;
				buf[n] = '\0';
				take(socks[i].kind, ntohl(from.sin_addr.s_addr), buf, (size_t)n, targets, nets, net_count, &found,
				     &found_count);
			}
		}
	}
	for (size_t i = 0; i < sock_count; i++)
		close(socks[i].fd);
	free(events);

	for (size_t i = 0; i < found_count && !stopped; i++) {
		if (o->stop && *o->stop)
			stopped = 1;
		else
			report(devices, &found[i], o);
	}
	free(found);
	return stopped;
}

/* -------------------------------------------------------------- NetBIOS */

#define NETBIOS_PARALLEL 50
#define NETBIOS_WAIT_MS 5000

struct nb_query {
	uint32_t address;
	int fd, attempt, done;
	uint16_t id;
	int64_t deadline;
	char host[16], workgroup[16];
};

static uint16_t random_id(void)
{
	uint16_t id = 0;
	if (getrandom(&id, sizeof(id), 0) != (ssize_t)sizeof(id))
		id = (uint16_t)rand();
	return id ? id : 1;   /* 1 to 65535, as in the desktop program */
}

/* Starts the next attempt on a new socket with a new transaction ID; after
 * the second, the query is over. */
static void nb_attempt(struct nb_query *q)
{
	uint8_t packet[sizeof(pkt_netbios_nbstat)];
	if (q->fd >= 0)
		close(q->fd);
	q->fd = -1;
	if (++q->attempt > 2) {
		q->done = 1;
		return;
	}
	q->fd = mnm_udp_socket(q->address, 137);
	if (q->fd < 0) {
		q->done = 1;
		return;
	}
	q->id = random_id();
	memcpy(packet, pkt_netbios_nbstat, sizeof(packet));
	packet[MNM_NETBIOS_ID] = (uint8_t)(q->id >> 8);
	packet[MNM_NETBIOS_ID + 1] = (uint8_t)q->id;
	if (send(q->fd, packet, sizeof(packet), 0) != (ssize_t)sizeof(packet)) {
		/* a failed send ends the query, as in the desktop program */
		close(q->fd);
		q->fd = -1;
		q->done = 1;
		return;
	}
	q->deadline = mnm_now_ms() + NETBIOS_WAIT_MS;
}

int mnm_bc_netbios(struct mnm_devices *devices, const struct mnm_bc_options *o)
{
	struct nb_query q[NETBIOS_PARALLEL];
	struct pollfd pf[NETBIOS_PARALLEL];
	uint8_t buf[1024];
	size_t total = devices->count;
	int stopped = 0;

	if (!(o->methods & MNM_BC_NETBIOS))
		return 0;
	for (size_t start = 0; start < total && !stopped; start += NETBIOS_PARALLEL) {
		size_t n = total - start < NETBIOS_PARALLEL ? total - start : NETBIOS_PARALLEL, active;
		for (size_t i = 0; i < n; i++) {
			q[i] = (struct nb_query){ .address = devices->list[start + i].ipv4, .fd = -1 };
			nb_attempt(&q[i]);
		}
		for (;;) {
			int64_t now = mnm_now_ms(), wait = NETBIOS_WAIT_MS;
			size_t index[NETBIOS_PARALLEL];
			active = 0;
			for (size_t i = 0; i < n; i++) {
				if (q[i].done)
					continue;
				if (now >= q[i].deadline) {
					nb_attempt(&q[i]);
					if (q[i].done)
						continue;
				}
				pf[active] = (struct pollfd){ .fd = q[i].fd, .events = POLLIN };
				index[active++] = i;
				if (q[i].deadline - now < wait)
					wait = q[i].deadline - now;
			}
			if (active == 0)
				break;
			if (o->stop && *o->stop) {
				stopped = 1;
				break;
			}
			if (poll(pf, (nfds_t)active, (int)(wait < 200 ? wait : 200)) <= 0)
				continue;
			for (size_t k = 0; k < active; k++) {
				struct nb_query *x = &q[index[k]];
				if (!pf[k].revents)
					continue;
				ssize_t len = recv(x->fd, buf, sizeof(buf), MSG_DONTWAIT);
				if (len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
					continue;
				/* an answer that is not valid, or "port unreachable": the next attempt */
				if (len > 0 && mnm_netbios_parse(buf, (size_t)len, x->id, x->host, sizeof(x->host), x->workgroup,
								 sizeof(x->workgroup))) {
					close(x->fd);
					x->fd = -1;
					x->done = 2;
				} else {
					nb_attempt(x);
				}
			}
		}
		for (size_t i = 0; i < n; i++) {
			if (q[i].fd >= 0)
				close(q[i].fd);
			if (q[i].done != 2)
				continue;
			struct mnm_device *d = mnm_devices_find(devices, q[i].address);
			char detail[64];
			if (!d)
				continue;
			if (q[i].workgroup[0])
				snprintf(detail, sizeof(detail), "%s (workgroup/domain: %s)", q[i].host, q[i].workgroup);
			else
				snprintf(detail, sizeof(detail), "%s", q[i].host);
			mnm_devices_mark(devices, d, MNM_SEEN_NETBIOS);
			mnm_devices_add_service(devices, d, "netbios", "NetBIOS", 137, 1, detail);
			if (!d->hostname[0]) {
				snprintf(d->hostname, sizeof(d->hostname), "%s", q[i].host);
				devices->changed = 1;
			}
		}
		if (o->on_change)
			o->on_change(o->context);
	}
	return stopped;
}
