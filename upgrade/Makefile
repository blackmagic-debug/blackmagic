
CC = $(CROSS_COMPILE)gcc

CFLAGS = -Wall -Wextra -std=gnu99 -O0 -g -MD
LDFLAGS = -lusb

OBJ =	bindata.o	\
	dfu.o		\
	stm32mem.o	\
	main.o

all: bmp_upgrade

bmp_upgrade: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	-rm -rf bmp_upgrade *.d *.o

-include *.d

