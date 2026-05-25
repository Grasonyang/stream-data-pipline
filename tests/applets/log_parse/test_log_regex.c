#include "log_regex.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void test_split_fields(void)
{
    log_t log = {0};
    char arg[] = "ts,level,msg";
    
    CHECK(log_regex_split_fields(arg, &log) == 0);
    CHECK(log.count == 3);
    CHECK(strcmp(log.names[0], "ts") == 0);
    CHECK(strcmp(log.names[1], "level") == 0);
    CHECK(strcmp(log.names[2], "msg") == 0);
    
    log_regex_free(&log);
}

static void test_parse_line(void)
{
    log_t log = {0};
    char arg[] = "type,duration";
    CHECK(log_regex_split_fields(arg, &log) == 0);

    regex_t re;
    CHECK(regcomp(&re, "^type=([a-z]+) duration=([0-9]+)", REG_EXTENDED) == 0);

    const char *line = "type=clip duration=300";
    CHECK(log_regex_parse_line(line, &re, &log) == 0);
    CHECK(log.count == 2);
    CHECK(strcmp(log.values[0], "clip") == 0);
    CHECK(strcmp(log.values[1], "300") == 0);

    log_regex_free_values(&log);

    /* Test mismatch */
    const char *bad_line = "type=clip other=123";
    CHECK(log_regex_parse_line(bad_line, &re, &log) == 1);

    regfree(&re);
    log_regex_free(&log);
}

int main(void)
{
    test_split_fields();
    test_parse_line();

    if (failures == 0) {
        printf("OK: log_regex tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d log_regex check(s)\n", failures);
    return 1;
}