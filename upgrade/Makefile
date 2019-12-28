OUTFILE = blackmagic_upgrade

PC_HOSTED =
ifeq ($(PROBE_HOST), libftdi)
        PC_HOSTED = true
endif
ifeq ($(PROBE_HOST), pc-stlinkv2)
        PC_HOSTED = true
endif

CC = $(CROSS_COMPILE)gcc

CFLAGS = -Wall -Wextra -std=gnu99 -O0 -g -MD -mno-ms-bitfields
LDFLAGS = -lusb

OBJ =	bindata.o	\
	dfu.o		\
	stm32mem.o	\
	main.o

ifndef $(PROBE_HOST)
	PROBE_HOST=native
endif

ifndef  PC_HOSTED
all: $(OUTFILE)
else
all:
 ifeq ($(PROBE_HOST), libftdi)
	@echo "Libftdi needs no firmware update"
 endif
 ifeq ($(PROBE_HOST), pc-stlinkv2)
	@echo "Pc-stlinkv2 use ST provided tools for firmware update"
 endif
endif

bindata.o: $(PROBE_HOST).d

$(PROBE_HOST).d: $(wildcard ../src/blackmagic.bin)
	-rm *.d
	make -C ../src $0 clean
	make -C ../src $0
	touch $(PROBE_HOST).d

$(OUTFILE) $(OUTFILE).exe: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	-rm -rf $(OUTFILE) $(OUTFILE).exe *.d *.o

-include *.d

