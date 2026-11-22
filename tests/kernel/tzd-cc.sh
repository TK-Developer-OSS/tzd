#!/bin/bash
# Compiler for the TzD kernel build (task #42, tests/kernel/build_tzd.sh).
# Every 64-bit kernel .c file is compiled by cc1tzd in kernel mode
# (bin/tzd --host -ftzd-kernel); a rejected file stops the build (no
# fallback).  Plain GCC compiles only:
#   - assembly (.S) and linking,
#   - the vDSO (-DBUILD_VDSO) and the decompressor (arch/x86/boot/): they
#     are linked apart from vmlinux and cannot call the kernel runtime
#     (task #47, TK 2026-11-13: GCC for now),
#   - 16/32-bit boot and real-mode code (TzD is x86_64 only).
# Functions written in or called from assembly use the native calling
# convention: tests/kernel/native.list (-ftzd-native).
T=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); R=$T/../..
G=$R/work/build-c/gcc
src= ; compile=0 ; bits64=1 ; apart=0 ; asm_out=0 ; stdin_src=0
for a in "$@"; do
  case "$a" in
    -c) compile=1 ;;
    -S) asm_out=1 ;;
    -) stdin_src=1 ;;
    -m16|-m32) bits64=0 ;;
    -DBUILD_VDSO) apart=1 ;;
    *.c) src=$a ;;
  esac
done
# Kconfig capability probes arrive as: $(CC) -x c - -S -o /dev/null
# The only such probe in this kernel is CC_HAS_NAMED_AS (__seg_gs/__seg_fs).
# cc1tzd accepts the basic probe tokens but fails on actual macro expansions
# in percpu.h (typeof+address_space+pointer in nested macro context).
# Report unsupported so CC_HAS_NAMED_AS=n → USE_X86_SEG_SUPPORT=n, which
# selects the inline-asm percpu path that cc1tzd handles correctly.
[ $asm_out = 1 ] && [ $stdin_src = 1 ] && exit 1
case "$src" in */arch/x86/boot/*|arch/x86/boot/*) apart=1 ;; esac
if [ $compile = 1 ] && [ -n "$src" ] && [ $bits64 = 1 ] && [ $apart = 0 ]; then
  # tzd-kernel.h: the TzD directives the kernel needs, in one place.
  # -fgnu89-inline (TK 2026-11-18, task #42): the kernel Makefile does not
  # pass it and relies on GCC always inlining a bare "inline" function
  # (lib/decompress_unlzo.c's parse_header and others); TzD's bounds
  # checks and wipes make such a function bigger, so cc1tzd sometimes
  # declines to inline it, and the strict C99 rule (no call outside an
  # inlined site needs a body) then leaves it undefined at link time.
  # gnu89 semantics always emit a body, matching what the kernel source
  # assumes.
  exec "$R/bin/tzd" --host -ftzd-kernel -ftzd-native="$T/native.list" \
    -fgnu89-inline -D__TZD__ -include "$T/tzd-kernel.h" "$@"
fi
exec "$G/xgcc" -B"$G/" "$@"
