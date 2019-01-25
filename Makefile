GCC	:= gcc
CFLAGS  := -O2 -ggdb -Wall -std=c99 `getconf LFS_CFLAGS`
LFLAGS  := -lrt -lm `getconf LFS_LDFLAGS`

default: hdck

hdck: src/block_info.o src/hdck.c src/sg-verify/libsgverify.a
	$(GCC) $(CFLAGS) -Isrc/sg-verify $^ -o $@ $(LFLAGS)

src/block_info.o: src/block_info.c
	$(GCC) -c $(CFLAGS)  $^ -o $@

src/sg-verify/libsgverify.a:
	cd src/sg-verify && make

clean:
	rm -f src/block_info.o hdck
	cd src/sg-verify && make clean

