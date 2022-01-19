#pragma once

#include <stdint.h>

typedef uint64_t   u64;
typedef int64_t    s64;
typedef uint32_t   u32;
typedef int32_t    s32;
typedef uint16_t   u16;
typedef int16_t    s16;
typedef uint8_t    u8;
typedef int8_t     s8;

typedef float      float32;
typedef double     float64;


#define For_Count(a, x) for (s32 x = 0; (x) < (a); (x)++)
#define For_Range(a, b, x) for (s32 x = (a); (x) < (b); (x)++)
