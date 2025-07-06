
#ifndef WHALE_UTILS_H
#define WHALE_UTILS_H

#include <assert.h>
#include <whale/types.h>

#define WH_ASSERT(expr) wh_assert(expr, #expr, __FILE__, __LINE__)

#define WH_ASSERT_DEBUG(expr) wh_assert(expr, #expr, __FILE__, __LINE__)

void wh_assert(bool expr, const char* str_expr, const char* file, size_t line);

/**
 * Get a monotonic timestamp in milliseconds. Since when is
 * the timestamp? Probably since the system started.
 */
u64 wh_time_monotonic_now_ms();

int wh_spawn_process(const char* path);

#endif // !WHALE_UTILS_H
