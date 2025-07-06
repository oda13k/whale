
#ifndef WHALE_VECTOR_H
#define WHALE_VECTOR_H

#include <whale/types.h>
#include <whale/utils.h>

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
    {                                                                          \
        (_vec)->data = calloc(_initial_capacity, sizeof(*(_vec)->data));       \
        WH_ASSERT((_vec)->data);                                               \
        (_vec)->capacity = _initial_capacity;                                  \
        (_vec)->count = 0;                                                     \
    }

#define VEC_INIT(_vec) VEC_INIT_SIZED(VEC_DEFAULT_INIT_CAPACITY, _vec)

#define VEC_DESTROY(_vec)                                                      \
    {                                                                          \
        free((_vec)->data);                                                    \
        (_vec)->data = nullptr;                                                \
        (_vec)->capacity = (_vec)->count = 0;                                  \
    }

#define VEC_PUSH(_elem, _vec)                                                  \
    {                                                                          \
        if ((_vec)->count < (_vec)->capacity)                                  \
        {                                                                      \
            (_vec)->data[(_vec)->count++] = _elem;                             \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            (_vec)->data = realloc(                                            \
                (_vec)->data,                                                  \
                (_vec)->capacity * VEC_DEFAULT_REALLOC_MULTIPLE *              \
                    sizeof(*(_vec)->data)                                      \
            );                                                                 \
            WH_ASSERT((_vec)->data);                                           \
            (_vec)->capacity *= VEC_DEFAULT_REALLOC_MULTIPLE;                  \
            (_vec)->data[(_vec)->count++] = _elem;                             \
        }                                                                      \
    }

#define VEC_REMOVE(_elem, _vec)                                                \
    {                                                                          \
        size_t i = 0;                                                          \
        for (; i < (_vec)->count; ++i)                                         \
        {                                                                      \
            if ((_vec)->data[i] == _elem)                                      \
            {                                                                  \
                --(_vec)->count;                                               \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        for (; i < (_vec)->count; ++i)                                         \
            (_vec)->data[i] = (_vec)->data[i + 1];                             \
    }

#endif // !WHALE_VECTOR_H
