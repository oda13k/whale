/**
 * Copyright Olaru Alexandru.
 * Distributed under the MIT license.
 */

#ifndef WHALE_VECTOR_H
#define WHALE_VECTOR_H

#include <stdlib.h>
#include <whale/debug.h>
#include <whale/types.h>

#define VEC_DEFAULT_INIT_CAPACITY 4
#define VEC_DEFAULT_REALLOC_MULTIPLE 2

#define VEC(_type)                                                             \
    struct                                                                     \
    {                                                                          \
        _type* data;                                                           \
        size_t capacity;                                                       \
        size_t count;                                                          \
    }

#define VEC_FOR_EACH(_elem, _vec)                                              \
    for (typeof((_vec)->data) _elem = (_vec)->data;                            \
         _elem < (_vec)->data + (_vec)->count;                                 \
         ++_elem)

#define VEC_FOR_EACH_REVERSE(_elem, _vec)                                      \
    for (typeof((_vec)->data) _elem = (_vec)->data + (_vec)->count - 1;        \
         (_vec)->count > 0 && _elem >= (_vec)->data;                           \
         --_elem)

#define VEC_INIT_SIZED(_initial_capacity, _vec)                                \
    ({                                                                         \
        int _st = 0;                                                           \
        (_vec)->data = calloc(_initial_capacity, sizeof(*(_vec)->data));       \
        if ((_vec)->data)                                                      \
        {                                                                      \
            (_vec)->capacity = _initial_capacity;                              \
            (_vec)->count = 0;                                                 \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            _st = -1;                                                          \
        }                                                                      \
        _st;                                                                   \
    })

#define VEC_INIT(_vec) VEC_INIT_SIZED(VEC_DEFAULT_INIT_CAPACITY, _vec)

#define VEC_DESTROY(_vec)                                                      \
    ({                                                                         \
        if ((_vec)->data)                                                      \
        {                                                                      \
            free((_vec)->data);                                                \
            (_vec)->data = nullptr;                                            \
        }                                                                      \
        (_vec)->capacity = (_vec)->count = 0;                                  \
    })

#define VEC_PUSH(_elem, _vec)                                                  \
    ({                                                                         \
        int _st = 0;                                                           \
        if ((_vec)->count < (_vec)->capacity)                                  \
        {                                                                      \
            (_vec)->data[(_vec)->count++] = _elem;                             \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            void* _tmp = realloc(                                              \
                (_vec)->data,                                                  \
                (_vec)->capacity * VEC_DEFAULT_REALLOC_MULTIPLE *              \
                    sizeof(*(_vec)->data)                                      \
            );                                                                 \
            if (_tmp)                                                          \
            {                                                                  \
                (_vec)->data = _tmp;                                           \
                (_vec)->capacity *= VEC_DEFAULT_REALLOC_MULTIPLE;              \
                (_vec)->data[(_vec)->count++] = _elem;                         \
            }                                                                  \
            else                                                               \
            {                                                                  \
                _st = -1;                                                      \
            }                                                                  \
        }                                                                      \
        _st;                                                                   \
    })

#define VEC_REMOVE(_elem, _vec)                                                \
    ({                                                                         \
        size_t _i = 0;                                                         \
        for (; _i < (_vec)->count; ++_i)                                       \
        {                                                                      \
            if ((_vec)->data[_i] == _elem)                                     \
            {                                                                  \
                --(_vec)->count;                                               \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        for (; _i < (_vec)->count; ++_i)                                       \
            (_vec)->data[_i] = (_vec)->data[_i + 1];                           \
    })

#define VEC_REMOVE_AT(_idx, _vec)                                              \
    ({                                                                         \
        WH_ASSERT(_idx < (_vec)->count);                                       \
        --(_vec)->count;                                                       \
        for (size_t _i = _idx; _i < (_vec)->count; ++_i)                       \
            (_vec)->data[_i] = (_vec)->data[_i + 1];                           \
    })

#define VEC_AT(_idx, _vec) ((_vec)->data[_idx])

#define VEC_GET_LENGTH(_vec) ((_vec)->count)

#define VEC_GET_CAPACITY(_vec) ((_vec)->capacity)

#define VEC_INCLUDES(_elem, _vec)                                              \
    ({                                                                         \
        bool _includes = false;                                                \
        VEC_FOR_EACH (_e, _vec)                                                \
        {                                                                      \
            if (*_e == _elem)                                                  \
            {                                                                  \
                _includes = true;                                              \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        _includes;                                                             \
    })

#define VEC_CLEAR(_vec) ({ (_vec)->count = 0; })

#endif // !WHALE_VECTOR_H
