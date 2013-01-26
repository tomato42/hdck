GCC	:= gcc
CFLAGS  := -O2 -ggdb -Wall -std=c99 `getconf LFS_CFLAGS`
LFLAGS  := -lrt -lm `getconf LFS_LDFLAGS`

default: hdck

hdck: block_info.o hdck.c sg-verify/sg_cmds_extra.o
	$(GCC) $(CFLAGS) -Isg-verify -Lsg-verify -o hdck hdck.c block_info.o sg-verify/*.o $(LFLAGS) 

block_info.o: block_info.c block_info.h
	$(GCC) -c $(CFLAGS)  block_info.c -o block_info.o

sg-verify/sg_cmds_extra.o:
	cd sg-verify && make

clean:
	rm -f block_info.o hdck
	cd sg-verify && make clean

