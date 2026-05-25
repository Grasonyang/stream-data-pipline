#include "db_query.h"

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

static void test_row_is_live(void)
{
    row_t row = {"key", "val", 1000};
    CHECK(row_is_live(&row, 500) == 1); /* before expiration */
    CHECK(row_is_live(&row, 1500) == 0); /* after expiration */
    
    row_t no_expire = {"key", "val", 0};
    CHECK(row_is_live(&no_expire, 500) == 1);
    CHECK(row_is_live(&no_expire, 1500) == 1);
    
    row_t tombstone = {"key", "", 0};
    CHECK(row_is_live(&tombstone, 500) == 0);
}

static void test_load_latest_rows(void)
{
    FILE *fp = tmpfile();
    CHECK(fp != NULL);
    if (!fp) return;

    /* Write some rows: update key1, add key2, tombstone key2, add key3 */
    fprintf(fp, "key1\t{\"v\":1}\t0\n");
    fprintf(fp, "key2\t{\"v\":2}\t0\n");
    fprintf(fp, "key1\t{\"v\":10}\t0\n"); /* Update key1 */
    fprintf(fp, "key2\t\t0\n"); /* Tombstone key2 */
    fprintf(fp, "key3\t{\"v\":3}\t100\n");
    fflush(fp);
    rewind(fp);
    
    row_index_t index = {0};
    CHECK(load_latest_rows(fp, &index) == 0);
    
    const row_t *r1 = row_index_get(&index, "key1");
    CHECK(r1 != NULL);
    if (r1) {
        CHECK(strcmp(r1->value, "{\"v\":10}") == 0);
    }
    
    const row_t *r2 = row_index_get(&index, "key2");
    CHECK(r2 != NULL);
    if (r2) {
        CHECK(strcmp(r2->value, "") == 0); /* tombstone is still in index, handled by row_is_live later */
    }
    
    const row_t *r3 = row_index_get(&index, "key3");
    CHECK(r3 != NULL);
    if (r3) {
        CHECK(strcmp(r3->value, "{\"v\":3}") == 0);
        CHECK(r3->expire_at == 100);
    }
    
    row_index_free(&index);
    fclose(fp);
}

int main(void)
{
    test_row_is_live();
    test_load_latest_rows();

    if (failures == 0) {
        printf("OK: db_query tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d db_query check(s)\n", failures);
    return 1;
}