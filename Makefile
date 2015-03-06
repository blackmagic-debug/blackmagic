all:
	@if [ ! -f libopencm3/Makefile ]; then \
		echo "Initialising git submodules..." ;\
		git submodule init ;\
		git submodule update ;\
	fi
	$(MAKE) -C libopencm3 lib
	$(MAKE) -C src

%:
	$(MAKE) -C libopencm3 $@
	$(MAKE) -C src $@

