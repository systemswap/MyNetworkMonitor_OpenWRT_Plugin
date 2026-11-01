/* SPDX-License-Identifier: GPL-2.0-only */
#include "discover.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "packets.h"
#include "textdata.h"

/* Pause between two requests: 2000 per second keep a /24 under a fifth of a
 * second without flooding the segment or the router's own queues. */
#define SEND_INTERVAL_US 500

int mnm_targets_contain(const struct mnm_targets *targets, uint32_t address)
{
	for (size_t i = 0; i < targets->count; i++)
		if (address >= targets->ranges[i].first && address <= targets->ranges[i].last)
			return 1;
	return 0;
}

static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int stopped(const struct mnm_discover_options *o)
{
	return o->stop && *o->stop;
}

static void changed(struct mnm_devices *devices, const struct mnm_discover_options *o)
{
	if (devices->changed && o->on_change)
		o->on_change(o->context);
}

/* ------------------------------------------------------------------ ARP */

struct local_net {
	char name[IF_NAMESIZE];
	int ifindex;
	uint8_t mac[6];
	int has_mac;
	uint32_t address, mask;
};

/* The IPv4 networks of the router with interface index and MAC address. */
static size_t local_networks(struct local_net *nets, size_t max)
{
	struct ifaddrs *list, *ifa;
	size_t count = 0;

	if (getifaddrs(&list) != 0)
		return 0;
	for (ifa = list; ifa && count < max; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET || !ifa->ifa_netmask)
			continue;
		if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK))
			continue;
		struct local_net *n = &nets[count];
		memset(n, 0, sizeof(*n));
		snprintf(n->name, sizeof(n->name), "%s", ifa->ifa_name);
		n->address = ntohl(((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr);
		n->mask = ntohl(((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr.s_addr);
		n->ifindex = (int)if_nametoindex(ifa->ifa_name);
		count++;
	}
	/* The MAC address comes from the AF_PACKET entry of the same interface. */
	for (ifa = list; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_PACKET)
			continue;
		struct sockaddr_ll *ll = (struct sockaddr_ll *)ifa->ifa_addr;
		for (size_t i = 0; i < count; i++) {
			if (strcmp(nets[i].name, ifa->ifa_name) == 0 && ll->sll_halen == 6) {
				memcpy(nets[i].mac, ll->sll_addr, 6);
				nets[i].has_mac = 1;
			}
		}
	}
	freeifaddrs(list);
	return count;
}

_Static_assert(sizeof(((struct mnm_local_net *)0)->name) == IF_NAMESIZE, "interface names of the same size");

size_t mnm_local_networks_of(const struct mnm_targets *targets, struct mnm_local_net *out, size_t max)
{
	struct local_net nets[32];
	size_t count = local_networks(nets, 32), n = 0;

	for (size_t i = 0; i < count && n < max; i++) {
		uint32_t first = nets[i].address & nets[i].mask, last = first | ~nets[i].mask;
		int overlaps = 0;
		for (size_t r = 0; r < targets->count && !overlaps; r++)
			overlaps = targets->ranges[r].first <= last && targets->ranges[r].last >= first;
		if (!overlaps)
			continue;
		memcpy(out[n].name, nets[i].name, sizeof(out[n].name));
		out[n].name[sizeof(out[n].name) - 1] = '\0';
		out[n].address = nets[i].address;
		out[n].mask = nets[i].mask;
		n++;
	}
	return n;
}

static void handle_arp_frames(int fd, const struct mnm_targets *targets, struct mnm_devices *devices)
{
	uint8_t frame[128];

	for (;;) {
		ssize_t n = recv(fd, frame, sizeof(frame), MSG_DONTWAIT);
		if (n <= 0)
			return;
		uint32_t ip;
		uint8_t mac[6];
		if (!mnm_arp_parse_reply(frame, (size_t)n, &ip, mac) || !mnm_targets_contain(targets, ip))
			continue;
		struct mnm_device *d = mnm_devices_get(devices, ip);
		if (!d)
			return;
		mnm_devices_set_mac(devices, d, mac);
		mnm_devices_mark(devices, d, MNM_SEEN_ARP);
	}
}

/* ARP requests for all targets inside one local network. Returns 1 if stopped. */
static int arp_network(const struct local_net *net, const struct mnm_targets *targets, struct mnm_devices *devices,
		       const struct mnm_discover_options *o)
{
	uint32_t first = net->address & net->mask, last = first | ~net->mask;
	int fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ARP));
	struct sockaddr_ll sll;
	uint8_t frame[MNM_ARP_FRAME_LEN];

	if (fd < 0) {
		fprintf(stderr, "arp on %s: %s (needs root)\n", net->name, strerror(errno));
		return 0;
	}
	memset(&sll, 0, sizeof(sll));
	sll.sll_family = AF_PACKET;
	sll.sll_protocol = htons(ETH_P_ARP);
	sll.sll_ifindex = net->ifindex;
	if (bind(fd, (struct sockaddr *)&sll, sizeof(sll)) != 0) {
		close(fd);
		return 0;
	}
	sll.sll_halen = 6;
	memset(sll.sll_addr, 0xff, 6);

	for (uint32_t t = 0; t < targets->total; t++) {
		uint32_t ip = mnm_targets_at(targets, t);
		if (ip < first || ip > last || ip == net->address || ip == first || ip == last)
			continue;
		if (stopped(o)) {
			close(fd);
			return 1;
		}
		mnm_arp_build_request(frame, net->mac, net->address, ip);
		sendto(fd, frame, sizeof(frame), 0, (struct sockaddr *)&sll, sizeof(sll));
		handle_arp_frames(fd, targets, devices);
		changed(devices, o);
		usleep(SEND_INTERVAL_US);
	}

	int64_t end = now_ms() + o->timeout_ms;
	while (!stopped(o)) {
		int64_t left = end - now_ms();
		if (left <= 0)
			break;
		struct pollfd p = { .fd = fd, .events = POLLIN };
		if (poll(&p, 1, (int)(left > 200 ? 200 : left)) > 0)
			handle_arp_frames(fd, targets, devices);
		changed(devices, o);
	}
	close(fd);
	return stopped(o);
}

/* ----------------------------------------------------------------- ping */

static void handle_ping_replies(int fd, uint16_t id, const struct mnm_targets *targets, struct mnm_devices *devices)
{
	uint8_t buf[256];

	for (;;) {
		ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (n <= 0)
			return;
		uint32_t ip;
		uint8_t ttl;
		if (!mnm_icmp_parse_reply(buf, (size_t)n, id, &ip, &ttl) || !mnm_targets_contain(targets, ip))
			continue;
		struct mnm_device *d = mnm_devices_get(devices, ip);
		if (!d)
			return;
		if (d->ttl != ttl) {
			d->ttl = ttl;
			devices->changed = 1;
		}
		mnm_devices_mark(devices, d, MNM_SEEN_PING);
	}
}

static int ping_all(const struct mnm_targets *targets, struct mnm_devices *devices,
		    const struct mnm_discover_options *o)
{
	int fd = socket(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_ICMP);
	uint16_t id = (uint16_t)getpid();
	uint8_t packet[MNM_ICMP_ECHO_LEN];

	if (fd < 0) {
		fprintf(stderr, "ping: %s (needs root)\n", strerror(errno));
		return 0;
	}
	for (uint32_t t = 0; t < targets->total; t++) {
		if (stopped(o)) {
			close(fd);
			return 1;
		}
		struct sockaddr_in sa = { .sin_family = AF_INET };
		sa.sin_addr.s_addr = htonl(mnm_targets_at(targets, t));
		mnm_icmp_build_echo(packet, id, (uint16_t)t);
		sendto(fd, packet, sizeof(packet), 0, (struct sockaddr *)&sa, sizeof(sa));
		handle_ping_replies(fd, id, targets, devices);
		changed(devices, o);
		usleep(SEND_INTERVAL_US);
	}

	int64_t end = now_ms() + o->timeout_ms;
	while (!stopped(o)) {
		int64_t left = end - now_ms();
		if (left <= 0)
			break;
		struct pollfd p = { .fd = fd, .events = POLLIN };
		if (poll(&p, 1, (int)(left > 200 ? 200 : left)) > 0)
			handle_ping_replies(fd, id, targets, devices);
		changed(devices, o);
	}
	close(fd);
	return stopped(o);
}

int mnm_discover_arp_ping(const struct mnm_targets *targets, struct mnm_devices *devices,
			  const struct mnm_discover_options *options)
{
	struct local_net nets[32];
	size_t count = local_networks(nets, 32);

	for (size_t i = 0; i < count; i++) {
		uint32_t first = nets[i].address & nets[i].mask, last = first | ~nets[i].mask;
		int overlaps = 0;
		/* Only networks of /16 and smaller: ARP is answered only in the own
		 * segment, and a larger network is no single segment in practice. */
		if (!nets[i].has_mac || nets[i].ifindex <= 0 || nets[i].mask < 0xffff0000u)
			continue;
		for (size_t r = 0; r < targets->count && !overlaps; r++)
			overlaps = targets->ranges[r].first <= last && targets->ranges[r].last >= first;
		if (overlaps && arp_network(&nets[i], targets, devices, options))
			return 1;
	}
	return options->ping ? ping_all(targets, devices, options) : 0;
}

/* ------------------------------------------------------ neighbour table */

void mnm_discover_neighbors(const struct mnm_targets *targets, struct mnm_devices *devices)
{
	int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	struct {
		struct nlmsghdr nh;
		struct ndmsg nd;
	} req;
	static uint8_t buf[16384];

	if (fd < 0)
		return;
	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ndmsg));
	req.nh.nlmsg_type = RTM_GETNEIGH;
	req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	req.nd.ndm_family = AF_UNSPEC;
	if (send(fd, &req, req.nh.nlmsg_len, 0) < 0) {
		close(fd);
		return;
	}

	/* IPv6 entries are attached by MAC address, so they are kept until all
	 * IPv4 entries are known. */
	struct {
		uint8_t mac[6];
		struct in6_addr address;
	} v6[256];
	size_t v6_count = 0;

	for (int done = 0; !done;) {
		struct pollfd p = { .fd = fd, .events = POLLIN };
		if (poll(&p, 1, 1000) <= 0)
			break;
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n <= 0)
			break;
		for (struct nlmsghdr *nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, (size_t)n); nh = NLMSG_NEXT(nh, n)) {
			if (nh->nlmsg_type == NLMSG_DONE || nh->nlmsg_type == NLMSG_ERROR) {
				done = 1;
				break;
			}
			if (nh->nlmsg_type != RTM_NEWNEIGH || nh->nlmsg_len < NLMSG_LENGTH(sizeof(struct ndmsg)))
				continue;
			struct ndmsg *nd = NLMSG_DATA(nh);
			/* Entries that never got an answer carry no usable MAC address. */
			if (nd->ndm_state & (NUD_INCOMPLETE | NUD_FAILED | NUD_NOARP))
				continue;
			const uint8_t *dst = NULL, *lladdr = NULL;
			size_t dst_len = 0, ll_len = 0;
			int len = (int)(nh->nlmsg_len - NLMSG_LENGTH(sizeof(*nd)));
			for (struct rtattr *a = (struct rtattr *)((char *)nd + NLMSG_ALIGN(sizeof(*nd))); RTA_OK(a, len);
			     a = RTA_NEXT(a, len)) {
				if (a->rta_type == NDA_DST) {
					dst = RTA_DATA(a);
					dst_len = RTA_PAYLOAD(a);
				} else if (a->rta_type == NDA_LLADDR) {
					lladdr = RTA_DATA(a);
					ll_len = RTA_PAYLOAD(a);
				}
			}
			if (!dst || !lladdr || ll_len != 6)
				continue;
			if (nd->ndm_family == AF_INET && dst_len == 4) {
				uint32_t ip = (uint32_t)dst[0] << 24 | (uint32_t)dst[1] << 16 | (uint32_t)dst[2] << 8 | dst[3];
				if (!mnm_targets_contain(targets, ip))
					continue;
				struct mnm_device *d = mnm_devices_get(devices, ip);
				if (!d)
					continue;
				mnm_devices_set_mac(devices, d, lladdr);
				mnm_devices_mark(devices, d, MNM_SEEN_NEIGHBOR);
			} else if (nd->ndm_family == AF_INET6 && dst_len == 16 && v6_count < 256) {
				memcpy(v6[v6_count].mac, lladdr, 6);
				memcpy(&v6[v6_count].address, dst, 16);
				v6_count++;
			}
		}
	}
	close(fd);
	for (size_t i = 0; i < v6_count; i++)
		mnm_devices_add_ipv6(devices, v6[i].mac, &v6[i].address);
}

/* ------------------------------------------------------- leases, vendors */

void mnm_discover_leases(const char *path, struct mnm_devices *devices)
{
	FILE *f = path ? fopen(path, "r") : NULL;
	char line[512], name[MNM_NAME_LEN];
	uint8_t mac[6];
	uint32_t ip;

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		if (!mnm_lease_parse_line(line, mac, &ip, name, sizeof(name)))
			continue;
		for (size_t i = 0; i < devices->count; i++) {
			struct mnm_device *d = &devices->list[i];
			if (d->ipv4 != ip && !(d->has_mac && memcmp(d->mac, mac, 6) == 0))
				continue;
			if (name[0] && strcmp(d->hostname, name) != 0) {
				snprintf(d->hostname, sizeof(d->hostname), "%s", name);
				devices->changed = 1;
			}
			mnm_devices_mark(devices, d, MNM_SEEN_LEASE);
		}
	}
	fclose(f);
}

void mnm_discover_vendors(const char *path, struct mnm_devices *devices)
{
	FILE *f = path ? fopen(path, "r") : NULL;
	char line[512], name[MNM_VENDOR_LEN];
	uint64_t prefix;
	unsigned int bits;

	if (!f || devices->count == 0) {
		if (f)
			fclose(f);
		return;
	}
	unsigned int *best = calloc(devices->count, sizeof(*best));
	if (!best) {
		fclose(f);
		return;
	}
	while (fgets(line, sizeof(line), f)) {
		if (!mnm_vendor_parse_line(line, &prefix, &bits, name, sizeof(name)))
			continue;
		uint64_t mask = (~0ull << (48 - bits)) & 0xffffffffffffull;
		for (size_t i = 0; i < devices->count; i++) {
			struct mnm_device *d = &devices->list[i];
			if (!d->has_mac || bits <= best[i] || (mnm_mac_value(d->mac) & mask) != prefix)
				continue;
			best[i] = bits;
			if (strcmp(d->vendor, name) != 0) {
				snprintf(d->vendor, sizeof(d->vendor), "%s", name);
				devices->changed = 1;
			}
		}
	}
	free(best);
	fclose(f);
}
