#include "pd_pipeline.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void test_pipeline_init(void)
{
    pd_pipeline_t pipeline;
    
    /* Initialize with some garbage data to ensure it resets properly */
    pipeline.pipe1[0] = 5;
    pipeline.pipe1[1] = 6;
    pipeline.pipe2[0] = 7;
    pipeline.pipe2[1] = 8;
    pipeline.children[0].pid = 1234;
    
    pd_pipeline_init(&pipeline);
    
    CHECK(pipeline.pipe1[0] == -1);
    CHECK(pipeline.pipe1[1] == -1);
    CHECK(pipeline.pipe2[0] == -1);
    CHECK(pipeline.pipe2[1] == -1);
    
    for (int i = 0; i < PD_CHILD_COUNT; i++) {
        CHECK(pipeline.children[i].pid == -1);
    }
}

static void test_pipeline_cleanup(void)
{
    pd_pipeline_t pipeline;
    pd_pipeline_init(&pipeline);
    
    /* Cleanup should handle uninitialized/default pipes safely */
    pd_pipeline_cleanup(&pipeline);
    CHECK(pipeline.pipe1[0] == -1);
}

int main(void)
{
    test_pipeline_init();
    test_pipeline_cleanup();

    if (failures == 0) {
        printf("OK: pd_pipeline tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d pd_pipeline check(s)\n", failures);
    return 1;
}