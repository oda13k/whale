
#include <stdlib.h>
#include <string.h>
#include <whale/log.h>
#include <whale/utils/env.h>

void wh_setenv(const char* name, const char* value, bool replace_if_exsiting)
{
    const char* initial_value = getenv(name);

    if (initial_value && replace_if_exsiting && strcmp(value, initial_value))
        wh_log(INFO, "env: %s=%s (replaced '%s')", name, value, initial_value);
    else
        wh_log(INFO, "env: %s=%s", name, value);

    setenv(name, value, replace_if_exsiting);
}
