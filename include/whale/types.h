
#ifndef _WHALE_TYPES_H
#define _WHALE_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef double wh_coord_t;

typedef struct
{
    wh_coord_t x;
    wh_coord_t y;
} wh_pos2d_t;

#define WH_SIZE_UNDEFINED 0
typedef u32 wh_size_t;

typedef struct
{
    wh_size_t w;
    wh_size_t h;
} wh_size2d_t;

typedef struct
{
    wh_coord_t x;
    wh_coord_t y;

    wh_size_t w;
    wh_size_t h;
} WhaleGeometry2D;

#define LISTEN(signal, listener, cb)                                           \
    wl_signal_add(signal, ((listener)->notify = cb, listener))

#define UNLISTEN(listener) wl_list_remove(&(listener)->link)

#endif // !_WHALE_TYPES_H
