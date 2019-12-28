ifneq ($(V), 1)
MFLAGS += --no-print-dir
Q := @
endif

PC_HOSTED =
NO_LIBOPENCM3 =
ifeq ($(PROBE_HOST), libftdi)
	PC_HOSTED = true
	NO_LIBOPENCM3 = true
endif
ifeq ($(PROBE_HOST), pc-stlinkv2)
	PC_HOSTED = true
	NO_LIBOPENCM3 = true
endif
ifeq ($(PROBE_HOST), pc-hosted)
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

