/* SPDX-License-Identifier: GPL-2.0-only */
#include "tcpscan.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

struct slot {
	int fd;           /* -1 when free */
	uint32_t address;
	uint16_t port;
	int64_t deadline; /* milliseconds, monotonic clock */
};

static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Starts a non-blocking connection. Returns the socket, or -1 if the attempt
 * is already over (unreachable, refused at once); *open is set when the
 * connection was established immediately (local addresses). Returns -2 when
 * no socket is available right now (descriptor limit). */
static int start_connect(uint32_t address, uint16_t port, int *open)
{
	struct sockaddr_in sa;
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

	*open = 0;
	if (fd < 0)
		return (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM) ? -2 : -1;

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(address);

	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
		*open = 1;
		close(fd);
		return -1;
	}
	if (errno == EINPROGRESS)
		return fd;
	close(fd);
	return -1;
}

int mnm_tcpscan_run(const struct mnm_targets *targets, const struct mnm_ports *ports,
		    const struct mnm_tcpscan_options *options)
{
	int parallel = options->parallel;
	uint64_t total = (uint64_t)targets->total * ports->count;
	uint64_t next = 0, done = 0;
	int stopped = 0;

	if (parallel < 1)
		parallel = 1;
	if (parallel > MNM_TCPSCAN_MAX_PARALLEL)
		parallel = MNM_TCPSCAN_MAX_PARALLEL;
	if (total == 0)
		return 0;

	struct slot *slots = calloc((size_t)parallel, sizeof(*slots));
	struct pollfd *pfds = calloc((size_t)parallel, sizeof(*pfds));
	int *index_of = calloc((size_t)parallel, sizeof(*index_of));
	if (!slots || !pfds || !index_of) {
		free(slots);
		free(pfds);
		free(index_of);
		return -1;
	}
	for (int i = 0; i < parallel; i++)
		slots[i].fd = -1;

	for (;;) {
		int active = 0;
		int out_of_sockets = 0;

		if (options->stop && *options->stop)
			stopped = 1;

		/* Fill free slots with the next attempts. */
		for (int i = 0; i < parallel && !stopped && !out_of_sockets; i++) {
			while (slots[i].fd < 0 && next < total) {
				uint32_t address = mnm_targets_at(targets, (uint32_t)(next % targets->total));
				uint16_t port = ports->list[next / targets->total];
				int open;
				int fd = start_connect(address, port, &open);

				if (fd == -2) {
					/* No descriptor left: retry this attempt when a slot frees up. */
					out_of_sockets = 1;
					break;
				}
				next++;
				if (fd >= 0) {
					slots[i].fd = fd;
					slots[i].address = address;
					slots[i].port = port;
					slots[i].deadline = now_ms() + options->timeout_ms;
				} else {
					if (open && options->on_open)
						options->on_open(options->context, address, port);
					done++;
					if (options->on_progress)
						options->on_progress(options->context, done, total);
				}
			}
		}

		int64_t now = now_ms();
		int64_t wait = -1;
		for (int i = 0; i < parallel; i++) {
			if (slots[i].fd < 0)
				continue;
			pfds[active].fd = slots[i].fd;
			pfds[active].events = POLLOUT;
			pfds[active].revents = 0;
			index_of[active] = i;
			active++;
			int64_t left = slots[i].deadline - now;
			if (left < 0)
				left = 0;
			if (wait < 0 || left < wait)
				wait = left;
		}

		if (active == 0) {
			if (stopped || next >= total)
				break;
			/* Only possible after running out of descriptors with nothing in
			 * flight: wait briefly instead of spinning. */
			poll(NULL, 0, 50);
			continue;
		}

		/* Wake up at least every 200 ms so that a stop request is noticed. */
		if (wait > 200)
			wait = 200;
		int ready = poll(pfds, (nfds_t)active, (int)wait);
		if (ready < 0 && errno != EINTR)
			break;

		now = now_ms();
		for (int k = 0; k < active; k++) {
			struct slot *s = &slots[index_of[k]];
			int finished = 0;

			if (ready > 0 && (pfds[k].revents & (POLLOUT | POLLERR | POLLHUP))) {
				int error = 0;
				socklen_t len = sizeof(error);
				if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0 &&
				    options->on_open)
					options->on_open(options->context, s->address, s->port);
				finished = 1;
			} else if (now >= s->deadline || stopped) {
				finished = 1;   /* no answer in time: filtered or no device */
			}
			if (finished) {
				close(s->fd);
				s->fd = -1;
				done++;
				if (options->on_progress)
					options->on_progress(options->context, done, total);
			}
		}
	}

	for (int i = 0; i < parallel; i++)
		if (slots[i].fd >= 0)
			close(slots[i].fd);
	free(slots);
	free(pfds);
	free(index_of);
	return stopped ? 1 : 0;
}
