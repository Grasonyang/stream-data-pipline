#include "sm_events.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void test_event_set_add_tag(void)
{
    sm_event_set_t set;
    sm_event_set_reset(&set);
    
    CHECK(sm_event_set_add_tag(&set, "motion") == 0);
    CHECK(set.count == 1);
    CHECK(strcmp(set.items[0], "motion") == 0);
    
    /* Duplicate tag should be ignored but return success */
    CHECK(sm_event_set_add_tag(&set, "motion") == 0);
    CHECK(set.count == 1);
    
    CHECK(sm_event_set_add_tag(&set, "person") == 0);
    CHECK(set.count == 2);
    CHECK(strcmp(set.items[1], "person") == 0);
}

static void test_event_set_add_all(void)
{
    sm_event_set_t dst, src;
    sm_event_set_reset(&dst);
    sm_event_set_reset(&src);
    
    sm_event_set_add_tag(&dst, "motion");
    sm_event_set_add_tag(&src, "motion");
    sm_event_set_add_tag(&src, "person");
    
    CHECK(sm_event_set_add_all(&dst, &src) == 0);
    CHECK(dst.count == 2);
    CHECK(strcmp(dst.items[0], "motion") == 0);
    CHECK(strcmp(dst.items[1], "person") == 0);
}

static void test_event_set_append_json(void)
{
    sm_event_set_t set;
    dynamic_buffer_t buf = {0};
    
    sm_event_set_reset(&set);
    sm_event_set_add_tag(&set, "motion");
    sm_event_set_add_tag(&set, "person");
    
    CHECK(sm_event_set_append_json(&set, &buf) == 0);
    CHECK(strcmp(buf.data, "[\"motion\",\"person\"]") == 0);
    
    dynamic_buffer_free(&buf);
}

int main(void)
{
    test_event_set_add_tag();
    test_event_set_add_all();
    test_event_set_append_json();

    if (failures == 0) {
        printf("OK: sm_events tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d sm_events check(s)\n", failures);
    return 1;
}