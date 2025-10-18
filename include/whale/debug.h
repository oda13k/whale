
#ifndef WHALE_DEBUG_H
#define WHALE_DEBUG_H

#include <limits.h>
#include <whale/log.h>
#include <whale/types.h>

#define WH_ASSERT(_expr)                                                       \
    if (!(_expr))                                                              \
    wh_die(                                                                    \
        true,                                                                  \
        "Assertion failed: '%s' in file %s:%zu.",                              \
        #_expr,                                                                \
        __FILE__,                                                              \
        (size_t)__LINE__                                                       \
    )

#define WH_ASSERT_NOT_REACHED()                                                \
    wh_die(                                                                    \
        true,                                                                  \
        "Assertion failed: Reached unreachable code path in file %s:%zu.",     \
        __FILE__,                                                              \
        (size_t)__LINE__                                                       \
    )

#if WHALE_DEBUG == 1
#define WH_ASSERT_SANITY(_expr) WH_ASSERT(_expr)
#else
#define WH_ASSERT_SANITY(_expr) (_expr)
#endif

#define WH_PRAGMA(_x) _Pragma(#_x)

#define WH_NOWARN(_warn, ...)                                                  \
    WH_PRAGMA(GCC diagnostic push)                                             \
    WH_PRAGMA(GCC diagnostic ignored _warn)                                    \
    __VA_ARGS__                                                                \
    WH_PRAGMA(GCC diagnostic pop)

#define CAST_DBL_TO_INT(_dbl)                                                  \
    ({                                                                         \
        _Static_assert(                                                        \
            _Generic((_dbl), double: 1, default: 0),                           \
            "Non double passed to CAST_DBL_TO_INT"                             \
        );                                                                     \
        int _ret = (int)(_dbl);                                                \
        WH_ASSERT((_dbl) <= INT_MAX && (_dbl) >= INT_MIN);                     \
        _ret;                                                                  \
    })

#define CAST_U32_TO_S32(_u32)                                                  \
    ({                                                                         \
        _Static_assert(                                                        \
            _Generic((_u32), u32: 1, default: 0),                              \
            "Non u32 passed to CAST_U32_TO_S32"                                \
        );                                                                     \
        s32 _ret = (s32)(_u32);                                                \
        WH_ASSERT((_u32) <= INT32_MAX);                                        \
        _ret;                                                                  \
    })

#define CAST_S32_TO_U32(_s32)                                                  \
    ({                                                                         \
        _Static_assert(                                                        \
            _Generic((_s32), s32: 1, default: 0),                              \
            "Non s32 passed to CAST_S32_TO_U32"                                \
        );                                                                     \
        WH_ASSERT((_s32) >= 0);                                                \
        (u32)(_s32);                                                           \
    })

[[noreturn]] [[gnu::format(printf, 2, 3)]] void
wh_die(bool print_call_trace, const char* fmt, ...);

void wh_debug_register_crash_handlers();

void wh_debug_print_stack_trace(LogLevel log_level);

#endif // !WHALE_DEBUG_H
