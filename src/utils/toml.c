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
        if (is_eol((_ctx)->parser.c))                                          \
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
    while (!is_eol(CTX_FGETC(_ctx)) && ctx->parser.c != EOF)                   \
        ;

#define CTX_MATCHES_DELIMITER(_c, _delim, _ctx)                                \
    ({                                                                         \
        bool _ret = false;                                                     \
        if (strchr((_delim)->stop, (_c)) || (_c) == EOF)                       \
        {                                                                      \
            _ret = true;                                                       \
        }                                                                      \
        if ((_c) == SYM_COMMENT)                                               \
        {                                                                      \
            CTX_SKIP_LINE(_ctx);                                               \
            _ret = !!!strchr((_delim)->ignore, SYM_COMMENT);                   \
        }                                                                      \
        _ret;                                                                  \
    })

#define CTX_FIND_DELIMITER(_delim, _ctx)                                       \
    ({                                                                         \
        bool _ret = false;                                                     \
        while (true)                                                           \
        {                                                                      \
            CTX_FGETC(ctx);                                                    \
            if (CTX_MATCHES_DELIMITER(ctx->parser.c, _delim, _ctx))            \
            {                                                                  \
                _ret = true;                                                   \
                break;                                                         \
            }                                                                  \
            if (strchr((_delim)->ignore, (_ctx)->parser.c))                    \
                continue;                                                      \
            break;                                                             \
        }                                                                      \
        _ret;                                                                  \
    })

#define IS_UNQUOTED_CHAR_VALID(_c)                                             \
    ((_c >= '0' && _c <= '9') || (_c >= 'a' && _c <= 'z') ||                   \
     (_c >= 'A' && _c <= 'Z') || _c == '_' || _c == '-')

typedef struct
{
    const char* stop;
    const char* ignore;
} ValueDelimiter;

static int
parse_value(TOML_Value* val, const ValueDelimiter* delim, TOML_Context* ctx);
static int parse_keyvalue(
    TOML_Table* parent,
    const ValueDelimiter* delim,
    TOML_Context* ctx
);
static void table_destroy(TOML_Table* table);

static bool is_whitespace(char c)
{
    return c == ' ' || c == '\t';
}

static bool is_eol(char c)
{
    return c == '\n' || c == '\r';
}

static bool is_comment(char c, TOML_Context* ctx)
{
    if (c == SYM_COMMENT)
    {
        CTX_SKIP_LINE(ctx);
        return true;
    }

    return false;
}

static void value_destroy(TOML_Value* val)
{
    switch (val->type)
    {
    case TOML_TYPE_STRING:
        free(val->data.string);
        break;

    case TOML_TYPE_TABLE:
        table_destroy(val->data.table);
        break;

    case TOML_TYPE_ARRAY:
        VEC_FOR_EACH (v, &val->data.array)
            value_destroy(v);

        VEC_DESTROY(&val->data.array);
        break;

    case TOML_TYPE_INT64:
    case TOML_TYPE_FLOAT64:
    case TOML_TYPE_BOOL:
        break;

    default:
        WH_ASSERT_NOT_REACHED();
    }
}

static void keyvalue_destroy(TOML_KeyValue* keyval)
{
    free(keyval->key);
    value_destroy(&keyval->value);
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

static TOML_Table* table_get_or_create(
    const char* name,
    bool explicit,
    TOML_Table* parent,
    TOML_Context* ctx
)
{
    VEC_FOR_EACH (kv, &parent->keyvalues)
    {
        if (strcmp(name, kv->key) != 0)
            continue;

        if (kv->value.type != TOML_TYPE_TABLE)
        {
            CTX_MAKE_ERROR(ctx, "Cannot redefine key '%s'", name);
            return nullptr;
        }

        TOML_Table* table = kv->value.data.table;
        /* Error out on redefinition */
        if (explicit && table->explicitely_defined)
        {
            CTX_MAKE_ERROR(ctx, "Cannot redefine existing table '%s'", name);
            return nullptr;
        }

        if (!table->explicitely_defined && explicit)
            table->explicitely_defined = true;

        return table;
    }

    TOML_Table* table = table_create();
    if (!table)
    {
        CTX_MAKE_ERROR(ctx, "Internal error: Out of memory");
        return nullptr;
    }

    TOML_KeyValue kv = {
        .key = strdup(name),
        .value = {.type = TOML_TYPE_TABLE, .data.table = table}
    };

    if (!kv.key)
    {
        table_destroy(table);
        CTX_MAKE_ERROR(ctx, "Internal error: Out of memory");
        return nullptr;
    }

    if (VEC_PUSH(kv, &parent->keyvalues) < 0)
    {
        keyvalue_destroy(&kv);
        CTX_MAKE_ERROR(ctx, "Internal error: Out of memory");
        return nullptr;
    }

    table->explicitely_defined = explicit;
    return table;
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
    const ValueDelimiter* delim,
    TOML_Context* ctx
)
{
    const char quoute_char = ctx->parser.c;
    char value[MAX_VALUE_LENGTH] = {0};
    size_t value_idx = 0;

    while (true)
    {
        CTX_FGETC(ctx);

        if (is_eol(ctx->parser.c) || ctx->parser.c == EOF)
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

    if (!CTX_FIND_DELIMITER(delim, ctx))
    {
        CTX_MAKE_ERROR(ctx, "Unexpected character");
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
    const ValueDelimiter* delim,
    TOML_Context* ctx
)
{
    val->type = TOML_TYPE_BOOL;
    if (ctx->parser.c == 't')
    {
        if (CTX_FGETC(ctx) != 'r' || CTX_FGETC(ctx) != 'u' ||
            CTX_FGETC(ctx) != 'e')
            goto error;

        val->data.boolean = true;
    }
    else if (ctx->parser.c == 'f')
    {
        if (CTX_FGETC(ctx) != 'a' || CTX_FGETC(ctx) != 'l' ||
            CTX_FGETC(ctx) != 's' || CTX_FGETC(ctx) != 'e')
            goto error;

        val->data.boolean = false;
    }

    if (!CTX_FIND_DELIMITER(delim, ctx))
        goto error;

    return 0;

error:
    // FIXME: this message could be better
    CTX_MAKE_ERROR(ctx, "Unexpected value, expected true or false");
    return -1;
}

static int parse_value_numeric(
    TOML_Value* val,
    const ValueDelimiter* delim,
    TOML_Context* ctx
)
{
    char value[MAX_VALUE_LENGTH] = {0};
    size_t value_idx = 0;

    bool is_float = false;
    u8 base = 10;

    if (ctx->parser.c == '+' || ctx->parser.c == '-')
    {
        value[value_idx++] = ctx->parser.c;
        CTX_FGETC(ctx);
    }

    if (ctx->parser.c == '0')
    {
        switch (CTX_FGETC(ctx))
        {
        case 'x':
            CTX_FGETC(ctx);
            base = 16;
            break;

        case 'o':
            CTX_FGETC(ctx);
            base = 8;
            break;

        case 'b':
            CTX_FGETC(ctx);
            base = 2;
            break;

        case '.':
            CTX_FGETC(ctx);
            value[value_idx++] = '.';
            is_float = true;
            break;

        default:
            value[value_idx++] = '0';
            break;
        }
    }

    while (true)
    {
        if (is_eol(ctx->parser.c) || is_whitespace(ctx->parser.c) ||
            strchr(delim->stop, ctx->parser.c) ||
            is_comment(ctx->parser.c, ctx))

        {
            break;
        }

        if (value_idx >= MAX_VALUE_LENGTH)
        {
            CTX_MAKE_ERROR(
                ctx,
                "Value exceeds max length of %d characters",
                MAX_VALUE_LENGTH
            );
            return -1;
        }

        if (ctx->parser.c == '_')
            goto next;

        if (ctx->parser.c == '.')
        {
            if (!is_float && base == 10)
            {
                value[value_idx++] = '.';
                is_float = true;
                goto next;
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
    next:
        CTX_FGETC(ctx);
    }

    if (!strchr(delim->stop, ctx->parser.c) && !CTX_FIND_DELIMITER(delim, ctx))
    {
        CTX_MAKE_ERROR(ctx, "Unexpected character");
        return -1;
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

static int parse_value_inline_table(
    TOML_Value* val,
    const ValueDelimiter* delim,
    TOML_Context* ctx
)
{
    TOML_Table* table = table_create();
    if (!table)
    {
        CTX_MAKE_ERROR(ctx, "Internal error: Out of memory");
        return -1;
    }

    static const ValueDelimiter val_delim = {.stop = ",}", .ignore = " #\t\n"};

    while (true)
    {
        CTX_FGETC(ctx);

        if (ctx->parser.c == EOF)
        {
            CTX_MAKE_ERROR(ctx, "Unexpected end of file");
            goto error;
        }

        if (is_whitespace(ctx->parser.c) || is_eol(ctx->parser.c) ||
            is_comment(ctx->parser.c, ctx))
            continue;

        if (ctx->parser.c == '}')
            break;

        if (parse_keyvalue(table, &val_delim, ctx) < 0)
            goto error;

        if (ctx->parser.c == '}')
            break;
    }

    if (!CTX_FIND_DELIMITER(delim, ctx))
    {
        CTX_MAKE_ERROR(ctx, "Unexpected character");
        goto error;
    }

    val->type = TOML_TYPE_TABLE;
    val->data.table = table;
    return 0;

error:
    table_destroy(table);
    return -1;
}

static int parse_value_array(
    TOML_Value* value,
    const ValueDelimiter* delim,
    TOML_Context* ctx
)
{
    static const ValueDelimiter val_delim = {.stop = ",]", .ignore = " #\t\n"};

    if (VEC_INIT(&value->data.array) < 0)
    {
        CTX_MAKE_ERROR(ctx, "Internal error: Out of memory");
        return -1;
    }

    value->type = TOML_TYPE_ARRAY;

    while (true)
    {
        CTX_FGETC(ctx);

        if (ctx->parser.c == EOF)
        {
            CTX_MAKE_ERROR(ctx, "Unexpected end of file");
            goto error;
        }

        if (is_whitespace(ctx->parser.c) || is_eol(ctx->parser.c) ||
            is_comment(ctx->parser.c, ctx))
            continue;

        if (ctx->parser.c == ']')
            break;

        TOML_Value tmp_value;
        if (parse_value(&tmp_value, &val_delim, ctx) < 0)
            goto error;

        if (VEC_PUSH(tmp_value, &value->data.array) < 0)
        {
            value_destroy(&tmp_value);
            CTX_MAKE_ERROR(ctx, "Internal error: Out of memory");
            goto error;
        }

        if (ctx->parser.c == ']')
            break;
    }

    if (!CTX_FIND_DELIMITER(delim, ctx))
    {
        CTX_MAKE_ERROR(ctx, "Unexpected character");
        goto error;
    }

    return 0;

error:
    value_destroy(value);
    return -1;
}

static char* parse_key(
    int initial_char,
    int end_char,
    TOML_Table** parent,
    TOML_Context* ctx
)
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
        return nullptr;
    }

    while (true)
    {
        CTX_FGETC(ctx);

        if (is_eol(ctx->parser.c) || ctx->parser.c == EOF)
        {
            CTX_MAKE_ERROR(ctx, "Invalid character, expected '%c'", end_char);
            return nullptr;
        }

        if (keyname_idx == MAX_KEY_NAME)
        {
            CTX_MAKE_ERROR(
                ctx, "Key exceeds max length of %d characters", MAX_KEY_NAME
            );
            return nullptr;
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
                auto* t = table_get_or_create(keyname, false, *parent, ctx);
                if (!t)
                    return nullptr;

                *parent = t;
                return parse_key(CTX_FGETC(ctx), end_char, parent, ctx);
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
                return nullptr;
            }
            else if (!IS_UNQUOTED_CHAR_VALID(ctx->parser.c))
            {
                CTX_MAKE_ERROR(ctx, "Key contains invalid character(s)");
                return nullptr;
            }
        }

        keyname[keyname_idx++] = ctx->parser.c;
    }

    return strdup(keyname);
}

static int parse_table_header(TOML_Context* ctx)
{
    TOML_Table* parent = ctx->root_table;
    char* key = parse_key(CTX_FGETC(ctx), SYM_TABLE_HEADER_END, &parent, ctx);
    if (!key)
        return -1;

    TOML_Table* table = table_get_or_create(key, true, parent, ctx);

    free(key);

    if (!table)
        return -1;

    ctx->parser.active_table = table;
    return 0;
}

static int
parse_value(TOML_Value* val, const ValueDelimiter* delim, TOML_Context* ctx)
{
    while (is_whitespace(ctx->parser.c))
        CTX_FGETC(ctx);

    if (is_eol(ctx->parser.c) || ctx->parser.c == EOF)
    {
        CTX_MAKE_ERROR(ctx, "Unexpected end of line");
        return -1;
    }

    switch (ctx->parser.c)
    {
    case '"':
    case '\'':
        return parse_value_string(val, delim, ctx);

    case 't':
    case 'f':
        return parse_value_boolean(val, delim, ctx);

    case '{':
        return parse_value_inline_table(val, delim, ctx);

    case '[':
        return parse_value_array(val, delim, ctx);

    case '-':
    case '+':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return parse_value_numeric(val, delim, ctx);

    default:
        CTX_MAKE_ERROR(ctx, "Unexpected character");
        return -1;
    }
}

static int parse_keyvalue(
    TOML_Table* parent,
    const ValueDelimiter* delim,
    TOML_Context* ctx
)
{
    TOML_KeyValue kv = {
        .key = parse_key(ctx->parser.c, SYM_ASSIGN, &parent, ctx)
    };
    if (!kv.key)
        return -1;

    VEC_FOR_EACH (keyval, &parent->keyvalues)
    {
        if (strcmp(keyval->key, kv.key) == 0)
        {
            CTX_MAKE_ERROR(ctx, "Cannot redefine key '%s'", kv.key);
            free(kv.key);
            return -1;
        }
    }

    WH_ASSERT_SANITY(ctx->parser.c == SYM_ASSIGN);
    CTX_FGETC(ctx);

    if (parse_value(&kv.value, delim, ctx) < 0)
    {
        free(kv.key);
        return -1;
    }

    if (VEC_PUSH(kv, &parent->keyvalues) < 0)
    {
        keyvalue_destroy(&kv);
        CTX_MAKE_ERROR(ctx, "Internal error: Out of memory");
        return -1;
    }

    return 0;
}

[[maybe_unused]] static void
print_keyvalue(TOML_KeyValue* keyval, size_t indent)
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

    TOML_Value* val = &keyval->value;

    printf("%s %s", type_table[val->type], keyval->key ? keyval->key : "");

    switch (val->type)
    {
    case TOML_TYPE_TABLE:
        printf("\n");

        if (VEC_GET_LENGTH(&val->data.table->keyvalues) == 0)
        {
            for (size_t i = 0; i < indent + 2; ++i)
                printf(" ");

            printf("(empty)\n");
        }
        else
        {
            VEC_FOR_EACH (kv, &val->data.table->keyvalues)
                print_keyvalue(kv, indent + 2);
        }
        break;

    case TOML_TYPE_ARRAY:
        printf("%s[\n", keyval->key ? " = " : "");
        VEC_FOR_EACH (v, &val->data.array)
        {
            TOML_KeyValue kv = {.value = *v};
            print_keyvalue(&kv, indent + 2);
        }
        for (size_t i = 0; i < indent; ++i)
            printf(" ");
        printf("]\n");
        break;

    case TOML_TYPE_STRING:
        printf("%s\"%s\"\n", keyval->key ? " = " : "", val->data.string);
        break;

    case TOML_TYPE_INT64:
        printf("%s%ld\n", keyval->key ? " = " : "", val->data.int64);
        break;

    case TOML_TYPE_FLOAT64:
        printf("%s%lf\n", keyval->key ? " = " : "", val->data.float64);
        break;

    case TOML_TYPE_BOOL:
        printf(
            "%s%s\n",
            keyval->key ? " = " : "",
            val->data.boolean ? "true" : "false"
        );
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

    const ValueDelimiter val_delim = {.stop = "\n", .ignore = " \t"};

    while (CTX_FGETC(ctx) != EOF)
    {
        if (is_whitespace(ctx->parser.c) || is_eol(ctx->parser.c) ||
            is_comment(ctx->parser.c, ctx))
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
        else if (parse_keyvalue(ctx->parser.active_table, &val_delim, ctx) < 0)
        {
            fclose(ctx->parser.f);
            ctx->parser.f = nullptr;
            return -1;
        }
    }

    fclose(ctx->parser.f);
    ctx->parser.f = nullptr;

    // VEC_FOR_EACH (kv, &ctx->root_table->keyvalues)
    //     print_keyvalue(kv, 0);

    return 0;
}

const TOML_Value* toml_get_value(const char* key, const TOML_Context* ctx)
{
    char quoute = 0;
    const size_t key_len = strlen(key);

    if (key_len && key[0] == SYM_TABLE_SPLIT)
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
        else if (c == SYM_TABLE_SPLIT && !quoute)
        {
            TOML_Table* t = table ? table : ctx->root_table;
            VEC_FOR_EACH (kv, &t->keyvalues)
            {
                if (strncmp(kv->key, key + ii, i - ii))
                    continue;

                if (kv->value.type != TOML_TYPE_TABLE)
                    break;

                table = kv->value.data.table;
                ii = i + 1;
            }

            if (!table || table == t)
                return nullptr;
        }
    }

    TOML_Table* t = table ? table : ctx->root_table;
    VEC_FOR_EACH (kv, &t->keyvalues)
    {
        const size_t len = key_len - ii;
        WH_ASSERT(len >= 0);

        if ((!len && !strlen(kv->key)) || (len && !strcmp(kv->key, key + ii)))
            return &kv->value;
    }

    return nullptr;
}

const char* toml_get_error(const TOML_Context* ctx)
{
    return ctx->parser.error;
}
