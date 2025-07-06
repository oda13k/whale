
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <whale/log.h>
#include <whale/utils.h>

void wh_assert(bool expr, const char* str_expr, const char* file, size_t line)
{
    if (expr)
        return;

    wh_log(
        FATAL, "Assertion failed: '%s', in file %s:%zu", str_expr, file, line
    );
    abort();
}

u64 wh_time_monotonic_now_ms()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int wh_spawn_process(const char* path)
{
    const pid_t pid = fork();
    if (pid < 0)
    {
        wh_log(ERR, "Failed to spawn process %s", path);
        return -1;
    }

    if (pid > 0)
        return 0; // parent, ok.

    /* Here we are the child. The parent should not see the child's stdout nor
     * write anything to it's stdin, so we replace the child's std* with
     * /dev/null. We don't close them because some programs expect std* to be
     * there. */
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0)
    {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    char* const argv[] = {(char* const)path, NULL};
    execv(path, argv);
    exit(1);
}
