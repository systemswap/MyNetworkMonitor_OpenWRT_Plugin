/* SPDX-License-Identifier: GPL-2.0-only */
#include "probe_engine.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Size of a request built per target (HTTP with the Host header). */
#define BUILT_MAX 512

enum state { FREE, CONNECTING, READING };

struct slot {
	enum state state;
	int fd;
	size_t job;
	int64_t deadline;      /* end of the current step: connecting, then waiting for the answer */
	const uint8_t *hello;  /* what is sent: the fixed packet or the built request */
	size_t hello_len, sent;
	uint8_t built[BUILT_MAX];
	uint8_t *answer;
	size_t len;
};

static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Starts a non-blocking connection; -1 if it failed at once, -2 without a free descriptor. */
static int start_connect(uint32_t address, uint16_t port)
{
	struct sockaddr_in sa;
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

	if (fd < 0)
		return (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM) ? -2 : -1;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(address);
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0 || errno == EINPROGRESS)
		return fd;
	close(fd);
	return -1;
}

/* Sends what is left of the greeting. Returns 0 while more is to send, 1 when
 * done, -1 on error. */
static int send_hello(struct slot *s)
{
	while (s->sent < s->hello_len) {
		ssize_t n = send(s->fd, s->hello + s->sent, s->hello_len - s->sent, MSG_NOSIGNAL);
		if (n < 0)
			return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
		s->sent += (size_t)n;
	}
	return 1;
}

static void finish(struct slot *s, const struct mnm_probe_job *job, const struct mnm_probe_options *o)
{
	const struct mnm_probe *p = job->probe;

	if (s->len > 0 && p->identify(s->answer, s->len)) {
		char detail[MNM_DETAIL_LEN] = "";
		if (p->describe)
			p->describe(s->answer, s->len, detail, sizeof(detail));
		if (o->on_found)
			o->on_found(o->context, job->address, job->port, p, detail);
	}
	if (s->fd >= 0)
		close(s->fd);
	s->fd = -1;
	s->state = FREE;
}

/* ------------------------------------------------ probes with own procedure */

/* Workers for probes with several steps: each takes the next job and runs the
 * probe's procedure, which blocks within its own time limits. A few threads
 * are enough to keep many devices busy; their stacks are kept small for routers. */
#define WORKERS_MAX 16
#define WORKER_STACK (128 * 1024)

struct workshop {
	const struct mnm_probe_job *jobs;
	const size_t *list;        /* indices of the jobs with own procedure */
	size_t count, next, done, done_before, total;
	const struct mnm_probe_options *o;
	pthread_mutex_t lock;
};

static void *worker(void *arg)
{
	struct workshop *w = arg;
	for (;;) {
		pthread_mutex_lock(&w->lock);
		if (w->next >= w->count || (w->o->stop && *w->o->stop)) {
			pthread_mutex_unlock(&w->lock);
			return NULL;
		}
		const struct mnm_probe_job *job = &w->jobs[w->list[w->next++]];
		pthread_mutex_unlock(&w->lock);

		char detail[MNM_DETAIL_LEN] = "";
		int found = job->probe->run(job->address, job->port, w->o->timeout_ms, detail, sizeof(detail));

		/* The callbacks change the device list and write files: one at a time. */
		pthread_mutex_lock(&w->lock);
		if (found && w->o->on_found)
			w->o->on_found(w->o->context, job->address, job->port, job->probe, detail);
		w->done++;
		if (w->o->on_progress)
			w->o->on_progress(w->o->context, w->done_before + w->done, w->total);
		pthread_mutex_unlock(&w->lock);
	}
}

static int run_own_procedures(struct workshop *w, int parallel)
{
	pthread_t threads[WORKERS_MAX];
	pthread_attr_t attr;
	int n = parallel < WORKERS_MAX ? parallel : WORKERS_MAX, started = 0;

	if (w->count == 0)
		return 0;
	pthread_mutex_init(&w->lock, NULL);
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, WORKER_STACK);
	for (int i = 0; i < n && (size_t)i < w->count; i++)
		if (pthread_create(&threads[started], &attr, worker, w) == 0)
			started++;
	pthread_attr_destroy(&attr);
	if (started == 0)
		worker(w);   /* no thread could be started: do the work here */
	for (int i = 0; i < started; i++)
		pthread_join(threads[i], NULL);
	pthread_mutex_destroy(&w->lock);
	return 0;
}

/* -------------------------------------------- probes with one greeting */

static int run_greetings(const struct mnm_probe_job *all, const size_t *list, size_t count, size_t done_before,
			 size_t total, const struct mnm_probe_options *o);

int mnm_probe_run(const struct mnm_probe_job *jobs, size_t count, const struct mnm_probe_options *o)
{
	if (count == 0)
		return 0;
	size_t *own = malloc(count * sizeof(*own)), *greet = malloc(count * sizeof(*greet));
	size_t n_own = 0, n_greet = 0;
	if (!own || !greet) {
		free(own);
		free(greet);
		return -1;
	}
	for (size_t i = 0; i < count; i++) {
		if (jobs[i].probe->run)
			own[n_own++] = i;
		else
			greet[n_greet++] = i;
	}
	int parallel = o->parallel < 1 ? 1 : o->parallel > 256 ? 256 : o->parallel;
	struct workshop w = { .jobs = jobs, .list = own, .count = n_own, .total = count, .o = o };
	run_own_procedures(&w, parallel);
	int outcome = (o->stop && *o->stop) ? 1 : run_greetings(jobs, greet, n_greet, n_own, count, o);
	free(own);
	free(greet);
	return outcome;
}

static int run_greetings(const struct mnm_probe_job *all, const size_t *list, size_t count, size_t done_before,
			 size_t total, const struct mnm_probe_options *o)
{
	int parallel = o->parallel < 1 ? 1 : o->parallel > 256 ? 256 : o->parallel;
	size_t next = 0, done = done_before;
	int stopped = 0;

	if (count == 0)
		return 0;
	struct slot *slots = calloc((size_t)parallel, sizeof(*slots));
	struct pollfd *pfds = calloc((size_t)parallel, sizeof(*pfds));
	int *index_of = calloc((size_t)parallel, sizeof(*index_of));
	uint8_t *buffers = malloc((size_t)parallel * MNM_PROBE_ANSWER_MAX);
	if (!slots || !pfds || !index_of || !buffers) {
		free(slots);
		free(pfds);
		free(index_of);
		free(buffers);
		return -1;
	}
	for (int i = 0; i < parallel; i++) {
		slots[i].fd = -1;
		slots[i].answer = buffers + (size_t)i * MNM_PROBE_ANSWER_MAX;
	}

	for (;;) {
		if (o->stop && *o->stop)
			stopped = 1;

		int out_of_sockets = 0;
		for (int i = 0; i < parallel && !stopped && !out_of_sockets; i++) {
			while (slots[i].state == FREE && next < count) {
				int fd = start_connect(all[list[next]].address, all[list[next]].port);
				if (fd == -2) {
					out_of_sockets = 1;
					break;
				}
				size_t job = list[next++];
				if (fd < 0) {
					done++;
					if (o->on_progress)
						o->on_progress(o->context, done, total);
					continue;
				}
				struct slot *s = &slots[i];
				s->state = CONNECTING;
				s->fd = fd;
				s->job = job;
				s->deadline = now_ms() + o->timeout_ms;
				s->sent = 0;
				s->len = 0;
				const struct mnm_probe *p = all[job].probe;
				if (p->build) {
					s->hello = s->built;
					s->hello_len = p->build(all[job].address, s->built, sizeof(s->built));
				} else {
					s->hello = p->hello;
					s->hello_len = p->hello ? p->hello_len : 0;
				}
			}
		}

		int active = 0;
		int64_t now = now_ms(), wait = 200;
		for (int i = 0; i < parallel; i++) {
			struct slot *s = &slots[i];
			if (s->state == FREE)
				continue;
			pfds[active].fd = s->fd;
			pfds[active].events = (s->state == CONNECTING) ? POLLOUT : POLLIN;
			pfds[active].revents = 0;
			index_of[active++] = i;
			if (s->deadline - now < wait)
				wait = s->deadline - now < 0 ? 0 : s->deadline - now;
		}
		if (active == 0) {
			if (stopped || next >= count)
				break;
			poll(NULL, 0, 50);
			continue;
		}
		if (poll(pfds, (nfds_t)active, (int)wait) < 0 && errno != EINTR)
			break;

		now = now_ms();
		for (int k = 0; k < active; k++) {
			struct slot *s = &slots[index_of[k]];
			const struct mnm_probe_job *job = &all[s->job];
			int over = 0;

			if (s->state == CONNECTING && (pfds[k].revents & (POLLOUT | POLLERR | POLLHUP))) {
				int error = 0;
				socklen_t elen = sizeof(error);
				if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &error, &elen) != 0 || error != 0) {
					over = 1;
				} else {
					/* Connected: the time for the answer starts now, as for the connection before. */
					s->state = READING;
					s->deadline = now + o->timeout_ms;
					if (send_hello(s) < 0)
						over = 1;
				}
			} else if (s->state == READING && s->sent < s->hello_len && send_hello(s) < 0) {
				over = 1;
			}
			/* Exactly one read as soon as something arrives; the check sees what
			 * came in that read (a second read would change the answer some
			 * checks expect, e.g. BigFix wants exactly 12 bytes). */
			if (!over && s->state == READING && s->sent == s->hello_len &&
			    (pfds[k].revents & (POLLIN | POLLHUP | POLLERR))) {
				ssize_t n = recv(s->fd, s->answer, MNM_PROBE_ANSWER_MAX, MSG_DONTWAIT);
				if (n > 0)
					s->len = (size_t)n;
				if (n >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
					over = 1;
			}
			if (now >= s->deadline || stopped)
				over = 1;
			if (over) {
				finish(s, job, o);
				done++;
				if (o->on_progress)
					o->on_progress(o->context, done, total);
			}
		}
	}

	for (int i = 0; i < parallel; i++)
		if (slots[i].fd >= 0)
			close(slots[i].fd);
	free(slots);
	free(pfds);
	free(index_of);
	free(buffers);
	return stopped ? 1 : 0;
}
