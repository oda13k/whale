
#ifndef WHALE_TYPES_H
#define WHALE_TYPES_H

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

#define WH_SIZE_UNDEFINED 0
typedef u32 wh_dim_t;
typedef s32 wh_coord_t;

typedef struct
{
    wh_coord_t x;
    wh_coord_t y;
} WhalePosition2D;

typedef struct
{
    wh_dim_t w;
    wh_dim_t h;
} WhaleSize2D;

typedef struct
{
    WhalePosition2D pos;
    WhaleSize2D size;
} WhaleGeometry2D;

#define LISTEN(signal, listener, cb)                                           \
    wl_signal_add(signal, ((listener)->notify = cb, listener))

#define UNLISTEN(listener) wl_list_remove(&(listener)->link)

#endif // !WHALE_TYPES_H
