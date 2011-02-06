GCC=gcc
LDFLAGS=-lrt
CFLAGS=-O2 -ggdb -Wall -std=c99

sg_verify: sg_cmds_basic.o sg_cmds_extra.o sg_pt_linux.o sg_lib.o sg_lib_data.o
#$(GCC) $(LDFLAGS) $(CFLAGS) sg_verify.c sg_cmds_basic.o sg_cmds_extra.o sg_pt_linux.o sg_lib.o sg_lib_data.o -o sg_verify

sg_cmds_basic.o: 
	$(GCC) $(CFALGS) -c sg_cmds_basic.c

sg_lib.o: 
	$(GCC) $(CFALGS) -c sg_lib.c

sg_cmds_extra.o: 
	$(GCC) $(CFALGS) -c sg_cmds_extra.c

sg_pt_linux.o: 
	$(GCC) $(CFALGS) -c sg_pt_linux.c

sg_lib_data.o:
	$(GCC) $(CFALGS) -c sg_lib_data.c

clean:
	rm -rf *.o sg_verify
