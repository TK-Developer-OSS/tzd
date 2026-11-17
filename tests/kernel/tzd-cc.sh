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
src= ; compile=0 ; bits64=1 ; apart=0
for a in "$@"; do
  case "$a" in
    -c) compile=1 ;;
    -m16|-m32) bits64=0 ;;
    -DBUILD_VDSO) apart=1 ;;
    *.c) src=$a ;;
  esac
done
case "$src" in */arch/x86/boot/*|arch/x86/boot/*) apart=1 ;; esac
if [ $compile = 1 ] && [ -n "$src" ] && [ $bits64 = 1 ] && [ $apart = 0 ]; then
  # tzd-kernel.h: the TzD directives the kernel needs, in one place.
  exec "$R/bin/tzd" --host -ftzd-kernel -ftzd-native="$T/native.list" \
    -include "$T/tzd-kernel.h" "$@"
fi
exec "$G/xgcc" -B"$G/" "$@"
