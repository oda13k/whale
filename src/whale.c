
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <whale/compositor.h>

static void print_help(const char* bin_name)
{
    printf("Usage: %s [ARGS...] [CMD]\n", bin_name);
    printf("\n");
    printf("If present, CMD will be executed immediately after startup.\n");
    printf("\n");
    printf("ARGS:\n");
    printf("  -h, --help             Print this message and exit.\n");
}

static bool arg_matches(const char* s, const char* l, const char* arg)
{
    return !strcmp(s, arg) || !strcmp(l, arg);
}

int main(int argc, char** argv)
{
    WhaleCompositorOptions opts = {0};

    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];

        if (arg_matches("-h", "--help", arg))
        {
            print_help(argv[0]);
            exit(0);
        }
        else if (i == argc - 1)
        {
            opts.startup_cmd = arg;
        }
        else
        {
            print_help(argv[0]);
            printf("\nUnrecognized argument: '%s'.\n", arg);
            exit(1);
        }
    }

    return wh_compositor_start(&opts);
}
