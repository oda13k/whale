/**
 * Copyright Olaru Alexandru.
 * Licensed under the MIT license.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <whale/log.h>
#include <whale/utils/toml.h>

#define MAX_KEY_NAME 128
#define MAX_VALUE_LENGTH 1024

#define SYM_COMMENT '#'
#define SYM_TABLE_HEADER_START '['
#define SYM_TABLE_HEADER_END ']'
#define SYM_TABLE_SPLIT '.'
#define SYM_ASSIGN '='

#define IS_EOLEOF(_c) ((_c) == EOF || (_c) == '\n')

#define VAL2TABLE(_val) ((_val)->data.table)
#define VAL2STRING(_val) ((_val)->data.string)
#define VAL2INT64(_val) ((_val)->data.int64)
#define VAL2FLOAT64(_val) ((_val)->data.float64)
#define VAL2BOOL(_val) ((_val)->data.boolean)

#define CTX_MAKE_ERROR(_ctx, _fmt, ...)                                        \
    snprintf(                                                                  \
        (_ctx)->parser.error,                                                  \
        TOML_CONTEXT_ERR_LENGTH,                                               \
        _fmt "; row: %zu, col: %zu." __VA_OPT__(, ) __VA_ARGS__,               \
        (_ctx)->parser.row,                                                    \
        (_ctx)->parser.col                                                     \
    )

#define CTX_FGETC(_ctx)                                                        \
    ({                                                                         \
        (_ctx)->parser.c = fgetc((_ctx)->parser.f);                            \
        if ((_ctx)->parser.c == '\n')                                          \
        {                                                                      \
            ++(_ctx)->parser.row;                                              \
            (_ctx)->parser.col = 0;                                            \
        }                                                                      \
        else if ((_ctx)->parser.c != EOF)                                      \
        {                                                                      \
            ++(_ctx)->parser.col;                                              \
        }                                                                      \
        (_ctx)->parser.c;                                                      \
    })

#define CTX_SKIP_LINE(_ctx)                                                    \
    while (true)                                                               \
    {                                                                          \
        CTX_FGETC(_ctx);                                                       \
        if (IS_EOLEOF((_ctx)->parser.c))                                       \
            break;                                                             \
    }

#define CTX_FIND_INLINE_DELIMITER(_ctx)                                        \
    ({                                                                         \
        bool _ret = true;                                                      \
        while (true)                                                           \
        {                                                                      \
            CTX_FGETC((_ctx));                                                 \
            if ((_ctx)->parser.c == '\n' || is_whitespace((_ctx)->parser.c))   \
                continue;                                                      \
            if ((_ctx)->parser.c == ',' || (_ctx)->parser.c == '}')            \
                break;                                                         \
            _ret = false;                                                      \
            break;                                                             \
        }                                                                      \
        _ret;                                                                  \
    })

#define IS_UNQUOTED_CHAR_VALID(_c)                                             \
    ((_c >= '0' && _c <= '9') || (_c >= 'a' && _c <= 'z') ||                   \
     (_c >= 'A' && _c <= 'Z') || _c == '_' || _c == '-')

typedef struct
{
    char* key;
    TOML_Table* parent;
} KeyParseResponse;

static bool is_whitespace(char c)
{

    return c == ' ' || c == '\t';
}

static bool is_endoftoken(char c, TOML_Context* ctx)
{
    if (is_whitespace(c) || IS_EOLEOF(c))
    {
        return true;
    }
    else if (c == SYM_COMMENT)
    {
        CTX_SKIP_LINE(ctx);
        return true;
    }

    return false;
}

static void table_destroy(TOML_Table* table);

static void keyvalue_destroy(TOML_KeyValue* keyval)
{
    TOML_Value* val = &keyval->value;
    switch (val->type)
    {
    case TOML_TYPE_STRING:
        free(val->data.string);
        break;

    case TOML_TYPE_TABLE:
        table_destroy(val->data.table);
        break;

    case TOML_TYPE_ARRAY:
        WH_ASSERT_NOT_REACHED();
        break;

    default:
        break;
    }

    free(keyval->key);
}

static TOML_Table* table_create()
{
    TOML_Table* table = calloc(1, sizeof(TOML_Table));
    if (!table)
        return nullptr;

    if (VEC_INIT_SIZED(16, &table->keyvalues) < 0)
    {
        free(table);
        return nullptr;
    }

    return table;
}

static void table_destroy(TOML_Table* table)
{
    VEC_FOR_EACH (keyval, &table->keyvalues)
        keyvalue_destroy(keyval);

    VEC_DESTROY(&table->keyvalues);
    free(table);
}

static TOML_Table* get_or_create_table(
    const char* name,
    bool explicit,
    TOML_Table* parent,
    TOML_Context* ctx
)
{
    VEC_FOR_EACH (keyval, &parent->keyvalues)
    {
        if (strcmp(name, keyval->key) != 0)
            continue;

        if (keyval->value.type == TOML_TYPE_TABLE)
        {
            TOML_Table* table = VAL2TABLE(&keyval->value);
            /* Error out on redefinition */
            if (explicit && table->explicitely_defined)
            {
                CTX_MAKE_ERROR(
                    ctx, "Cannot redefine existing table '%s'", name
                );
                return nullptr;
            }

            if (!table->explicitely_defined && explicit)
                table->explicitely_defined = true;

            return table;
        }
        else
        {
            CTX_MAKE_ERROR(ctx, "Cannot redefine key '%s'", name);
            return nullptr;
        }
    }

    TOML_Table* table = table_create();
    if (!table)
    {
        CTX_MAKE_ERROR(ctx, "Internal error: Out of memory");
        return nullptr;
    }

    TOML_KeyValue keyval = {
        .key = strdup(name),
        .value = {.type = TOML_TYPE_TABLE, .data.table = table}
    };

    if (!keyval.key)
    {
        table_destroy(table);
        CTX_MAKE_ERROR(ctx, "Internal error: Out of memory.");
        return nullptr;
    }

    if (VEC_PUSH(keyval, &parent->keyvalues) < 0)
    {
        free(keyval.key);
        table_destroy(table);
        CTX_MAKE_ERROR(ctx, "Internal error: Out of memory.");
        return nullptr;
    }

    table->explicitely_defined = explicit;

    return table;
}

static KeyParseResponse
parse_key(int initial_char, int end_char, TOML_Table* parent, TOML_Context* ctx)
{
    char keyname[MAX_KEY_NAME + 1] = {0};
    size_t keyname_idx = 0;
    bool is_quoted = false;
    char quote_char = 0;
    bool waiting_for_end = false;

    while (is_whitespace(initial_char))
        initial_char = CTX_FGETC(ctx);

    if (initial_char == '"' || initial_char == '\'')
    {
        is_quoted = true;
        quote_char = initial_char;
    }
    else if (IS_UNQUOTED_CHAR_VALID(initial_char))
    {
        keyname[keyname_idx++] = initial_char;
    }
    else
    {
        CTX_MAKE_ERROR(ctx, "Key contains invalid character(s)");
        return (KeyParseResponse){0};
    }

    while (true)
    {
        CTX_FGETC(ctx);
        if (IS_EOLEOF(ctx->parser.c))
        {
            CTX_MAKE_ERROR(ctx, "Invalid character, expected '%c'", end_char);
            return (KeyParseResponse){0};
        }

        if (keyname_idx == MAX_KEY_NAME)
        {
            CTX_MAKE_ERROR(
                ctx, "Key exceeds max length of %d characters", MAX_KEY_NAME
            );
            return (KeyParseResponse){0};
        }

        if (is_quoted)
        {
            if (ctx->parser.c == quote_char)
            {
                is_quoted = false;
                waiting_for_end = true;
                continue;
            }
        }
        else
        {
            if (ctx->parser.c == SYM_TABLE_SPLIT)
            {
                TOML_Table* table =
                    get_or_create_table(keyname, false, parent, ctx);
                if (!table)
                    return (KeyParseResponse){0};

                return parse_key(CTX_FGETC(ctx), end_char, table, ctx);
            }
            else if (is_whitespace(ctx->parser.c))
            {
                waiting_for_end = true;
                continue;
            }
            else if (ctx->parser.c == end_char)
            {
                break;
            }
            else if (waiting_for_end)
            {
                CTX_MAKE_ERROR(
                    ctx, "Invalid character, expected '%c'", end_char
                );
                return (KeyParseResponse){0};
            }
            else if (!IS_UNQUOTED_CHAR_VALID(ctx->parser.c))
            {
                CTX_MAKE_ERROR(ctx, "Key contains invalid character(s)");
                return (KeyParseResponse){0};
            }
        }

        keyname[keyname_idx++] = ctx->parser.c;
    }

    return (KeyParseResponse){.key = strdup(keyname), .parent = parent};
}

static s64 string_escape_sequence(TOML_Context* ctx)
{
    switch (CTX_FGETC(ctx))
    {
    case '\\':
        return '\\';
    case 'b':
        return '\b';
    case 't':
        return '\t';
    case 'n':
        return '\n';
    case 'f':
        return '\f';
    case 'r':
        return '\r';
    case '"':
        return '"';

    case 'u':
    case 'U':
        CTX_MAKE_ERROR(
            ctx,
            "Internal error: Use of unimplemented feature (unicode escape sequences)"
        );
        return -1;

    default:
        CTX_MAKE_ERROR(ctx, "Use of invalid escape sequence");
        return -1;
    }
}

static int parse_value_string(
    TOML_Value* val,
    char quoute_char,
    bool is_inline,
    TOML_Context* ctx
)
{
    /* Missing features: Multiline strings, unicode escape sequences. */
    char value[MAX_VALUE_LENGTH] = {0};
    size_t value_idx = 0;

    while (true)
    {
        CTX_FGETC(ctx);
        if (IS_EOLEOF(ctx->parser.c))
        {
            CTX_MAKE_ERROR(ctx, "Unexpected end of value");
            return -1;
        }

        if (value_idx == MAX_VALUE_LENGTH)
        {
            CTX_MAKE_ERROR(
                ctx,
                "Value exceeds max length of %d characters",
                MAX_VALUE_LENGTH
            );
            return -1;
        }

        // Only double-quote strings can be escaped
        if (ctx->parser.c == '\\' && quoute_char == '"')
        {
            s64 seq = string_escape_sequence(ctx);
            if (seq < 0)
                return -1;

            value[value_idx++] = (u8)seq;
            continue;
        }

        if (ctx->parser.c == quoute_char)
            break;

        value[value_idx++] = ctx->parser.c;
    }

    if (is_inline && !CTX_FIND_INLINE_DELIMITER(ctx))
    {
        CTX_MAKE_ERROR(ctx, "Unexpected character, expected ',' or '}'");
        return -1;
    }

    val->type = TOML_TYPE_STRING;
    val->data.string = strdup(value);
    if (!val->data.string)
    {
        CTX_MAKE_ERROR(ctx, "Internal error: Out of memory");
        return -1;
    }

    return 0;
}

static int parse_value_boolean(
    TOML_Value* val,
    char initial_char,
    bool is_inline,
    TOML_Context* ctx
)
{
    if (initial_char == 't')
    {
        if (CTX_FGETC(ctx) != 'r' || CTX_FGETC(ctx) != 'u' ||
            CTX_FGETC(ctx) != 'e')
            goto error;

        val->type = TOML_TYPE_BOOL;
        val->data.boolean = true;
    }
    else if (initial_char == 'f')
    {
        if (CTX_FGETC(ctx) != 'a' || CTX_FGETC(ctx) != 'l' ||
            CTX_FGETC(ctx) != 's' || CTX_FGETC(ctx) != 'e')
            goto error;

        val->type = TOML_TYPE_BOOL;
        val->data.boolean = false;
    }

    if (is_inline)
    {
        if (!CTX_FIND_INLINE_DELIMITER(ctx))
        {
            CTX_MAKE_ERROR(ctx, "Unexpected character, expected ',' or '}'");
            return -1;
        }
    }
    else if (!is_endoftoken(CTX_FGETC(ctx), ctx))
    {
        goto error;
    }

    return 0;

error:
    CTX_MAKE_ERROR(ctx, "Unexpected value, expected true or false");
    return -1;
}

static int parse_value_numeric(
    TOML_Value* val,
    char initial_char,
    bool is_inline,
    TOML_Context* ctx
)
{
    /* Missing features: floats with exponents, +-inf, +-nan, trailing
     * underscores are allowed, time/date */

    char value[MAX_VALUE_LENGTH] = {0};
    size_t value_idx = 0;

    bool is_float = false;
    u8 base = 10;

    if (initial_char == '+' || initial_char == '-')
    {
        value[value_idx++] = initial_char;
        initial_char = CTX_FGETC(ctx);
    }

    if (initial_char == '0')
    {
        CTX_FGETC(ctx);
        if (ctx->parser.c == 'x')
        {
            base = 16;
        }
        else if (ctx->parser.c == 'o')
        {
            base = 8;
        }
        else if (ctx->parser.c == 'b')
        {
            base = 2;
        }
        else if (ctx->parser.c == '.')
        {
            value[value_idx++] = '.';
            is_float = true;
        }
        else if ((is_inline &&
                  (ctx->parser.c == ',' || ctx->parser.c == '}')) ||
                 is_endoftoken(ctx->parser.c, ctx))
        {
            goto done;
        }
        else
        {
            CTX_MAKE_ERROR(ctx, "Leading zeros are not allowed");
            return -1;
        }
    }
    else if (initial_char != '_')
    {
        goto process;
    }
    else
    {
        CTX_MAKE_ERROR(ctx, "Leading underscores are not allowed");
        return -1;
    }

    while (true)
    {
        CTX_FGETC(ctx);

    process:

        if ((is_inline && (ctx->parser.c == ',' || ctx->parser.c == '}')) ||
            is_endoftoken(ctx->parser.c, ctx))
            break;

        if (value_idx == MAX_VALUE_LENGTH)
        {
            CTX_MAKE_ERROR(
                ctx,
                "Value exceeds max length of %d characters",
                MAX_VALUE_LENGTH
            );
            return -1;
        }

        if (ctx->parser.c == '_')
            continue;

        if (ctx->parser.c == '.')
        {
            if (!is_float && base == 10)
            {
                value[value_idx++] = '.';
                is_float = true;
                continue;
            }

            CTX_MAKE_ERROR(ctx, "Unexpected character");
            return -1;
        }

        switch (base)
        {
        case 10:
            if (ctx->parser.c < '0' || ctx->parser.c > '9')
            {
                CTX_MAKE_ERROR(ctx, "Unexpected character");
                return -1;
            }
            break;

        case 16:
            if (!strchr("0123456789aAbBcCdDeEfF", ctx->parser.c))
            {
                CTX_MAKE_ERROR(ctx, "Unexpected character");
                return -1;
            }
            break;

        case 8:
            if (ctx->parser.c < '0' || ctx->parser.c > '7')
            {
                CTX_MAKE_ERROR(ctx, "Unexpected character");
                return -1;
            }
            break;

        case 2:
            if (ctx->parser.c != '0' && ctx->parser.c != '1')
            {
                CTX_MAKE_ERROR(ctx, "Unexpected character");
                return -1;
            }
            break;

        default:
            WH_ASSERT_NOT_REACHED();
        }

        value[value_idx++] = ctx->parser.c;
    }

done:
    if (is_inline && ctx->parser.c != ',' && ctx->parser.c != '}')
    {
        if (!CTX_FIND_INLINE_DELIMITER(ctx))
        {
            CTX_MAKE_ERROR(ctx, "Unexpected character, expected ',' or '}'");
            return -1;
        }
    }

    errno = 0;

    if (is_float)
    {
        val->type = TOML_TYPE_FLOAT64;
        val->data.float64 = strtod(value, NULL);
    }
    else
    {
        val->type = TOML_TYPE_INT64;
        val->data.int64 = strtoll(value, NULL, base);
    }

    if (errno == ERANGE)
    {
        CTX_MAKE_ERROR(ctx, "Numeric value too large");
        return -1;
    }

    WH_ASSERT(errno == 0);
    return 0;
}

static int
parse_keyvalue_pair(TOML_Table* parent, bool is_inline, TOML_Context* ctx);

static int
parse_value_inline_table(TOML_Value* val, bool is_inline, TOML_Context* ctx)
{
    TOML_Table* table = table_create();
    if (!table)
    {
        CTX_MAKE_ERROR(ctx, "Internal error: Out of memory");
        return -1;
    }

    while (true)
    {
        CTX_FGETC(ctx);

        if (ctx->parser.c == EOF)
        {
            table_destroy(table);
            CTX_MAKE_ERROR(ctx, "Unexpected end of file");
            return -1;
        }

        if (is_endoftoken(ctx->parser.c, ctx))
            continue;

        if (ctx->parser.c == '}')
            break;

        if (parse_keyvalue_pair(table, true, ctx) < 0)
        {
            table_destroy(table);
            return -1;
        }
        else if (ctx->parser.c == '}')
        {
            break;
        }
    }

    if (is_inline && !CTX_FIND_INLINE_DELIMITER(ctx))
    {
        CTX_MAKE_ERROR(ctx, "Unexpected character, expected ',' or '}'");
        return -1;
    }

    val->type = TOML_TYPE_TABLE;
    val->data.table = table;
    return 0;
}

static int parse_value_array(TOML_Value*, TOML_Context* ctx)
{
    CTX_MAKE_ERROR(
        ctx, "Internal error: Use of unimplemented feature (arrays)"
    );
    return -1;
}

static int parse_table_header(TOML_Context* ctx)
{
    KeyParseResponse res =
        parse_key(CTX_FGETC(ctx), SYM_TABLE_HEADER_END, ctx->root_table, ctx);
    if (!res.key)
        return -1;

    TOML_Table* table = get_or_create_table(res.key, true, res.parent, ctx);
    if (!table)
    {
        free(res.key);
        return -1;
    }

    ctx->parser.active_table = table;

    return 0;
}

static int parse_value(TOML_Value* val, bool is_inline, TOML_Context* ctx)
{
    while (is_whitespace(CTX_FGETC(ctx)))
        ;

    if (ctx->parser.c == '"' || ctx->parser.c == '\'')
        return parse_value_string(val, ctx->parser.c, is_inline, ctx);
    else if (ctx->parser.c == 't' || ctx->parser.c == 'f')
        return parse_value_boolean(val, ctx->parser.c, is_inline, ctx);
    else if (ctx->parser.c == '{')
        return parse_value_inline_table(val, is_inline, ctx);
    else if (ctx->parser.c == '[')
        return parse_value_array(val, ctx);
    else if (!IS_EOLEOF(ctx->parser.c))
        return parse_value_numeric(val, ctx->parser.c, is_inline, ctx);

    CTX_MAKE_ERROR(ctx, "Unexpected end of line, expected value");
    return -1;
}

static int
parse_keyvalue_pair(TOML_Table* parent, bool is_inline, TOML_Context* ctx)
{
    KeyParseResponse res = parse_key(ctx->parser.c, SYM_ASSIGN, parent, ctx);
    if (!res.key)
        return -1;

    VEC_FOR_EACH (keyval, &res.parent->keyvalues)
    {
        if (strcmp(keyval->key, res.key) == 0)
        {
            CTX_MAKE_ERROR(ctx, "Cannot redefine key '%s'", res.key);
            free(res.key);
            return -1;
        }
    }

    TOML_Value val;
    if (parse_value(&val, is_inline, ctx) < 0)
    {
        free(res.key);
        return -1;
    }

    TOML_KeyValue keyval = {.key = res.key, .value = val};
    VEC_PUSH(keyval, &res.parent->keyvalues);

    return 0;
}

static void print_value(TOML_KeyValue* keyval, size_t indent)
{
    for (size_t i = 0; i < indent; ++i)
        printf(" ");

    const char* type_table[] = {
        [TOML_TYPE_TABLE] = "(t)",
        [TOML_TYPE_ARRAY] = "(a)",
        [TOML_TYPE_STRING] = "(s)",
        [TOML_TYPE_INT64] = "(i)",
        [TOML_TYPE_FLOAT64] = "(f)",
        [TOML_TYPE_BOOL] = "(b)",
    };

    const TOML_Value* val = &keyval->value;

    printf("%s %s", type_table[val->type], keyval->key);

    switch (val->type)
    {
    case TOML_TYPE_TABLE:
        printf("\n");

        if (VEC_GET_LENGTH(&VAL2TABLE(val)->keyvalues) == 0)
        {
            for (size_t i = 0; i < indent + 2; ++i)
                printf(" ");

            printf("(empty)\n");
        }
        else
        {
            VEC_FOR_EACH (k, &VAL2TABLE(val)->keyvalues)
                print_value(k, indent + 2);
        }
        break;

    case TOML_TYPE_ARRAY:
        WH_ASSERT_NOT_REACHED();
        break;

    case TOML_TYPE_STRING:
        printf(" = \"%s\"\n", VAL2STRING(val));
        break;

    case TOML_TYPE_INT64:
        printf(" = %ld\n", VAL2INT64(val));
        break;

    case TOML_TYPE_FLOAT64:
        printf(" = %lf\n", VAL2FLOAT64(val));
        break;

    case TOML_TYPE_BOOL:
        printf(" = %s\n", VAL2BOOL(val) ? "true" : "false");
        break;

    default:
        WH_ASSERT_NOT_REACHED();
    }
}

TOML_Context* toml_create_ctx()
{
    TOML_Context* ctx = calloc(1, sizeof(TOML_Context));
    if (!ctx)
        return nullptr;

    ctx->root_table = table_create();
    if (!ctx->root_table)
    {
        free(ctx);
        return nullptr;
    }

    ctx->parser.row = 1;
    ctx->parser.col = 1;
    ctx->parser.active_table = ctx->root_table;

    return ctx;
}

void toml_release_ctx(TOML_Context* ctx)
{
    table_destroy(ctx->root_table);
    free(ctx);
}

int toml_parse_from_file(const char* filepath, TOML_Context* ctx)
{
    struct stat stat_info;
    if (stat(filepath, &stat_info) < 0)
    {
        switch (errno)
        {
        case EACCES:
            CTX_MAKE_ERROR(ctx, "Permission denied for file \"%s\"", filepath);
            return -1;

        case ENOTDIR:
        case ENOENT:
            CTX_MAKE_ERROR(ctx, "No such file \"%s\"", filepath);
            return -1;

        default:
            CTX_MAKE_ERROR(ctx, "Failed to access file \"%s\"", filepath);
            return -1;
        }
    }

    if ((stat_info.st_mode & S_IFMT) == S_IFDIR)
    {
        CTX_MAKE_ERROR(ctx, "\"%s\" is a directory", filepath);
        return -1;
    }

    ctx->parser.f = fopen(filepath, "r");
    if (!ctx->parser.f)
    {
        if (errno == EACCES)
            CTX_MAKE_ERROR(ctx, "Permission denied for file \"%s\"", filepath);
        else
            CTX_MAKE_ERROR(ctx, "Failed to open file \"%s\"", filepath);

        return -1;
    }

    while (CTX_FGETC(ctx) != EOF)
    {
        if (is_endoftoken(ctx->parser.c, ctx))
            continue;

        if (ctx->parser.c == SYM_TABLE_HEADER_START)
        {
            if (parse_table_header(ctx) < 0)
            {
                fclose(ctx->parser.f);
                ctx->parser.f = nullptr;
                return -1;
            }
        }
        else if (parse_keyvalue_pair(ctx->parser.active_table, false, ctx) < 0)
        {
            fclose(ctx->parser.f);
            ctx->parser.f = nullptr;
            return -1;
        }
    }

    fclose(ctx->parser.f);
    ctx->parser.f = nullptr;

    VEC_FOR_EACH (keyval, &ctx->root_table->keyvalues)
        print_value(keyval, 0);

    return 0;
}

const TOML_Value* toml_get_value(const char* key, const TOML_Context* ctx)
{
    char quoute = 0;
    const size_t key_len = strlen(key);

    if (key_len && key[0] == '.')
        return nullptr;

    TOML_Table* table = nullptr;
    size_t ii = 0;

    for (size_t i = 0; i < key_len; ++i)
    {
        const char c = key[i];

        if (c == '\'' || c == '"')
        {
            quoute = quoute == c ? 0 : c;
        }
        else if (c == '.' && !quoute)
        {
            TOML_Table* t = table ? table : ctx->root_table;
            VEC_FOR_EACH (keyval, &t->keyvalues)
            {
                if (strncmp(keyval->key, key + ii, i - ii))
                    continue;

                if (keyval->value.type != TOML_TYPE_TABLE)
                    break;

                table = VAL2TABLE(&keyval->value);
                ii = i + 1;
            }

            if (!table || table == t)
                return nullptr;
        }
    }

    TOML_Table* t = table ? table : ctx->root_table;
    VEC_FOR_EACH (keyval, &t->keyvalues)
    {
        const size_t len = key_len - ii;
        WH_ASSERT(len >= 0);

        if ((len == 0 && strlen(keyval->key) == 0) ||
            (len && strcmp(keyval->key, key + ii) == 0))
            return &keyval->value;
    }

    return nullptr;
}

const char* toml_get_error(const TOML_Context* ctx)
{
    return ctx->parser.error;
}
