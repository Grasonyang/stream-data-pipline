#include "log_filter_expr.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void test_filter_parse(void)
{
    filter_t f;
    
    char expr1[] = "type=clip";
    CHECK(log_filter_parse(expr1, &f) == 0);
    CHECK(f.op == FILTER_EQUALS);
    CHECK(strcmp(f.key, "type") == 0);
    CHECK(strcmp(f.value, "clip") == 0);
    
    char expr2[] = "duration>100";
    CHECK(log_filter_parse(expr2, &f) == 0);
    CHECK(f.op == FILTER_GREATER_THAN);
    CHECK(strcmp(f.key, "duration") == 0);
    CHECK(strcmp(f.value, "100") == 0);
    
    char expr3[] = "msg~error";
    CHECK(log_filter_parse(expr3, &f) == 0);
    CHECK(f.op == FILTER_CONTAINS);
    CHECK(strcmp(f.key, "msg") == 0);
    CHECK(strcmp(f.value, "error") == 0);
    
    char expr4[] = "status!=OK";
    CHECK(log_filter_parse(expr4, &f) == 0);
    CHECK(f.op == FILTER_NOT_EQUALS);
    CHECK(strcmp(f.key, "status") == 0);
    CHECK(strcmp(f.value, "OK") == 0);
}

static void test_filter_match_jsonl(void)
{
    filter_t f1;
    char expr1[] = "type=clip";
    CHECK(log_filter_parse(expr1, &f1) == 0);
    
    int matched = 0;
    const char *line1 = "{\"type\":\"clip\",\"duration\":300}";
    CHECK(log_filter_match_jsonl(line1, &f1, &matched) == 0);
    CHECK(matched == 1);
    
    const char *line2 = "{\"type\":\"data\",\"duration\":100}";
    CHECK(log_filter_match_jsonl(line2, &f1, &matched) == 0);
    CHECK(matched == 0);
    
    filter_t f2;
    char expr2[] = "duration>150";
    CHECK(log_filter_parse(expr2, &f2) == 0);
    
    CHECK(log_filter_match_jsonl(line1, &f2, &matched) == 0);
    CHECK(matched == 1);
    
    CHECK(log_filter_match_jsonl(line2, &f2, &matched) == 0);
    CHECK(matched == 0);
}

int main(void)
{
    test_filter_parse();
    test_filter_match_jsonl();

    if (failures == 0) {
        printf("OK: log_filter_expr tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d log_filter_expr check(s)\n", failures);
    return 1;
}