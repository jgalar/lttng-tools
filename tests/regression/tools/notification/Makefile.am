AM_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src -I$(top_srcdir)/tests -I$(top_srcdir)/tests/utils/ -I$(srcdir)
AM_LDFLAGS =

if LTTNG_TOOLS_BUILD_WITH_LIBDL
LIBS += -ldl
endif
if LTTNG_TOOLS_BUILD_WITH_LIBC_DL
LIBS += -lc
endif

LIBTAP=$(top_builddir)/tests/utils/tap/libtap.la
LIB_LTTNG_CTL = $(top_builddir)/src/lib/lttng-ctl/liblttng-ctl.la

noinst_PROGRAMS = base_client notification

if NO_SHARED
EXTRA_DIST = consumer_testpoints.c
else
# In order to test the health check feature, the helper library
# must be built as .so to be able to LD_PRELOAD it.
FORCE_SHARED_LIB_OPTIONS = -module -shared -avoid-version \
			   -rpath $(abs_builddir)
libpause_consumer_la_SOURCES = consumer_testpoints.c
libpause_consumer_la_LIBADD = $(top_builddir)/src/common/libcommon.la
libpause_consumer_la_LDFLAGS = $(FORCE_SHARED_LIB_OPTIONS)
noinst_LTLIBRARIES = libpause_consumer.la
endif

base_client_SOURCES = base_client.c
base_client_LDADD = $(LIB_LTTNG_CTL)

notification_SOURCES = notification.c
notification_LDADD = $(LIB_LTTNG_CTL) $(LIBTAP) -lm


all-local:
	@if [ x"$(srcdir)" != x"$(builddir)" ]; then \
		for script in $(EXTRA_DIST); do \
			cp -f $(srcdir)/$$script $(builddir); \
		done; \
	fi

clean-local:
	@if [ x"$(srcdir)" != x"$(builddir)" ]; then \
		for script in $(EXTRA_DIST); do \
			rm -f $(builddir)/$$script; \
		done; \
	fi
