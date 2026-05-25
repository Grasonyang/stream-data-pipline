#ifndef PD_EXIT_H
#define PD_EXIT_H

/**
 * @brief Stable shell-visible exit codes for pipeline_dispatcher.
 */
typedef enum {
    PD_EXIT_OK = 0,
    PD_EXIT_SETUP_ERROR = 1,
    PD_EXIT_BAD_ARGS = 2,
    PD_EXIT_CHILD_ERROR = 3,
    PD_EXIT_INTERRUPTED = 130
} pd_exit_code_t;

#endif
