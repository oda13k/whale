
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <whale/log.h>
#include <whale/utils/proc.h>

static void on_sigchld(int)
{
    while (waitpid(-1, nullptr, WNOHANG) > 0)
        ;

    signal(SIGCHLD, on_sigchld);
}

int wh_proc_spawn(char* const args[])
{
    signal(SIGCHLD, on_sigchld);

    const pid_t pid = fork();
    if (pid < 0)
    {
        wh_log(ERR, "Failed to spawn process %s", args[0]);
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

    setsid();
    execv(args[0], args);
    exit(1);
}

int wh_proc_spawn_literal(const char* cmd)
{
    size_t len = strlen(cmd);
    if (len > 4096)
    {
        wh_log(
            ERR, "utils: Failed to spawn cmd, too long (%zu characters).", len
        );
        return -1;
    }

#define MAX_ARGS 64

    size_t space_count = 0;
    for (size_t i = 0; i < len; ++i)
        space_count += cmd[i] == ' ';

    if (space_count > MAX_ARGS - 2)
    {
        wh_log(
            ERR, "utils: Failed to spawn cmd: '%s', too many arguments.", cmd
        );
        return -1;
    }

    char dup_cmd[len];
    strcpy(dup_cmd, cmd);

    char* args[MAX_ARGS] = {0};

#undef MAX_ARGS

    size_t i = 0;
    char* tok = strtok(dup_cmd, " ");
    while (tok)
    {
        args[i++] = tok;
        tok = strtok(NULL, " ");
    }

    if (i == 0)
    {
        wh_log(ERR, "utils: Failed to spawn cmd, empty.");
        return -1;
    }

    return wh_proc_spawn(args);
}
