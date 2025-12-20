/**
 * Copyright Olaru Alexandru.
 * Licensed under the MIT license.
 */

/* This parser is not 100% on spec. It does things I want it to do/doesn't do
 * things I find pointless for my use case. It's based on the 1.0.0 spec and
 * differs by the following:
 * Inline tables can span multiple lines (now supported in TOML 1.1.0)
 * Doesn't support dates/times
 * Leading zeros are allowed for all bases
 * Doesn't support +-inf and +-nan (yet?)
 * Doesn't support scientific notation,
 * Doesn't support utf8 (yet)
 * Doesn't support multi-line strings (yet?)
 * Table redefinitions using dotted keys/headers differs in a way
 *   that is so esoteric it's not even worth explaining
 * Probably some other stuff
 */

#ifndef WHALE_TOML_H
#define WHALE_TOML_H

#include <stdio.h>
#include <whale/utils/vector.h>

#define TOML_CONTEXT_ERR_LENGTH 128

typedef enum
{
    TOML_TYPE_ARRAY,
    TOML_TYPE_TABLE,
    TOML_TYPE_STRING,
    TOML_TYPE_INT64,
    TOML_TYPE_FLOAT64,
    TOML_TYPE_BOOL
} TOML_Type;

struct toml_table;

typedef struct toml_value
{
    TOML_Type type;
    union
    {
        VEC(struct toml_value) array;
        struct toml_table* table;
        char* string;
        s64 int64;
        f64 float64;
        bool boolean;
    } data;
} TOML_Value;

typedef struct
{
    char* key;
    TOML_Value value;
} TOML_KeyValue;

typedef struct toml_table
{
    bool explicitely_defined;
    VEC(TOML_KeyValue) keyvalues;
} TOML_Table;

typedef struct
{
    TOML_Table* root_table;
    struct
    {
        FILE* f;
        char c;
        size_t row;
        size_t col;
        char error[TOML_CONTEXT_ERR_LENGTH];
        TOML_Table* active_table;
    } parser;
} TOML_Context;

TOML_Context* toml_create_ctx();
void toml_release_ctx(TOML_Context* ctx);

int toml_parse_from_file(const char* filepath, TOML_Context* ctx);

const TOML_Value* toml_get_value(const char* key, const TOML_Context* ctx);

const char* toml_get_error(const TOML_Context* ctx);

#endif // !WHALE_TOML_H
