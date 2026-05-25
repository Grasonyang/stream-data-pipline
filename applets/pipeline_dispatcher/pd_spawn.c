#include "pd_spawn.h"

#include "libpipeline.h"
#include "pd_signal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

pid_t pd_spawn_child(const char *name, const char *bin, char *const argv[], int stdin_fd, int stdout_fd, int *close_fds, size_t close_count) {
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork(%s) failed: %s", name, strerror(errno));
        return -1;
    }
    if (pid == 0) {
        if (stdin_fd != STDIN_FILENO && dup2(stdin_fd, STDIN_FILENO) < 0) {
            _exit(126);
        }
        if (stdout_fd != STDOUT_FILENO && dup2(stdout_fd, STDOUT_FILENO) < 0) {
            _exit(126);
        }
        for (size_t i = 0; i < close_count; ++i) {
            if (close_fds[i] >= 0) {
                close(close_fds[i]);
            }
        }
        execv(bin, argv);
        fprintf(stderr, "exec %s failed: %s\n", bin, strerror(errno));
        _exit(127);
    }
    LOG_INFO("spawned %s pid=%d", name, (int)pid);
    return pid;
}

pd_exit_code_t pd_wait_children(pd_child_t *children, size_t count) {
    pd_exit_code_t rc = PD_EXIT_OK;
    for (size_t i = 0; i < count; ++i) {
        if (children[i].pid <= 0) {
            continue;
        }

        int status = 0;
        if (waitpid(children[i].pid, &status, 0) < 0) {
            if (errno == EINTR && pd_signal_interrupted()) {
                LOG_WARN("interrupted; terminating child processes");
                pd_kill_children(children, count);
                return PD_EXIT_INTERRUPTED;
            }
            LOG_ERROR("waitpid(%s pid=%d) failed: %s", children[i].name, (int)children[i].pid, strerror(errno));
            rc = PD_EXIT_CHILD_ERROR;
            continue;
        }
        if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            LOG_INFO("%s pid=%d exited with %d", children[i].name, (int)children[i].pid, code);
            if (code != 0) {
                rc = PD_EXIT_CHILD_ERROR;
            }
        } else if (WIFSIGNALED(status)) {
            LOG_WARN("%s pid=%d killed by signal %d", children[i].name, (int)children[i].pid, WTERMSIG(status));
            rc = PD_EXIT_CHILD_ERROR;
        }
        children[i].pid = -1;
    }
    return rc;
}

void pd_kill_children(pd_child_t *children, size_t count) {
    if (children == NULL) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        if (children[i].pid > 0) {
            kill(children[i].pid, SIGTERM);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (children[i].pid > 0) {
            int status = 0;
            while (waitpid(children[i].pid, &status, 0) < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            children[i].pid = -1;
        }
    }
}
