
#ifndef WHALE_DEBUG_H
#define WHALE_DEBUG_H

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

#if WHALE_TARGET == debug
#define WH_ASSERT_SANITY(_expr) WH_ASSERT(_expr)
#else
#define WH_ASSERT_SANITY(_expr)
#endif

__attribute__((format(printf, 2, 3))) __attribute__((noreturn)) void
wh_die(bool print_call_trace, const char* fmt, ...);

void wh_debug_register_crash_handlers();

void wh_debug_print_stack_trace(LogLevel log_level);

#endif // !WHALE_DEBUG_H
