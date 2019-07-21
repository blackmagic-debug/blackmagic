ifneq ($(V), 1)
MFLAGS += --no-print-dir
Q := @
endif

PC_HOSTED =
ifeq ($(PROBE_HOST), libftdi)
	PC_HOSTED = true
endif
ifeq ($(PROBE_HOST), pc-stlinkv2)
	PC_HOSTED = true
endif

all:
ifndef 	PC_HOSTED
	$(Q)if [ ! -f libopencm3/Makefile ]; then \
		echo "Initialising git submodules..." ;\
		git submodule init ;\
		git submodule update ;\
	fi
	$(Q)$(MAKE) $(MFLAGS) -C libopencm3 lib
endif
	$(Q)$(MAKE) $(MFLAGS) -C src

clean:
ifndef 	PC_HOSTED
	$(Q)$(MAKE) $(MFLAGS) -C libopencm3 $@
endif
	$(Q)$(MAKE) $(MFLAGS) -C src $@

