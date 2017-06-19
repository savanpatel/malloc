#Sample Makefile for Malloc
CC=gcc
CFLAGS=-g -O0 -fPIC

all:	check

clean:
	rm -rf libmalloc.so malloc.o test1 test1.o

libmalloc.so: malloc.o
	$(CC) $(CFLAGS) -shared -Wl,--unresolved-symbols=ignore-all -pthread $< -o $@

test1: test1.o
	$(CC) $(CFLAGS) $< -o $@ -pthread

# For every XYZ.c file, generate XYZ.o.
%.o: %.c
	$(CC) $(CFLAGS) $< -c -o $@

check:	libmalloc.so test1
	LD_PRELOAD=`pwd`/libmalloc.so ./test1

dist:
	dir=`basename $$PWD`; cd ..; tar cvf $$dir.tar ./$$dir; gzip $$dir.tar
