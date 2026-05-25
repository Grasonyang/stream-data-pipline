#include "pd_config.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void test_config_parse_valid(void)
{
    pd_config_t cfg;
    char *argv[] = {"dispatcher", "--ttl", "120", "session123", "/src", "/db", NULL};
    int argc = 6;
    
    CHECK(pd_config_parse(argc, argv, &cfg) == 0);
    CHECK(strcmp(cfg.session_id, "session123") == 0);
    CHECK(strcmp(cfg.src_dir, "/src") == 0);
    CHECK(strcmp(cfg.db_path, "/db") == 0);
    CHECK(strcmp(cfg.ttl, "120") == 0);
    CHECK(strcmp(cfg.clip_secs, "5") == 0); /* Default */
}

static void test_config_parse_invalid(void)
{
    pd_config_t cfg;
    char *argv[] = {"dispatcher", "session123", NULL};
    int argc = 2;
    
    CHECK(pd_config_parse(argc, argv, &cfg) == -1);
}

int main(void)
{
    test_config_parse_valid();
    test_config_parse_invalid();

    if (failures == 0) {
        printf("OK: pd_config tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d pd_config check(s)\n", failures);
    return 1;
}