/* SPDX-License-Identifier: GPL-2.0-only */
#include "netio.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static struct sockaddr_in endpoint(uint32_t address, uint16_t port)
{
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(address);
	return sa;
}

enum mnm_connect_result mnm_tcp_connect(uint32_t address, uint16_t port, int timeout_ms, int *fd)
{
	struct sockaddr_in sa = endpoint(address, port);
	int s = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

	*fd = -1;
	if (s < 0)
		return MNM_FAILED;
	if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		if (errno == ECONNREFUSED) {
			close(s);
			return MNM_REFUSED;
		}
		if (errno != EINPROGRESS) {
			close(s);
			return MNM_FAILED;
		}
		struct pollfd p = { .fd = s, .events = POLLOUT };
		int r = poll(&p, 1, timeout_ms);
		if (r == 0) {
			close(s);
			return MNM_TIMEOUT;
		}
		int error = 0;
		socklen_t len = sizeof(error);
		if (r < 0 || getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &len) != 0 || error != 0) {
			close(s);
			return error == ECONNREFUSED ? MNM_REFUSED : MNM_FAILED;
		}
	}
	/* Back to blocking: every later step uses poll for its own time limit. */
	fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK);
	*fd = s;
	return MNM_CONNECTED;
}

int mnm_send_all(int fd, const void *data, size_t len, int timeout_ms)
{
	const uint8_t *p = data;
	int64_t end = now_ms() + timeout_ms;

	while (len > 0) {
		int64_t left = end - now_ms();
		struct pollfd pf = { .fd = fd, .events = POLLOUT };
		if (left <= 0 || poll(&pf, 1, (int)left) <= 0)
			return -1;
		ssize_t n = send(fd, p, len, MSG_NOSIGNAL | MSG_DONTWAIT);
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				continue;
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

ssize_t mnm_recv_once(int fd, void *buf, size_t cap, int timeout_ms)
{
	struct pollfd p = { .fd = fd, .events = POLLIN };
	int r = poll(&p, 1, timeout_ms);
	if (r == 0)
		return -2;
	if (r < 0)
		return -1;
	ssize_t n = recv(fd, buf, cap, MSG_DONTWAIT);
	if (n < 0)
		return (errno == EAGAIN || errno == EWOULDBLOCK) ? -2 : -1;
	return n;
}

int mnm_udp_socket(uint32_t address, uint16_t port)
{
	struct sockaddr_in sa = endpoint(address, port);
	int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (s < 0)
		return -1;
	if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		close(s);
		return -1;
	}
	return s;
}
