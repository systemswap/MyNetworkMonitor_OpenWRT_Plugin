/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Runs service probes against open ports: connect, send the greeting, collect
 * the answer until the service stops sending or the time is up, and let the
 * probe decide. Many probes run at the same time, each with its own time limit.
 */
#ifndef MNM_PROBE_ENGINE_H
#define MNM_PROBE_ENGINE_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#include "probes.h"

struct mnm_probe_job {
	uint32_t address;
	uint16_t port;
	const struct mnm_probe *probe;
};

struct mnm_probe_options {
	int parallel;
	int timeout_ms;               /* per job: connect, send and answer together */
	volatile sig_atomic_t *stop;
	/* Called for every recognised service. detail may be empty. */
	void (*on_found)(void *context, uint32_t address, uint16_t port, const struct mnm_probe *probe,
			 const char *detail);
	void (*on_progress)(void *context, uint64_t done, uint64_t total);
	void *context;
};

/* Size of the answer buffer per job: enough for every greeting the probes
 * read, small enough for 256 jobs at once on a router. */
#define MNM_PROBE_ANSWER_MAX 4096

/* Runs all jobs. Returns 0 when done, 1 when stopped early, -1 without memory. */
int mnm_probe_run(const struct mnm_probe_job *jobs, size_t count, const struct mnm_probe_options *options);

#endif
