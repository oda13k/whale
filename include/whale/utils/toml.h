
#ifndef WHALE_TOML_H
#define WHALE_TOML_H

#include <stdint.h>
#include <whale/utils/hashtable.h>
#include <whale/utils/vector.h>

typedef enum
{
    TOML_TYPE_ARRAY,
    TOML_TYPE_TABLE,
    TOML_TYPE_STRING,
    TOML_TYPE_INT64,
    TOML_TYPE_FLOAT64,
    TOML_TYPE_BOOL
} TOML_Type;

typedef struct
{
    TOML_Type type;
    void* data;
} TOML_Object;

typedef VEC(TOML_Object) TOML_Array;

// typedef HashTable TOML_Table;

typedef struct
{

} TOML_Context;

TOML_Context* toml_create_ctx();

void toml_destroy_ctx(TOML_Context* ctx);

int toml_parse_from_file(const char* filepath, TOML_Context* ctx);

#endif // !WHALE_TOML_H
