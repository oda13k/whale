
#ifndef WHALE_VECTOR_H
#define WHALE_VECTOR_H

#include <whale/types.h>
#include <whale/utils.h>

#define VEC(_type)                                                             \
    struct                                                                     \
    {                                                                          \
        _type* data;                                                           \
        size_t capacity;                                                       \
        size_t count;                                                          \
    }

#define VEC_FOR_EACH(_elem, _vec)                                              \
    for (typeof((_vec)->data) _elem = (_vec)->data; _elem < (_vec)->data + (_vec)->count;  \
         ++_elem)

#define VEC_INIT_SIZED(_initial_capacity, _vec)                                \
{ \
    (_vec)->data = calloc(_initial_capacity, sizeof(*(_vec)->data)); \
    WH_ASSERT((_vec)->data); \
    (_vec)->capacity = _initial_capacity; \
    (_vec)->count = 0; \
}

#define VEC_DESTROY(_vec) { free((_vec)->data); (_vec)->data = nullptr; (_vec)->capacity = (_vec)->count = 0; }

#define VEC_PUSH(_elem, _vec) \
{ \
    if ((_vec)->count < (_vec)->capacity) { \
        (_vec)->data[(_vec)->count++] = _elem; \
    } \
    else { \
        (_vec)->data = realloc((_vec)->data, (_vec)->capacity * 2 * sizeof(*(_vec)->data)); \
        WH_ASSERT((_vec)->data); \
        (_vec)->capacity *= 2; \
        (_vec)->data[(_vec)->count++] = _elem; \
    } \
}

#define VEC_REMOVE(_elem, _vec) \
{ \
    size_t i = 0; \
    for (; i < (_vec)->count; ++i) { \
        if ((_vec)->data[i] == _elem) { \
            --(_vec)->count; \
            break; \
        } \
    } \
    for (; i < (_vec)->count; ++i) \
        (_vec)->data[i] = (_vec)->data[i + 1]; \
}

#endif // !WHALE_VECTOR_H
