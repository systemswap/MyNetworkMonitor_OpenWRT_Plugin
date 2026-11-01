/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tests of the discovery methods: reading SSDP answers and UPnP descriptions,
 * WS-Discovery and ONVIF probe matches, mDNS responses and NetBIOS node
 * status, the message IDs, and the HTTP GET against a server on the loopback
 * interface. All answers are made up (documentation addresses).
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../src/bc_discover.h"
#include "../src/bc_parse.h"
#include "../src/http_get.h"
#include "check.h"

#define S(lit) lit, sizeof(lit) - 1

/* Appends a DNS name as labels. */
static size_t put_name(uint8_t *p, const char *name)
{
	size_t n = 0;
	while (*name) {
		const char *dot = strchr(name, '.');
		size_t l = dot ? (size_t)(dot - name) : strlen(name);
		p[n++] = (uint8_t)l;
		memcpy(p + n, name, l);
		n += l;
		name += l + (dot ? 1 : 0);
	}
	p[n++] = 0;
	return n;
}

/* Appends a resource record header (type, class IN, TTL 120) and its data. */
static size_t put_record(uint8_t *p, const char *owner, unsigned int type, const uint8_t *data, size_t len)
{
	size_t n = put_name(p, owner);
	p[n++] = (uint8_t)(type >> 8), p[n++] = (uint8_t)type, p[n++] = 0, p[n++] = 1;
	p[n++] = 0, p[n++] = 0, p[n++] = 0, p[n++] = 120;
	p[n++] = (uint8_t)(len >> 8), p[n++] = (uint8_t)len;
	memcpy(p + n, data, len);
	return n + len;
}

static void test_ssdp_upnp(void)
{
	char v[128], host[64], path[128], d[MNM_DETAIL_LEN];
	uint16_t port;
	static const char answer[] = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=1800\r\nX-SERVER: other\r\n"
				     "Location: http://192.0.2.10:49152/desc.xml\r\nSERVER: Linux/5.4 UPnP/1.0 Example/1.0\r\n\r\n";
	CHECK(mnm_ssdp_header(S(answer), "LOCATION", v, sizeof(v)) && strcmp(v, "http://192.0.2.10:49152/desc.xml") == 0);
	CHECK(mnm_ssdp_header(S(answer), "SERVER", v, sizeof(v)) && strcmp(v, "Linux/5.4 UPnP/1.0 Example/1.0") == 0);
	CHECK(mnm_ssdp_header(S(answer), "USN", v, sizeof(v)) == 0);

	CHECK(mnm_url_split("http://192.0.2.10:49152/desc.xml", host, sizeof(host), &port, path, sizeof(path)) &&
	      strcmp(host, "192.0.2.10") == 0 && port == 49152 && strcmp(path, "/desc.xml") == 0);
	CHECK(mnm_url_split("HTTP://192.0.2.10", host, sizeof(host), &port, path, sizeof(path)) && port == 80 &&
	      strcmp(path, "/") == 0);
	CHECK(mnm_url_split("https://192.0.2.10/", host, sizeof(host), &port, path, sizeof(path)) == 0);
	CHECK(mnm_url_split("http://user@192.0.2.10/", host, sizeof(host), &port, path, sizeof(path)) == 0);
	CHECK(mnm_url_split("http://192.0.2.10:0/", host, sizeof(host), &port, path, sizeof(path)) == 0);
	CHECK(mnm_url_split("http://192.0.2.10/a b", host, sizeof(host), &port, path, sizeof(path)) == 0);

	static const char xml[] = "<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
				  "<device><friendlyName>Living &amp; Room</friendlyName>\n<manufacturer> Example\n Corp "
				  "</manufacturer><modelName>Box</modelName><modelNumber>2</modelNumber>"
				  "<deviceList><device><friendlyName>Inner</friendlyName></device></deviceList></device></root>";
	CHECK(mnm_upnp_describe(S(xml), d, sizeof(d)) &&
	      strcmp(d, "Name: Living & Room · Manufacturer: Example Corp · Model: Box 2") == 0);
	CHECK(mnm_upnp_describe(S("<root><device/></root>"), d, sizeof(d)) == 0);
	CHECK(mnm_xml_text(S("<a><b/><B>x<c>y</c>z</B></a>"), "b", v, sizeof(v)) == 0);   /* the first one is empty */
	CHECK(mnm_xml_text(S("<a><n:x>1<n:x>2</n:x>3</n:x></a>"), "x", v, sizeof(v)) && strcmp(v, "123") == 0);
	/* HTML entities as device pages use them; a space in the middle of a run of spaces collapses */
	CHECK(mnm_xml_text(S("<title>Printer&nbsp;&nbsp;&nbsp;192.0.2.6 &auml;&#252;&#x20AC; &bogus; &amp;</title>"), "title",
			   v, sizeof(v)) && strcmp(v, "Printer 192.0.2.6 \xc3\xa4\xc3\xbc\xe2\x82\xac &bogus; &") == 0);
	for (size_t cut = 0; cut < sizeof(xml) - 1; cut++)
		mnm_upnp_describe(xml, cut, d, sizeof(d));
}

static void test_wsd_onvif(void)
{
	char d[MNM_DETAIL_LEN];
	static const char printer[] = "<s:Envelope><s:Body><d:ProbeMatches><d:ProbeMatch><d:Types>wsdp:Device "
				      "wprt:PrintDeviceType wscn:ScanDeviceType</d:Types><d:XAddrs>http://192.0.2.40:5357/x"
				      "</d:XAddrs></d:ProbeMatch></d:ProbeMatches></s:Body></s:Envelope>";
	mnm_wsd_describe(S(printer), d, sizeof(d));
	CHECK(strcmp(d, "printer, scanner · Types: wsdp:Device wprt:PrintDeviceType wscn:ScanDeviceType") == 0);
	mnm_wsd_describe(S("\x01\x02 not xml"), d, sizeof(d));
	CHECK(strcmp(d, "Replied, but the response could not be read.") == 0);

	static const char camera[] =
		"<SOAP-ENV:Envelope><SOAP-ENV:Body><d:ProbeMatches><d:ProbeMatch>"
		"<wsa:EndpointReference><wsa:Address>urn:uuid:0a1b2c3d-0000-4000-8000-000000000001</wsa:Address>"
		"</wsa:EndpointReference><d:Types>dn:NetworkVideoTransmitter</d:Types>"
		"<d:Scopes>onvif://www.onvif.org/type/video_encoder onvif://www.onvif.org/name/Garden%20Cam "
		"onvif://www.onvif.org/hardware/X1</d:Scopes>"
		"<d:XAddrs>http://192.0.2.20/onvif/device_service http://[2001:db8::20]/onvif/device_service</d:XAddrs>"
		"<d:MetadataVersion>1</d:MetadataVersion></d:ProbeMatch></d:ProbeMatches></SOAP-ENV:Body></SOAP-ENV:Envelope>";
	CHECK(mnm_onvif_describe(S(camera), d, sizeof(d)) == 1 && strcmp(d, "Garden Cam http://192.0.2.20") == 0);
	char without[sizeof(camera)];
	memcpy(without, camera, sizeof(camera));
	memcpy(strstr(without, "MetadataVersion>1"), "Xetadata", 8);   /* the element missing */
	CHECK(mnm_onvif_describe(without, sizeof(without) - 1, d, sizeof(d)) == 0);
	for (size_t cut = 0; cut < sizeof(camera) - 1; cut++)
		mnm_onvif_describe(camera, cut, d, sizeof(d));
}

static void test_mdns(void)
{
	uint8_t r[512], data[128];
	size_t n = 12;
	struct mnm_mdns_info info;
	char d[MNM_DETAIL_LEN];

	memset(r, 0, 12);
	r[2] = 0x84;                       /* response, authoritative */
	r[5] = 1, r[7] = 1, r[11] = 1;     /* one question (echoed), one answer, one additional record */
	n += put_name(r + n, "_googlecast._tcp.local");
	r[n++] = 0, r[n++] = 12, r[n++] = 0, r[n++] = 1;
	size_t dl = put_name(data, "Living Room._googlecast._tcp.local");
	n += put_record(r + n, "_googlecast._tcp.local", 12, data, dl);
	const uint8_t a[4] = { 192, 0, 2, 30 };
	n += put_record(r + n, "Chromecast-1.local", 1, a, 4);

	memset(&info, 0, sizeof(info));
	CHECK(mnm_mdns_parse(r, n, ip("192.0.2.30"), &info) == 1);
	CHECK(info.service_count == 1 && strcmp(info.services[0], "_googlecast._tcp") == 0);
	CHECK(strcmp(info.instance, "Living Room") == 0 && strcmp(info.host, "Chromecast-1") == 0);
	mnm_mdns_describe(&info, d, sizeof(d));
	CHECK(strcmp(d, "Services: _googlecast._tcp · Device: Living Room") == 0);

	/* an address record for another address does not name the sender */
	memset(&info, 0, sizeof(info));
	CHECK(mnm_mdns_parse(r, n, ip("192.0.2.31"), &info) == 1 && info.host[0] == '\0');

	/* a query of another device is no answer */
	r[2] = 0x00;
	memset(&info, 0, sizeof(info));
	CHECK(mnm_mdns_parse(r, n, ip("192.0.2.30"), &info) == 0);
	r[2] = 0x84;
	for (size_t cut = 0; cut < n; cut++)
		mnm_mdns_parse(r, cut, ip("192.0.2.30"), &info);
}

static void test_netbios(void)
{
	uint8_t r[200];
	char host[16], group[16];
	size_t n = 0;
	static const struct { const char *name; uint8_t suffix, flags; } names[] = {
		{ "DESKTOP-ONE", 0x00, 0x04 }, { "WORKGROUP", 0x00, 0x84 }, { "DESKTOP-ONE", 0x20, 0x04 },
		{ "WORKGROUP", 0x1e, 0x84 },
	};

	r[n++] = 0x12, r[n++] = 0x34, r[n++] = 0x84, r[n++] = 0x00;
	r[n++] = 0, r[n++] = 0, r[n++] = 0, r[n++] = 1, r[n++] = 0, r[n++] = 0, r[n++] = 0, r[n++] = 0;
	r[n++] = 0x20;
	memset(r + n, 'A', 32);
	r[n] = 'C', r[n + 1] = 'K';
	n += 32;
	r[n++] = 0;
	r[n++] = 0, r[n++] = 0x21, r[n++] = 0, r[n++] = 1, r[n++] = 0, r[n++] = 0, r[n++] = 0, r[n++] = 0;
	size_t rdlen_at = n;
	n += 2;
	size_t rdata = n;
	r[n++] = 4;
	for (size_t i = 0; i < 4; i++) {
		memset(r + n, ' ', 15);
		memcpy(r + n, names[i].name, strlen(names[i].name));
		r[n + 15] = names[i].suffix, r[n + 16] = names[i].flags, r[n + 17] = 0;
		n += 18;
	}
	memcpy(r + n, "\x02\x00\x00\x00\x00\x01", 6);   /* unit id */
	n += 6;
	r[rdlen_at] = (uint8_t)((n - rdata) >> 8), r[rdlen_at + 1] = (uint8_t)(n - rdata);

	CHECK(mnm_netbios_parse(r, n, 0x1234, host, sizeof(host), group, sizeof(group)) == 1);
	CHECK(strcmp(host, "DESKTOP-ONE") == 0 && strcmp(group, "WORKGROUP") == 0);
	CHECK(mnm_netbios_parse(r, n, 0x1235, host, sizeof(host), group, sizeof(group)) == 0);   /* other ID */
	r[3] = 0x03;                                                                       /* an error */
	CHECK(mnm_netbios_parse(r, n, 0x1234, host, sizeof(host), group, sizeof(group)) == 0);
	r[3] = 0x00;
	/* cut off in the middle of the names: the complete ones still count */
	CHECK(mnm_netbios_parse(r, rdata + 1 + 18 * 3 + 5, 0x1234, host, sizeof(host), group, sizeof(group)) == 1);
	for (size_t cut = 0; cut < n; cut++)
		mnm_netbios_parse(r, cut, 0x1234, host, sizeof(host), group, sizeof(group));
}

static void test_guid_methods(void)
{
	uint8_t g[36], h[36];
	unsigned int m;
	char err[80];
	mnm_guid(g);
	mnm_guid(h);
	int form = 1;
	for (int i = 0; i < 36; i++) {
		int dash = i == 8 || i == 13 || i == 18 || i == 23;
		form &= dash ? g[i] == '-' : ((g[i] >= '0' && g[i] <= '9') || (g[i] >= 'a' && g[i] <= 'f'));
	}
	CHECK(form && g[14] == '4' && strchr("89ab", g[19]) && memcmp(g, h, 36) != 0);
	CHECK(mnm_bc_parse_methods("ssdp,mdns,netbios", &m, err, sizeof(err)) == 0 &&
	      m == (MNM_BC_SSDP | MNM_BC_MDNS | MNM_BC_NETBIOS));
	CHECK(mnm_bc_parse_methods("ssdp,llmnr", &m, err, sizeof(err)) == -1);
	CHECK(mnm_bc_parse_methods("", &m, err, sizeof(err)) == 0 && m == 0);
}

/* ---------------------------------------------------------- HTTP */

struct http_server {
	int listen_fd;
	char expected[128];
};

static void *serve_description(void *arg)
{
	struct http_server *srv = arg;
	int listen_fd = srv->listen_fd;
	char req[512];
	struct pollfd p = { .fd = listen_fd, .events = POLLIN };
	if (poll(&p, 1, 3000) <= 0)
		return NULL;
	int fd = accept(listen_fd, NULL, NULL);
	if (fd < 0)
		return NULL;
	ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
	static const char ok[] = "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nTransfer-Encoding: chunked\r\n\r\n"
				 "1c\r\n<root><device><friendlyName>\r\n"
				 "1f\r\nNAS Example</friendlyName></dev\r\n9\r\nice></roo\r\n2\r\nt>\r\n0\r\n\r\n";
	static const char bad[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
	/* the request must be exactly the one of the desktop program */
	int right = 0;
	if (n > 0) {
		req[n] = '\0';
		right = strcmp(req, srv->expected) == 0;
	}
	send(fd, right ? ok : bad, right ? sizeof(ok) - 1 : sizeof(bad) - 1, MSG_NOSIGNAL);
	/* keep the connection open: the client must stop at the last chunk */
	poll(&p, 0, 1500);
	close(fd);
	return NULL;
}

static void test_http(void)
{
	char buf[256], out[1024], *body;
	CHECK(mnm_http_request("192.0.2.10", 80, "/a.xml", buf, sizeof(buf)) &&
	      strcmp(buf, "GET /a.xml HTTP/1.1\r\nHost: 192.0.2.10\r\n\r\n") == 0);
	CHECK(mnm_http_request("192.0.2.10", 49152, "/a.xml", buf, sizeof(buf)) &&
	      strcmp(buf, "GET /a.xml HTTP/1.1\r\nHost: 192.0.2.10:49152\r\n\r\n") == 0);
	char plain[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhelloEXTRA";
	CHECK(mnm_http_complete(plain, strlen(plain)) && mnm_http_body(plain, strlen(plain), &body) == 5 &&
	      memcmp(body, "hello", 5) == 0);
	char chunked[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
	CHECK(mnm_http_complete(chunked, strlen(chunked)) && mnm_http_body(chunked, strlen(chunked), &body) == 11 &&
	      memcmp(body, "hello world", 11) == 0);
	char missing[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
	CHECK(mnm_http_body(missing, strlen(missing), &body) == -1);
	CHECK(mnm_http_complete("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nhello", 43) == 0);

	uint16_t port;
	pthread_t thread;
	struct http_server srv;
	srv.listen_fd = listen_local(&port);
	snprintf(srv.expected, sizeof(srv.expected), "GET /desc.xml HTTP/1.1\r\nHost: 127.0.0.1:%u\r\n\r\n", port);
	CHECK(srv.listen_fd >= 0 && pthread_create(&thread, NULL, serve_description, &srv) == 0);
	if (srv.listen_fd < 0)
		return;
	int listen_fd = srv.listen_fd;
	char host[32];
	snprintf(host, sizeof(host), "127.0.0.1");
	ssize_t n = mnm_http_get(0x7f000001u, port, host, "/desc.xml", 3000, out, sizeof(out));
	pthread_join(thread, NULL);
	close(listen_fd);
	CHECK(n > 0 && strcmp(out, "<root><device><friendlyName>NAS Example</friendlyName></device></root>") == 0);
}

void test_bc(void)
{
	test_ssdp_upnp();
	test_wsd_onvif();
	test_mdns();
	test_netbios();
	test_guid_methods();
	test_http();
}
