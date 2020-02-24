# Copyright (C) 2020  Jérémie Galarneau <jeremie.galarneau@efficios.com>
#
# THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
# OR IMPLIED. ANY USE IS AT YOUR OWN RISK.
#
# Permission is hereby granted to use or copy this program for any
# purpose, provided the above notices are retained on all copies.
# Permission to modify the code and to distribute modified code is
# granted, provided the above notices are retained, and a notice that
# the code was modified is included with the above copyright notice.
#
# This Makefile is not using automake so that users may see how to build
# a program with tracepoint provider probes compiled as static libraries.
#
# This makefile is purposefully kept simple to support GNU and BSD make.

LOCAL_CPPFLAGS += -I.
LIBS_INSTRUMENTED_APP = -ldl -llttng-ust
LIBS_NOTIFICATION_CLIENT = -ldl -llttng-ctl
LIBS_PERFORMANCE_CLIENT = -ldl -llttng-ust -llttng-ctl
AM_V_P := :
AR ?= ar

all: producer consumer

performance.o: performance.c performance.h
	@if $(AM_V_P); then set -x; else echo "  CC       $@"; fi; \
		$(CC) $(CPPFLAGS) $(LOCAL_CPPFLAGS) $(AM_CFLAGS) $(AM_CPPFLAGS) \
		$(CFLAGS) -c -o $@ $<

performance.a: performance.o
	@if $(AM_V_P); then set -x; else echo "  AR       $@"; fi; \
		$(AR) -rc $@ performance.o

producer.o: producer.c
	@if $(AM_V_P); then set -x; else echo "  CC       $@"; fi; \
		$(CC) $(CPPFLAGS) $(LOCAL_CPPFLAGS) $(AM_CFLAGS) $(AM_CPPFLAGS) \
		$(CFLAGS) -c -o $@ $<

consumer.o: consumer.c
	@if $(AM_V_P); then set -x; else echo "  CC       $@"; fi; \
		$(CC) $(CPPFLAGS) $(LOCAL_CPPFLAGS) $(AM_CFLAGS) $(AM_CPPFLAGS) \
		$(CFLAGS) -c -o $@ $<

producer: producer.o performance.a
	@if $(AM_V_P); then set -x; else echo "  CCLD     $@"; fi; \
		$(CC) -o $@ $(LDFLAGS) $(CPPFLAGS) $(AM_LDFLAGS) $(AM_CFLAGS) \
		$(CFLAGS) producer.o performance.a $(LIBS_PERFORMANCE_CLIENT)

consumer: consumer.o performance.a
	@if $(AM_V_P); then set -x; else echo "  CCLD     $@"; fi; \
		$(CC) -o $@ $(LDFLAGS) $(CPPFLAGS) $(AM_LDFLAGS) $(AM_CFLAGS) \
		$(CFLAGS) consumer.o performance.a $(LIBS_PERFORMANCE_CLIENT)

.PHONY: clean
clean:
	rm -f *.o *.a producer consumer
