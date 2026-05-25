#include "pd_spawn.h"

#include <stdio.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void test_spawn_and_wait(void)
{
    char *argv[] = {"true", NULL};
    pd_child_t children[1];
    
    children[0].name = "true";
    children[0].path = "/bin/true";
    children[0].pid = pd_spawn_child("true", "/bin/true", argv, STDIN_FILENO, STDOUT_FILENO, NULL, 0);
    
    CHECK(children[0].pid > 0);
    
    pd_exit_code_t rc = pd_wait_children(children, 1);
    CHECK(rc == PD_EXIT_OK);
}

static void test_spawn_failure(void)
{
    char *argv[] = {"false", NULL};
    pd_child_t children[1];
    
    children[0].name = "false";
    children[0].path = "/bin/false";
    children[0].pid = pd_spawn_child("false", "/bin/false", argv, STDIN_FILENO, STDOUT_FILENO, NULL, 0);
    
    CHECK(children[0].pid > 0);
    
    pd_exit_code_t rc = pd_wait_children(children, 1);
    CHECK(rc == PD_EXIT_CHILD_ERROR);
}

int main(void)
{
    test_spawn_and_wait();
    test_spawn_failure();

    if (failures == 0) {
        printf("OK: pd_spawn tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d pd_spawn check(s)\n", failures);
    return 1;
}