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
AM_V_P := :
AR ?= ar

all: instrumented-app notification-client

tracepoint-trigger-example.o: tracepoint-trigger-example.c tracepoint-trigger-example.h
	@if $(AM_V_P); then set -x; else echo "  CC       $@"; fi; \
		$(CC) $(CPPFLAGS) $(LOCAL_CPPFLAGS) $(AM_CFLAGS) $(AM_CPPFLAGS) \
		$(CFLAGS) -c -o $@ $<

tracepoint-trigger-example.a: tracepoint-trigger-example.o
	@if $(AM_V_P); then set -x; else echo "  AR       $@"; fi; \
		$(AR) -rc $@ tracepoint-trigger-example.o

instrumented-app.o: instrumented-app.c
	@if $(AM_V_P); then set -x; else echo "  CC       $@"; fi; \
		$(CC) $(CPPFLAGS) $(LOCAL_CPPFLAGS) $(AM_CFLAGS) $(AM_CPPFLAGS) \
		$(CFLAGS) -c -o $@ $<

instrumented-app: instrumented-app.o tracepoint-trigger-example.a
	@if $(AM_V_P); then set -x; else echo "  CCLD     $@"; fi; \
		$(CC) -o $@ $(LDFLAGS) $(CPPFLAGS) $(AM_LDFLAGS) $(AM_CFLAGS) \
		$(CFLAGS) instrumented-app.o tracepoint-trigger-example.a $(LIBS_INSTRUMENTED_APP)

notification-client.o: notification-client.c
	@if $(AM_V_P); then set -x; else echo "  CC       $@"; fi; \
		$(CC) $(CPPFLAGS) $(LOCAL_CPPFLAGS) $(AM_CFLAGS) $(AM_CPPFLAGS) \
		$(CFLAGS) -c -o $@ $<

notification-client: notification-client.o
	@if $(AM_V_P); then set -x; else echo "  CCLD     $@"; fi; \
		$(CC) -o $@ $(LDFLAGS) $(CPPFLAGS) $(AM_LDFLAGS) $(AM_CFLAGS) \
		$(CFLAGS) notification-client.o tracepoint-trigger-example.a $(LIBS_NOTIFICATION_CLIENT)

.PHONY: clean
clean:
	rm -f *.o *.a instrumented-app notification-client
