# SPDX-License-Identifier: GPL-2.0-only
#
# Builds the scanner on any Linux system with a C compiler. The OpenWrt package
# (openwrt/mynetworkmonitor-scan/Makefile) calls this file with the SDK's
# compiler (CC, CFLAGS, LDFLAGS from the SDK).
#
#   make            the program, in build/
#   make test       the tests, built with AddressSanitizer and UBSan, and run
#                   (MBEDTLS=<folder of a built mbedTLS source tree> where mbedTLS is not installed)
#   make clean
#
# Every warning is an error: on a router a quiet mistake in C costs more than a
# failed build.

# The OpenWrt package passes VERSION; a build in the source tree reads it from ../VERSION.
VERSION ?= $(shell cat ../VERSION 2>/dev/null || echo unknown)
BUILD   ?= build

CC      ?= cc
CFLAGS  ?= -O2
WARN    := -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion \
           -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Werror
DEFS    := -DMNM_VERSION='"$(VERSION)"'

# mbedTLS (HTTPS and certificates): on OpenWrt from the SDK, whose include path
# comes as TLS_INCLUDE; for a build elsewhere the folder of a built mbedTLS
# source tree can be given as MBEDTLS=<folder>. Its headers count as system
# headers, so that their own warnings do not stop the build.
ifdef MBEDTLS
TLS_CFLAGS := -isystem $(MBEDTLS)/include
TLS_LIBS   := $(MBEDTLS)/library/libmbedtls.a $(MBEDTLS)/library/libmbedx509.a $(MBEDTLS)/library/libmbedcrypto.a
else
TLS_CFLAGS := $(if $(TLS_INCLUDE),-isystem $(TLS_INCLUDE))
TLS_LIBS   := -lmbedtls -lmbedx509 -lmbedcrypto
endif

LIBSRC  := src/targets.c src/ports.c src/json.c src/tcpscan.c src/packets.c src/textdata.c src/devices.c \
           src/discover.c src/probes.c src/probes_steps.c src/probes_udp.c src/probes_smb.c src/probe_engine.c src/netio.c src/dnsname.c src/bc_packets.c src/bc_parse.c src/bc_discover.c src/http_get.c src/tlsconn.c
SRC     := src/main.c $(LIBSRC)

.PHONY: all test clean

all: $(BUILD)/mynetworkmonitor-scan

$(BUILD)/mynetworkmonitor-scan: $(SRC) src/*.h $(wildcard ../VERSION)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(WARN) $(DEFS) $(TLS_CFLAGS) $(SRC) -o $@ $(LDFLAGS) $(TLS_LIBS) -pthread

$(BUILD)/tests: tests/tests.c tests/test_steps.c tests/test_udp.c tests/test_bc.c tests/test_tls.c tests/check.h $(LIBSRC) src/*.h
	@mkdir -p $(BUILD)
	$(CC) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $(WARN) $(DEFS) \
	      $(TLS_CFLAGS) tests/tests.c tests/test_steps.c tests/test_udp.c tests/test_bc.c tests/test_tls.c $(LIBSRC) -o $@ \
	      $(TLS_LIBS) -pthread

test: $(BUILD)/tests
	$(BUILD)/tests

clean:
	rm -rf $(BUILD)
