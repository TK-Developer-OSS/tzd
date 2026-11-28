# libc2d build.  Uses the project's GCC build (see ../work/build-c).
GCCDIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../work/build-c/gcc)
CC      = $(GCCDIR)/xgcc -B$(GCCDIR)/
CFLAGS  = -O2 -Wall -Wextra -Iinclude -nostdinc -ffreestanding -fno-stack-protector -fno-omit-frame-pointer
# Hand-lowered C: the runtime the checks are built on (bounds table, wipe,
# trap, allocator core, syscalls, start-up, host boundary).
OBJS    = build/sys.o build/pointer.o build/stdio.o build/stdlib.o build/start.o build/main.o build/boundary.o build/shadow.o build/hostrule.o build/regs.o build/hostalloc.o build/fnabi.o build/mpx.o build/group.o
# Managed C compiled by cc1tzd (libc2d/tzd/): the ISO C library parts.  The
# share written in TzD grows until libc2d becomes libd (task #43).
TZD     = ../bin/tzd
TZDFLAGS = --host -ftzd-delegate-unit -O2 -nostdinc -isystem tzd/sys -fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector
TZDOBJS = build/tzd_string.o build/tzd_ctype.o build/tzd_stdlib.o build/tzd_strtol.o build/tzd_qsort.o build/tzd_printf.o

all: build/libc2d.a build/crt0.o

build/%.o: src/%.c include/d/*.h src/sys.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# Also rebuilt when the compiler changes (2026-11-17: these objects kept
# the old per-scope wipes after cc1tzd was rebuilt).
build/tzd_%.o: tzd/%.c tzd/sys/*.h $(GCCDIR)/cc1tzd | build
	TZD_GCC=$(GCCDIR) $(TZD) $(TZDFLAGS) -c $< -o $@

build/libc2d.a: $(OBJS) $(TZDOBJS)
	rm -f $@
	ar rcs $@ $^

build/crt0.o: src/crt0.S | build
	$(CC) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -f build/*.o build/libc2d.a

.PHONY: all clean
