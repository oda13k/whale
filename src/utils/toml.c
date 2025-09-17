
#include <stdlib.h>
#include <whale/utils/toml.h>

TOML_Context* toml_create_ctx()
{
    TOML_Context* ctx = calloc(1, sizeof(TOML_Context));
    if (!ctx)
        return nullptr;

    return ctx;
}

void toml_destroy_ctx(TOML_Context* ctx)
{
    free(ctx);
}
