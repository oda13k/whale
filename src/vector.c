
#include <errno.h>
#include <stdlib.h>
#include <whale/vector.h>

typedef VEC(void*) generic_vector_t;

int vec_init_sized_impl(size_t data_size, size_t initial_capacity, void* vec)
{
    VEC(void*)* generic = vec;

    generic->data = calloc(initial_capacity, data_size);
    if (!generic->data)
        return -ENOMEM;

    generic->count = 0;
    generic->capacity = initial_capacity;
    return 0;
}

int vec_push_impl(void*, void*)
{
    // VEC(void*)* generic = vec;

    return 0;
}
