/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tests of the UDP probes: the evaluations on made-up answers, and complete
 * runs against small UDP servers on the loopback interface that play a name
 * server, a BACnet device and an SQL Server Browser - and against a closed
 * port, where the "port unreachable" answer must not count as a finding.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../src/probes.h"
#include "../src/probes_udp.h"
#include "check.h"

enum udp_role { NAME_SERVER, BACNET, BROWSER };

struct udp_server {
	int fd;
	enum udp_role role;
	int requests;
};

/* A BACnet answer whose property is a character string "<text>". */
static size_t bacnet_string(uint8_t *out, const char *text)
{
	size_t n = strlen(text), len = 20 + 3 + n;
	memset(out, 0, len);
	out[0] = 0x81, out[1] = 0x0a, out[2] = (uint8_t)(len >> 8), out[3] = (uint8_t)len;
	out[4] = 0x01, out[6] = 0x30;                               /* NPDU, ComplexACK */
	out[20] = 0x75, out[21] = (uint8_t)(n + 1), out[22] = 0x00;   /* tag, length, character set */
	memcpy(out + 23, text, n);
	return len;
}

static void *serve_udp(void *arg)
{
	struct udp_server *s = arg;
	for (;;) {
		struct pollfd p = { .fd = s->fd, .events = POLLIN };
		uint8_t req[600], out[600];
		struct sockaddr_in from;
		socklen_t flen = sizeof(from);
		size_t n = 0;
		if (poll(&p, 1, 1500) <= 0)
			return NULL;
		ssize_t got = recvfrom(s->fd, req, sizeof(req), 0, (struct sockaddr *)&from, &flen);
		if (got <= 0)
			return NULL;
		s->requests++;
		if (s->role == NAME_SERVER) {
			/* the first query goes unanswered: the probe must ask again */
			if (s->requests == 1)
				continue;
			memcpy(out, req, (size_t)got);
			out[2] |= 0x80, out[3] = 0x83;   /* response, NXDOMAIN */
			n = (size_t)got;
		} else if (s->role == BACNET) {
			uint8_t property = req[got - 1];
			if (property == 0x4b) {
				memset(out, 0, 24);
				out[0] = 0x81, out[1] = 0x0a, out[3] = 24, out[4] = 0x01, out[6] = 0x30;
				out[19] = 0x02, out[20] = 0x01, out[21] = 0x02;   /* instance 0x020102 = 131330 */
				n = 24;
			} else if (property == 0x79) {
				n = bacnet_string(out, "Example Controls");
			} else if (property == 0x4d) {
				n = bacnet_string(out, "AHU-3");
			} else {
				/* a Reject: BACnet, but without a value */
				static const uint8_t reject[] = { 0x81, 0x0a, 0x00, 0x09, 0x01, 0x00, 0x60, 0x01, 0x09 };
				memcpy(out, reject, sizeof(reject));
				n = sizeof(reject);
			}
		} else {
			static const char answer[] = "\x05\x60\x00ServerName;SRV1;InstanceName;SQLEXPRESS;IsClustered;No;"
						     "Version;15.0.2000.5;tcp;49712;;";
			memcpy(out, answer, sizeof(answer) - 1);
			n = sizeof(answer) - 1;
		}
		sendto(s->fd, out, n, 0, (struct sockaddr *)&from, flen);
	}
}

static int run_udp(enum udp_role role, const char *id, char *detail, size_t len, int *requests)
{
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(0x7f000001u) };
	socklen_t sl = sizeof(sa);
	struct udp_server s = { .role = role };
	pthread_t thread;
	s.fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (s.fd < 0 || bind(s.fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
	    getsockname(s.fd, (struct sockaddr *)&sa, &sl) != 0 || pthread_create(&thread, NULL, serve_udp, &s) != 0)
		return -1;
	detail[0] = '\0';
	int found = mnm_probe_find(id)->run(0x7f000001u, ntohs(sa.sin_port), 500, detail, len);
	pthread_join(thread, NULL);
	close(s.fd);
	*requests = s.requests;
	return found;
}

void test_probes_udp(void)
{
	char d[MNM_DETAIL_LEN];
	uint16_t ports[4];
	int requests;

	/* DNS over UDP: the ID of the query and the QR bit */
	const struct mnm_probe *dns = mnm_probe_find("dns_udp");
	CHECK(dns && dns->transport == MNM_UDP);
	CHECK(dns->identify((const uint8_t *)"\xaa\xaa\x81\x83", 4) == 1);
	CHECK(dns->identify((const uint8_t *)"\xaa\xaa\x01\x00", 4) == 0);
	CHECK(dns->identify((const uint8_t *)"\xaa\xab\x81\x80", 4) == 0);

	/* BACnet: BVLC type and a length that fits */
	const struct mnm_probe *bacnet = mnm_probe_find("bacnet");
	CHECK(bacnet->identify((const uint8_t *)"\x81\x0a\x00\x09\x01\x00\x60\x01\x09", 9) == 1);
	CHECK(bacnet->identify((const uint8_t *)"\x81\x0a\x00\x0a\x01\x00\x60\x01\x09", 9) == 0);
	CHECK(bacnet->identify((const uint8_t *)"\x82\x0a\x00\x04", 4) == 0);

	/* SQL Server Browser: every instance with its TCP port */
	static const char two[] = "\x05\x00\x00ServerName;A;InstanceName;ONE;IsClustered;Yes;Version;16.0.1000.6;tcp;1500;;"
				  "ServerName;A;InstanceName;TWO;IsClustered;No;Version;15.0.2000.5;np;\\\\A\\pipe;;";
	CHECK(mnm_mssql_browser((const uint8_t *)two, sizeof(two) - 1, ports, 4, d, sizeof(d)) == 1 && ports[0] == 1500);
	CHECK(strcmp(d, "Instance: ONE, version 16.0.1000.6, clustered · Instance: TWO, version 15.0.2000.5 · "
			"TCP port 1500") == 0);
	CHECK(mnm_mssql_browser((const uint8_t *)"ServerName;A;TCP;1433;", 22, ports, 4, d, sizeof(d)) == 0);
	for (size_t cut = 0; cut < sizeof(two) - 1; cut++)
		mnm_mssql_browser((const uint8_t *)two, cut, ports, 4, d, sizeof(d));

	/* DHCP: the server identifier, read from the options, not the first byte 54 */
	uint8_t offer[300];
	uint32_t server = 0;
	memset(offer, 0, sizeof(offer));
	offer[0] = 2;                                         /* BOOTREPLY */
	offer[4] = 54;                                        /* a 54 in the transaction id */
	offer[16] = 192, offer[17] = 0, offer[18] = 2, offer[19] = 9;   /* siaddr */
	offer[236] = 0x63, offer[237] = 0x82, offer[238] = 0x53, offer[239] = 0x63;
	const uint8_t options[] = { 53, 1, 2, 0, 1, 4, 255, 255, 255, 0, 54, 4, 192, 0, 2, 1, 255 };
	memcpy(offer + 240, options, sizeof(options));
	CHECK(mnm_dhcp_server(offer, sizeof(offer), &server) == 1 && server == ip("192.0.2.1"));
	offer[250] = 50;                                      /* no option 54 any more: siaddr */
	CHECK(mnm_dhcp_server(offer, sizeof(offer), &server) == 1 && server == ip("192.0.2.9"));
	offer[24] = 198, offer[25] = 51, offer[26] = 100, offer[27] = 7;   /* through a relay: giaddr */
	CHECK(mnm_dhcp_server(offer, sizeof(offer), &server) == 1 && server == ip("198.51.100.7"));
	offer[0] = 1;                                         /* a request is no answer */
	CHECK(mnm_dhcp_server(offer, sizeof(offer), &server) == 0);
	offer[0] = 2;
	offer[241] = 200;                                     /* an option longer than the packet */
	for (size_t cut = 0; cut <= sizeof(offer); cut++)
		mnm_dhcp_server(offer, cut, &server);

	/* complete runs against UDP servers on the loopback interface */
	CHECK(run_udp(NAME_SERVER, "dns_udp", d, sizeof(d), &requests) == 1 && requests == 2);
	CHECK(run_udp(BACNET, "bacnet", d, sizeof(d), &requests) == 1 && requests == 10);
	CHECK(strcmp(d, "ObjectID: 131330 · Vendor Name: Example Controls · Object Name: AHU-3") == 0);
	CHECK(run_udp(BROWSER, "mssqlbrowser", d, sizeof(d), &requests) == 1 && requests == 1);
	CHECK(strcmp(d, "Instance: SQLEXPRESS, version 15.0.2000.5 · TCP port 49712") == 0);

	/* a closed port: "port unreachable" is no name server */
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(0x7f000001u) };
	socklen_t sl = sizeof(sa);
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK(fd >= 0 && bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0 &&
	      getsockname(fd, (struct sockaddr *)&sa, &sl) == 0);
	close(fd);
	CHECK(dns->run(0x7f000001u, ntohs(sa.sin_port), 500, d, sizeof(d)) == 0);
	CHECK(bacnet->run(0x7f000001u, ntohs(sa.sin_port), 500, d, sizeof(d)) == 0);
}
