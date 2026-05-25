#ifndef PD_SPAWN_H
#define PD_SPAWN_H

#include "pd_exit.h"

#include <stddef.h>
#include <sys/types.h>

/**
 * @brief Child process descriptor used by dispatcher supervision.
 */
typedef struct {
    const char *name;
    const char *path;
    pid_t pid;
} pd_child_t;

pid_t pd_spawn_child(const char *name, const char *bin, char *const argv[], int stdin_fd, int stdout_fd, int *close_fds, size_t close_count);
pd_exit_code_t pd_wait_children(pd_child_t *children, size_t count);
void pd_kill_children(pd_child_t *children, size_t count);

#endif
