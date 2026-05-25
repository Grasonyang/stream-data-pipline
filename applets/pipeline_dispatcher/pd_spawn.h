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

/**
 * @brief Spawns a new child process with redirected standard I/O.
 * 
 * Forks the current process and executes the specified binary. Standard input
 * and output can be redirected to provided file descriptors. A list of file
 * descriptors to close in the child process can also be provided.
 * 
 * @param name Diagnostic name of the child process for logging.
 * @param bin Absolute or relative path to the executable.
 * @param argv Null-terminated argument vector.
 * @param stdin_fd File descriptor to map to child's STDIN (or STDIN_FILENO).
 * @param stdout_fd File descriptor to map to child's STDOUT (or STDOUT_FILENO).
 * @param close_fds Array of file descriptors to close in the child process before exec.
 * @param close_count Number of file descriptors in close_fds array.
 * @return The process ID of the spawned child, or -1 on failure.
 */
pid_t pd_spawn_child(const char *name, const char *bin, char *const argv[], int stdin_fd, int stdout_fd, int *close_fds, size_t close_count);

/**
 * @brief Waits for a group of child processes to terminate.
 * 
 * Blocks until all specified child processes have exited. If an interrupt
 * is received while waiting, it will send SIGTERM to all remaining children
 * and return an interrupted status.
 * 
 * @param children Array of child process descriptors.
 * @param count Number of children in the array.
 * @return PD_EXIT_OK if all children exited normally with status 0,
 *         PD_EXIT_CHILD_ERROR if any child failed or was killed,
 *         PD_EXIT_INTERRUPTED if waiting was interrupted by a signal.
 */
pd_exit_code_t pd_wait_children(pd_child_t *children, size_t count);

/**
 * @brief Sends SIGTERM to a group of child processes and waits for them.
 * 
 * Useful for cleaning up spawned processes during shutdown or error recovery.
 * Skips children that have already been marked as terminated (pid <= 0).
 * 
 * @param children Array of child process descriptors.
 * @param count Number of children in the array.
 */
void pd_kill_children(pd_child_t *children, size_t count);

#endif
