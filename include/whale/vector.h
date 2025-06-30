
#ifndef WHALE_VECTOR_H
#define WHALE_VECTOR_H

#include <whale/types.h>

#define VEC(_type)                                                             \
    struct                                                                     \
    {                                                                          \
        _type* data;                                                           \
        size_t capacity;                                                       \
        size_t count;                                                          \
    }

#define VEC_FOR_EACH(_vec, _elem)                                              \
    for (typeof(_vec.data) _elem = _vec.data; _elem < _vec.data + _vec.count;  \
         ++_elem)

#define VEC_INIT_SIZED(_initial_capacity, _vec)                                \
    vec_init_sized_impl(sizeof(typeof(*(_vec.data))), _initial_capacity, _vec)

#define VEC_PUSH(_vec)

int vec_init_sized_impl(size_t data_size, size_t initial_capacity, void* vec);

int vec_push_impl(void* data, void* vec);

#endif // !WHALE_VECTOR_H
