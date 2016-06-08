EXTRA_DIST = live.lttng live_output_generation.lttng name_generation.lttng \
	     normal_local.lttng normal_net.lttng normal_no_output.lttng \
	     normal_output_generation.lttng shm_path.lttng snapshot_local.lttng \
	     snapshot_net.lttng snapshot_output_generation.lttng full_load.lttng

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
