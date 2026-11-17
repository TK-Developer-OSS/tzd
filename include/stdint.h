// SPDX-License-Identifier: MIT
/* TzD stdint.  The exact-width types are the built-in fixed-width types
   (int8 ... uint64, task #34): their width never depends on the target.  */
#ifndef _TZD_STDINT_H
#define _TZD_STDINT_H
typedef int8   int8_t;   typedef uint8  uint8_t;
typedef int16  int16_t;  typedef uint16 uint16_t;
typedef int32  int32_t;  typedef uint32 uint32_t;
typedef int64  int64_t;  typedef uint64 uint64_t;
typedef int64  intmax_t; typedef uint64 uintmax_t;
typedef int64  intptr_t; typedef uint64 uintptr_t;
#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535
#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  2147483647
#define UINT32_MAX 4294967295U
#define INT64_MIN  (-9223372036854775807L - 1)
#define INT64_MAX  9223372036854775807L
#define UINT64_MAX 18446744073709551615UL
#define SIZE_MAX   UINT64_MAX
#endif
