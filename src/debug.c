
#define UNW_LOCAL_ONLY
#include <execinfo.h>
#include <libunwind.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <whale/debug.h>

[[noreturn]] static void on_sigsegv(int)
{
    /* wh_die calls wh_log which calls printf which is technically signal
     * un-safe, but we are aborting so? */
    wh_die(true, "Segmentation fault (core may've been dumped ¯\\_(ツ)_/¯)");
}

void wh_die(bool print_call_trace, const char* fmt, ...)
{
    if (print_call_trace)
        wh_debug_print_stack_trace(FATAL);

    va_list vargs;
    va_start(vargs, fmt);

    WH_NOWARN("-Wformat-zero-length", wh_log(FATAL, "");)
    wh_vlog(FATAL, fmt, vargs);

    va_end(vargs);

    wh_log_flush_buffers();

    abort();
}

void wh_debug_register_crash_handlers()
{
    signal(SIGSEGV, on_sigsegv);
}

void wh_debug_print_stack_trace(LogLevel log_level)
{
    wh_log(log_level, "Call trace:");

    unw_cursor_t cursor;
    unw_context_t context;

    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    while (unw_step(&cursor))
    {
        unw_word_t offset;
        char function_name[64] = {0};
        unw_get_proc_name(&cursor, function_name, 64, &offset);

        bool external_function = strlen(function_name) == 0;

        if (external_function)
        {
            unw_word_t off;
            char elf_name[64] = {0};
            unw_get_elf_filename(&cursor, elf_name, 64, &off);

            wh_log(
                log_level, " | \033[38;5;248m %s + 0x%lx\033[0m", elf_name, off
            );
        }
        else
        {
            const char* c = strcmp(function_name, "_start") == 0 ? "└" : "├";
            wh_log(log_level, " %s %s + 0x%lx", c, function_name, offset);
        }
    }
}
