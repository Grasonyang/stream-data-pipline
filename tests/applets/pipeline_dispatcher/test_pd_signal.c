#include "pd_signal.h"

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void test_signal_handling(void)
{
    CHECK(pd_signal_install() == 0);
    CHECK(pd_signal_interrupted() == 0);
    
    /* Send a signal to ourselves */
    kill(getpid(), SIGINT);
    
    /* Ensure the signal handler caught it */
    CHECK(pd_signal_interrupted() == 1);
}

int main(void)
{
    test_signal_handling();

    if (failures == 0) {
        printf("OK: pd_signal tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d pd_signal check(s)\n", failures);
    return 1;
}