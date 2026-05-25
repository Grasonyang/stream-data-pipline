#include "db_format.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void test_parse_db_row_valid(void)
{
    char line[] = "clip123\t{\"data\":\"test\"}\t1700000000";
    row_t row = {0};
    
    CHECK(parse_db_row(line, &row) == 0);
    if (row.key == NULL) return;
    CHECK(strcmp(row.key, "clip123") == 0);
    CHECK(strcmp(row.value, "{\"data\":\"test\"}") == 0);
    CHECK(row.expire_at == 1700000000L);
}

static void test_parse_db_row_invalid(void)
{
    char line1[] = "clip123\t{\"data\":\"test\"}"; /* Missing expire_at */
    row_t row = {0};
    CHECK(parse_db_row(line1, &row) == -1);

    char line2[] = "clip123"; /* Missing value and expire_at */
    CHECK(parse_db_row(line2, &row) == -1);
}

static void test_append_db_row(void)
{
    FILE *fp = tmpfile();
    CHECK(fp != NULL);
    if (!fp) return;

    CHECK(append_db_row(fp, "clip456", "{\"val\":1}", 1800000000L) == 0);
    
    rewind(fp);
    char buf[128] = {0};
    CHECK(fgets(buf, sizeof(buf), fp) != NULL);
    CHECK(strcmp(buf, "clip456\t{\"val\":1}\t1800000000\n") == 0);

    fclose(fp);
}

int main(void)
{
    test_parse_db_row_valid();
    test_parse_db_row_invalid();
    test_append_db_row();

    if (failures == 0) {
        printf("OK: db_format tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d db_format check(s)\n", failures);
    return 1;
}