#include "libpipeline.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

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

static void test_jsonl_codec_strict(void)
{
    char out[64];
    int64_t i64 = 0;
    uint64_t u64 = 0;
    int b = -1;

    CHECK(jsonl_get_string("{\"x\":\"kind\",\"kind\":\"data\"}", "kind", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "data") == 0);
    CHECK(jsonl_get_string("{\"kind\":\"a\\nb\\tc\\u0041\"}", "kind", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "a\nb\tcA") == 0);
    CHECK(jsonl_get_string("{\"kind\":\"too long\"}", "kind", out, 4) == -1);
    CHECK(jsonl_get_string("{\"kind\":12}", "kind", out, sizeof(out)) == -1);
    CHECK(jsonl_get_string("[\"not-object\"]", "kind", out, sizeof(out)) == -1);
    CHECK(jsonl_get_string("{\"Kind\":\"data\"}", "kind", out, sizeof(out)) == -1);
    CHECK(jsonl_get_string("{\"kind\":\"data\"}junk", "kind", out, sizeof(out)) == -1);

    CHECK(jsonl_get_int64("{\"n\":-42}", "n", &i64) == 0);
    CHECK(i64 == -42);
    CHECK(jsonl_get_int64("{\"n\":1.25}", "n", &i64) == -1);
    CHECK(jsonl_get_int64("{\"n\":\"42\"}", "n", &i64) == -1);
    CHECK(jsonl_get_int64("{\"n\":9007199254740992}", "n", &i64) == -1);

    CHECK(jsonl_get_uint64("{\"n\":9007199254740991}", "n", &u64) == 0);
    CHECK(u64 == 9007199254740991ULL);
    CHECK(jsonl_get_uint64("{\"n\":-1}", "n", &u64) == -1);
    CHECK(jsonl_get_uint64("{\"n\":1.5}", "n", &u64) == -1);
    CHECK(jsonl_get_uint64("{\"n\":9007199254740992}", "n", &u64) == -1);

    CHECK(jsonl_get_bool("{\"ok\":true}", "ok", &b) == 0);
    CHECK(b == 1);
    CHECK(jsonl_get_bool("{\"ok\":false}", "ok", &b) == 0);
    CHECK(b == 0);
    CHECK(jsonl_get_bool("{\"ok\":1}", "ok", &b) == -1);
    CHECK(jsonl_is_object("{\"type\":\"clip\"}") == 1);
    CHECK(jsonl_is_object("{\"type\":\"clip\"}junk") == 0);
    CHECK(jsonl_is_object("{\"type\":\"clip\"}   \t") == 1);

    dynamic_buffer_t buf = {0};
    CHECK(jsonl_write_string(&buf, "a\"b\\c\n\t\b\f") == 0);
    CHECK(strcmp(buf.data, "\"a\\\"b\\\\c\\n\\t\\b\\f\"") == 0);
    dynamic_buffer_free(&buf);
    CHECK(jsonl_write_string(NULL, "x") == -1);
}

static void test_libpipeline_helpers_strict(void)
{
    char path[16];
    char *dup = NULL;

    CHECK(lp_is_completed_session("/tmp/a/.pipeline_end") == 1);
    CHECK(lp_is_completed_session("/tmp/a/.pipeline_end/") == 0);
    CHECK(lp_is_completed_session(".pipeline_end.tmp") == 0);

    CHECK(lp_build_artifact_path(path, sizeof(path), "dir", "file") == 0);
    CHECK(strcmp(path, "dir/file") == 0);
    CHECK(lp_build_artifact_path(path, sizeof(path), "123456789", "abcdef") == -1);
    CHECK(lp_build_artifact_path(NULL, sizeof(path), "dir", "file") == -1);

    dup = lp_strndup("abcdef", 3);
    CHECK(dup != NULL);
    CHECK(strcmp(dup, "abc") == 0);
    free(dup);

    dup = lp_strndup("abc", 0);
    CHECK(dup != NULL);
    CHECK(strcmp(dup, "") == 0);
    free(dup);
    CHECK(lp_strndup(NULL, 1) == NULL);
}

static void test_stream_logger_strict(void)
{
    char tmpl[] = "/tmp/ws_logger_strict_XXXXXX";
    int fd = mkstemp(tmpl);
    CHECK(fd >= 0);
    if (fd < 0) {
        return;
    }

    int saved_stderr = dup(STDERR_FILENO);
    CHECK(saved_stderr >= 0);
    CHECK(dup2(fd, STDERR_FILENO) >= 0);

    stream_logger_set_tag("strict_tag_name_that_is_longer_than_buffer");
    stream_logger_log(LOG_LVL_WARN, "value=%d", 42);
    fflush(stderr);

    CHECK(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stderr);

    char buf[256] = {0};
    CHECK(lseek(fd, 0, SEEK_SET) == 0);
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    CHECK(n > 0);
    if (n > 0) {
        buf[n] = '\0';
        CHECK(strstr(buf, "Z [WARN] strict_tag_name_") != NULL);
        CHECK(strstr(buf, ": value=42\n") != NULL);
    }

    close(fd);
    unlink(tmpl);
}

int main(void)
{
    test_dynamic_buffer_strict();
    test_jsonl_codec_strict();
    test_libpipeline_helpers_strict();
    test_stream_logger_strict();

    if (failures == 0) {
        printf("OK: strict lib tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d strict lib check(s)\n", failures);
    return 1;
}
