#include "pd_signal.h"

#include <signal.h>
#include <string.h>

static volatile sig_atomic_t interrupted = 0;

/** Keep the signal handler async-signal-safe by only setting a flag. */
static void handle_signal(int signo) {
    (void)signo;
    interrupted = 1;
}

int pd_signal_install(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        return -1;
    }
    return 0;
}

int pd_signal_interrupted(void) {
    return interrupted ? 1 : 0;
}
