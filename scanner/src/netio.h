/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Blocking network steps with time limits, for probes that talk in several
 * steps (connect, send, read once, decide, send the next packet, ...). Each
 * call waits at most the given time; nothing waits forever.
 */
#ifndef MNM_NETIO_H
#define MNM_NETIO_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Result of a connection attempt, as the probes of the desktop program tell them apart. */
enum mnm_connect_result {
	MNM_CONNECTED = 0,
	MNM_REFUSED = -1,    /* the port is closed */
	MNM_TIMEOUT = -2,    /* no answer in time: filtered */
	MNM_FAILED = -3,     /* any other error */
};

/* Connects to address:port (host byte order) within timeout_ms. On success
 * *fd is a connected, blocking socket. */
enum mnm_connect_result mnm_tcp_connect(uint32_t address, uint16_t port, int timeout_ms, int *fd);

/* Sends all bytes within timeout_ms. Returns 0 on success, -1 on error or timeout. */
int mnm_send_all(int fd, const void *data, size_t len, int timeout_ms);

/* Waits up to timeout_ms for data and then reads ONCE (one recv of up to cap
 * bytes), as the desktop program does. Returns the number of bytes, 0 if the
 * peer closed, -1 on error, -2 on timeout. */
ssize_t mnm_recv_once(int fd, void *buf, size_t cap, int timeout_ms);

/* Reads exactly len bytes, waiting at most until the monotonic time deadline_ms
 * (see mnm_now_ms). Returns 0 on success, -1 if the peer closed or an error
 * occurred, -2 on timeout. */
int mnm_recv_exact(int fd, void *buf, size_t len, int64_t deadline_ms);

/* Monotonic time in milliseconds, for deadlines. */
int64_t mnm_now_ms(void);

/* A UDP socket connected to address:port, so that it only receives from there;
 * -1 on error. */
int mnm_udp_socket(uint32_t address, uint16_t port);

#endif
