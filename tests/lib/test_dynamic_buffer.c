#include "libpipeline.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void test_buffer_basic(void)
{
    dynamic_buffer_t buf = {0};

    CHECK(dynamic_buffer_append_char(&buf, 'a') == 0);
    CHECK(dynamic_buffer_append_str(&buf, "bc") == 0);
    CHECK(dynamic_buffer_append_mem(&buf, "def", 3) == 0);
    CHECK(buf.len == 6);
    CHECK(buf.cap >= buf.len + 1);
    CHECK(strcmp(buf.data, "abcdef") == 0);

    CHECK(dynamic_buffer_reserve(&buf, 1024) == 0);
    CHECK(buf.cap >= buf.len + 1024 + 1);
    CHECK(strcmp(buf.data, "abcdef") == 0);

    dynamic_buffer_free(&buf);
    CHECK(buf.data == NULL);
    CHECK(buf.len == 0);
    CHECK(buf.cap == 0);
}

static void test_buffer_errors(void)
{
    dynamic_buffer_t buf = {0};

    CHECK(dynamic_buffer_append_str(NULL, "x") == -1);
    CHECK(dynamic_buffer_append_str(&buf, NULL) == -1);
    CHECK(dynamic_buffer_append_mem(&buf, NULL, 1) == -1);

    dynamic_buffer_t huge = {0};
    huge.len = (size_t)-8;
    huge.cap = huge.len;
    CHECK(dynamic_buffer_reserve(&huge, 16) == -1);
    CHECK(dynamic_buffer_has_failed(&huge) == 1);
    CHECK(dynamic_buffer_append_char(&huge, 'x') == -1);
    dynamic_buffer_reset(&huge);
    CHECK(dynamic_buffer_has_failed(&huge) == 0);
    CHECK(dynamic_buffer_append_str(&huge, "ok") == 0);
    CHECK(strcmp(huge.data, "ok") == 0);
    dynamic_buffer_free(&huge);

    dynamic_buffer_t nul = {0};
    CHECK(dynamic_buffer_append_mem(&nul, NULL, 0) == 0);
    CHECK(nul.len == 0);
    CHECK(nul.data != NULL);
    CHECK(nul.data[0] == '\0');
    dynamic_buffer_free(&nul);
}

static void test_dynamic_buffer_strict(void)
{
    dynamic_buffer_t buf = {0};

    CHECK(dynamic_buffer_reserve(&buf, 0) == 0);
    CHECK(buf.data != NULL);
    CHECK(buf.len == 0);
    CHECK(buf.data[0] == '\0');

    CHECK(dynamic_buffer_append_mem(&buf, "ab\0cd", 5) == 0);
    CHECK(buf.len == 5);
    CHECK(memcmp(buf.data, "ab\0cd", 5) == 0);
    CHECK(buf.data[5] == '\0');

    dynamic_buffer_reset(&buf);
    CHECK(buf.len == 0);
    CHECK(buf.data != NULL);
    CHECK(buf.data[0] == '\0');
    CHECK(dynamic_buffer_has_failed(&buf) == 0);

    errno = 0;
    CHECK(dynamic_buffer_append_mem(&buf, NULL, 1) == -1);
    CHECK(errno == EINVAL);
    CHECK(dynamic_buffer_has_failed(&buf) == 1);
    CHECK(dynamic_buffer_append_str(&buf, "after-fail") == -1);
    CHECK(errno == EIO);

    dynamic_buffer_reset(&buf);
    CHECK(dynamic_buffer_has_failed(&buf) == 0);
    CHECK(dynamic_buffer_append_str(&buf, "ok") == 0);
    CHECK(strcmp(buf.data, "ok") == 0);
    dynamic_buffer_free(&buf);

    dynamic_buffer_t corrupt = {0};
    corrupt.cap = 8;
    errno = 0;
    CHECK(dynamic_buffer_reserve(&corrupt, 1) == -1);
    CHECK(errno == EINVAL);
    CHECK(dynamic_buffer_has_failed(&corrupt) == 1);

    dynamic_buffer_t bad_len = {0};
    CHECK(dynamic_buffer_reserve(&bad_len, 1) == 0);
    bad_len.len = bad_len.cap;
    errno = 0;
    CHECK(dynamic_buffer_reserve(&bad_len, 1) == -1);
    CHECK(errno == EINVAL);
    CHECK(dynamic_buffer_has_failed(&bad_len) == 1);
    dynamic_buffer_free(&bad_len);
}

int main(void)
{
    test_buffer_basic();
    test_buffer_errors();
    test_dynamic_buffer_strict();

    if (failures == 0) {
        printf("OK: all dynamic_buffer tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d dynamic_buffer check(s)\n", failures);
    return 1;
}