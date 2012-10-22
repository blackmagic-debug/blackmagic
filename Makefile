all:
	$(MAKE) -C libopencm3 lib
	$(MAKE) -C src

%:
	$(MAKE) -C libopencm3 $@
	$(MAKE) -C src $@

