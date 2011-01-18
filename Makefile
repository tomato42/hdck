GCC	:= gcc
CFLAGS  := -O2 -ggdb -Wall -std=c99 `getconf LFS_CFLAGS`
LFLAGS  := -lrt -lm `getconf LFS_LDFLAGS`

default: hdck

hdck: block_info.o hdck.c
	$(GCC) $(CFLAGS) $(LFLAGS) -o hdck hdck.c block_info.o 

block_info.o: block_info.c block_info.h
	$(GCC) -c $(CFLAGS)  block_info.c -o block_info.o

clean:
	rm -f block_info.o hdck

