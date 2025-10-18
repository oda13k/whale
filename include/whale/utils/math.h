
#ifndef WHALE_UTILS_MATH_H
#define WHALE_UTILS_MATH_H

#include <whale/debug.h>
#include <whale/log.h>

#define MIN2(_x, _y) ((_x) < (_y) ? (_x) : (_y))
#define MAX2(_x, _y) ((_x) > (_y) ? (_x) : (_y))
#define CLAMP(_lb, _x, _ub)                                                    \
    ({                                                                         \
        if (UNLIKELY((_lb) > (_ub)))                                           \
            wh_log(ERR, "clamp: Lower bound is bigger than upper bound.");     \
        auto _ret = _x;                                                        \
        if (_ret < (_lb))                                                      \
            _ret = (_lb);                                                      \
        else if (_ret > (_ub))                                                 \
            _ret = (_ub);                                                      \
        _ret;                                                                  \
    })

#endif // !WHALE_UTILS_MATH_H
