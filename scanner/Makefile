# SPDX-License-Identifier: GPL-2.0-only
#
# Builds the scanner on any Linux system with a C compiler. The OpenWrt package
# (openwrt/mynetworkmonitor-scan/Makefile) calls this file with the SDK's
# compiler (CC, CFLAGS, LDFLAGS from the SDK).
#
#   make            the program, in build/
#   make test       the tests, built with AddressSanitizer and UBSan, and run
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

SRC     := src/main.c src/targets.c src/ports.c src/json.c src/tcpscan.c
LIBSRC  := src/targets.c src/ports.c src/json.c src/tcpscan.c

.PHONY: all test clean

all: $(BUILD)/mynetworkmonitor-scan

$(BUILD)/mynetworkmonitor-scan: $(SRC) src/*.h $(wildcard ../VERSION)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(WARN) $(DEFS) $(SRC) -o $@ $(LDFLAGS)

$(BUILD)/tests: tests/tests.c $(LIBSRC) src/*.h
	@mkdir -p $(BUILD)
	$(CC) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $(WARN) $(DEFS) \
	      tests/tests.c $(LIBSRC) -o $@

test: $(BUILD)/tests
	$(BUILD)/tests

clean:
	rm -rf $(BUILD)
