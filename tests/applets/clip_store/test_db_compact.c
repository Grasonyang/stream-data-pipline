#include "db_compact.h"
#include "db_format.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void test_rewrite_compact(void)
{
    char tmpl[] = "/tmp/test_db_compact_XXXXXX";
    int fd = mkstemp(tmpl);
    CHECK(fd >= 0);
    if (fd < 0) return;
    
    FILE *fp = fdopen(fd, "r+");
    CHECK(fp != NULL);
    if (!fp) {
        close(fd);
        unlink(tmpl);
        return;
    }

    /* Add expired, tombstoned, and active records */
    append_db_row(fp, "k1", "v1", 100); /* Expired (now=200) */
    append_db_row(fp, "k2", "v2", 300); /* Active */
    append_db_row(fp, "k3", "v3", 0);   /* Active, never expires */
    append_db_row(fp, "k4", "v4", 0);   /* Will be tombstoned */
    append_db_row(fp, "k4", "", 0);     /* Tombstone for k4 */
    append_db_row(fp, "k5", "old", 0);  /* Will be updated */
    append_db_row(fp, "k5", "new", 0);  /* Updated active */
    fflush(fp);
    
    CHECK(db_rewrite_compact(fp, tmpl, 200) == 0);
    fclose(fp);
    
    fp = fopen(tmpl, "r");
    CHECK(fp != NULL);
    if (!fp) {
        unlink(tmpl);
        return;
    }
    
    char line[256];
    int count = 0;
    int k2_found = 0, k3_found = 0, k5_found = 0;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        row_t r;
        CHECK(parse_db_row(line, &r) == 0);
        if (strcmp(r.key, "k2") == 0 && strcmp(r.value, "v2") == 0) k2_found = 1;
        if (strcmp(r.key, "k3") == 0 && strcmp(r.value, "v3") == 0) k3_found = 1;
        if (strcmp(r.key, "k5") == 0 && strcmp(r.value, "new") == 0) k5_found = 1;
        
        /* k1 and k4 should be gone */
        CHECK(strcmp(r.key, "k1") != 0);
        CHECK(strcmp(r.key, "k4") != 0);
        count++;
    }
    
    CHECK(count == 3);
    CHECK(k2_found == 1);
    CHECK(k3_found == 1);
    CHECK(k5_found == 1);
    
    fclose(fp);
    unlink(tmpl);
}

int main(void)
{
    test_rewrite_compact();

    if (failures == 0) {
        printf("OK: db_compact tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d db_compact check(s)\n", failures);
    return 1;
}