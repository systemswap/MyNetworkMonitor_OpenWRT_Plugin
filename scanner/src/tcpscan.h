/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TCP connect scan: a port counts as open when the three-way handshake
 * completes. Needs no special rights, unlike a SYN scan with raw sockets.
 */
#ifndef MNM_TCPSCAN_H
#define MNM_TCPSCAN_H

#include <signal.h>
#include <stdint.h>

#include "ports.h"
#include "targets.h"

struct mnm_tcpscan_options {
	int parallel;     /* connections at the same time, 1 to MNM_TCPSCAN_MAX_PARALLEL */
	int timeout_ms;   /* per connection attempt */
	/* Set from a signal handler to end the scan early; checked between attempts. */
	volatile sig_atomic_t *stop;
	/* Called for every open port. */
	void (*on_open)(void *context, uint32_t address, uint16_t port);
	/* Called after every finished attempt with the number done so far. */
	void (*on_progress)(void *context, uint64_t done, uint64_t total);
	void *context;
};

/* Upper bound for parallel connections. Small routers have a few hundred file
 * descriptors and a small connection tracking table; 256 leaves room for both. */
#define MNM_TCPSCAN_MAX_PARALLEL 256

/*
 * Tries every port on every target. The order interleaves the targets
 * (all targets on the first port, then on the second, ...), so that no single
 * device receives a burst of connections. Returns 0 when all attempts are
 * done, 1 when stopped early, -1 on an error that prevents scanning.
 */
int mnm_tcpscan_run(const struct mnm_targets *targets, const struct mnm_ports *ports,
		    const struct mnm_tcpscan_options *options);

#endif
