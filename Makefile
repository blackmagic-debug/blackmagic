ifneq ($(V), 1)
MFLAGS += --no-print-dir
Q := @
endif

PC_HOSTED =
NO_LIBOPENCM3 =
ifeq ($(PROBE_HOST), hosted)
	PC_HOSTED = true
	NO_LIBOPENCM3 = true
endif

all:
ifndef NO_LIBOPENCM3
	$(Q)if [ ! -f libopencm3/Makefile ]; then \
		echo "Initialising git submodules..." ;\
		git submodule init ;\
		git submodule update ;\
	fi
	$(Q)$(MAKE) $(MFLAGS) -C libopencm3 lib
endif
	$(Q)$(MAKE) $(MFLAGS) -C src

clean:
ifndef NO_LIBOPENCM3
	$(Q)$(MAKE) $(MFLAGS) -C libopencm3 $@
endif
	$(Q)$(MAKE) $(MFLAGS) -C src $@

