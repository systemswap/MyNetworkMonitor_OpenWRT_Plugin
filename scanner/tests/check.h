/* SPDX-License-Identifier: GPL-2.0-only */
/* What the test files share: the check macro and two helpers. */
#ifndef MNM_TEST_CHECK_H
#define MNM_TEST_CHECK_H

#include <stdint.h>
#include <stdio.h>

extern int failures, checks;

#define CHECK(cond)                                                                 \
	do {                                                                        \
		checks++;                                                           \
		if (!(cond)) {                                                      \
			failures++;                                                 \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
		}                                                                   \
	} while (0)

/* An IPv4 address in host byte order. */
uint32_t ip(const char *text);

/* A listening TCP socket on 127.0.0.1 with a free port, or -1. */
int listen_local(uint16_t *port);

/* The tests of the probes with several steps (test_steps.c). */
void test_probes_steps(void);

#endif
