OUTFILE = blackmagic_upgrade

CC = $(CROSS_COMPILE)gcc

CFLAGS = -Wall -Wextra -std=gnu99 -O0 -g -MD -mno-ms-bitfields
LDFLAGS = -lusb

OBJ =	bindata.o	\
	dfu.o		\
	stm32mem.o	\
	main.o

all: $(OUTFILE)

$(OUTFILE) $(OUTFILE).exe: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	-rm -rf $(OUTFILE) $(OUTFILE).exe *.d *.o

-include *.d

