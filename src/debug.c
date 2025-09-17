
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <whale/debug.h>

static void on_sigsegv(int)
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

    wh_log(FATAL, "");
    wh_vlog(FATAL, fmt, vargs);

    va_end(vargs);

    abort();
}

void wh_debug_register_crash_handlers()
{
    signal(SIGSEGV, on_sigsegv);
}

void wh_debug_print_stack_trace(LogLevel log_level)
{
#define MAX_SYMBOLS 64
    void* backtrace_buf[MAX_SYMBOLS];
    size_t symbol_count = (size_t)backtrace(backtrace_buf, MAX_SYMBOLS);
    char** symbols = backtrace_symbols(backtrace_buf, symbol_count);
#undef MAX_SYMBOLS

    wh_log(log_level, "Call trace:");

    for (size_t i = 1; i < symbol_count; ++i)
        wh_log(log_level, "  %s", symbols[i]);
}
