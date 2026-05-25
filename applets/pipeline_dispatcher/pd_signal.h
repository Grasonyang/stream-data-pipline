#ifndef PD_SIGNAL_H
#define PD_SIGNAL_H

/**
 * @brief Installs signal handlers for SIGINT and SIGTERM.
 * 
 * Sets up a signal handler that safely marks the process as interrupted
 * without performing any unsafe operations in signal context.
 * 
 * @return 0 on success, -1 on failure to register signals.
 */
int pd_signal_install(void);

/**
 * @brief Checks if an interrupt signal has been received.
 * 
 * @return 1 if interrupted (SIGINT or SIGTERM), 0 otherwise.
 */
int pd_signal_interrupted(void);

#endif
