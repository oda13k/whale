
#ifndef WHALE_TYPES_H
#define WHALE_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define KIB (1024)
#define MIB (1024 * 1024)

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
typedef double wh_coord_t;

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

#define S32_MAX_VALUE (2'147'483'647)

// #define DIM2COORD_CAST(_dim) ({ WH_ASSERT(_dim <= S32_MAX_VALUE);
// (wh_coord_t)(_dim); })

#define LISTEN(signal, listener, cb)                                           \
    wl_signal_add(signal, ((listener)->notify = cb, listener))

#define UNLISTEN(listener) wl_list_remove(&(listener)->link)

#define WH_CALLBACK(_name, _listener, _data)                                   \
    static struct wl_listener _g_listener_on_##_name;                          \
    static void _on_##_name(_listener, _data)

#define WH_LISTEN(_signal, _name)                                              \
    LISTEN(_signal, &_g_listener_on_##_name, _on_##_name)

#define WH_UNLISTEN(_name) wl_list_remove(&(_g_listener_on_##_name).link)

#endif // !WHALE_TYPES_H
