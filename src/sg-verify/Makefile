TARGET=sgverify
GCC=gcc
LDFLAGS=-lrt
CFLAGS=-O2 -ggdb -Wall -std=c99
CSRC=$(wildcard *.c)
OBJ=$(patsubst %.c,%.o,$(CSRC))

#sg_verify: sg_cmds_basic.o sg_cmds_extra.o sg_pt_linux.o sg_lib.o sg_lib_data.o
#$(GCC) $(LDFLAGS) $(CFLAGS) sg_verify.c sg_cmds_basic.o sg_cmds_extra.o sg_pt_linux.o sg_lib.o sg_lib_data.o -o sg_verify

%.o: %.c
	$(GCC) $(CFLAGS) -c -o $@ $<

lib: $(OBJ)
	ar r lib$(TARGET).a $(OBJ)
	ranlib lib$(TARGET).a

clean:
	rm -rf *.o libsgverify.a
