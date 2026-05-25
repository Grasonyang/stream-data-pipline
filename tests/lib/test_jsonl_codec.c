#include "libpipeline.h"

#include <errno.h>
#include <stdint.h>
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

static void test_jsonl_parse(void)
{
    const char *line = "{\"kind\":\"data\",\"sequence\":12,\"offset\":1024,\"length\":256,\"ts_ms\":1700,\"complete\":true}";
    char kind[16] = {0};
    uint64_t sequence = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
    int64_t ts_ms = 0;
    int complete = 0;

    CHECK(jsonl_get_string(line, "kind", kind, sizeof(kind)) == 0);
    CHECK(jsonl_get_uint64(line, "sequence", &sequence) == 0);
    CHECK(jsonl_get_uint64(line, "offset", &offset) == 0);
    CHECK(jsonl_get_uint64(line, "length", &length) == 0);
    CHECK(jsonl_get_int64(line, "ts_ms", &ts_ms) == 0);
    CHECK(jsonl_get_bool(line, "complete", &complete) == 0);
    CHECK(strcmp(kind, "data") == 0);
    CHECK(sequence == 12);
    CHECK(offset == 1024);
    CHECK(length == 256);
    CHECK(ts_ms == 1700);
    CHECK(complete == 1);

    char missing[8] = {0};
    CHECK(jsonl_get_string(line, "missing", missing, sizeof(missing)) == -1);

    const char *key_after_value = "{\"x\":\"kind\",\"kind\":\"data\"}";
    memset(kind, 0, sizeof(kind));
    CHECK(jsonl_get_string(key_after_value, "kind", kind, sizeof(kind)) == 0);
    CHECK(strcmp(kind, "data") == 0);

    const char *escaped = "{\"kind\":\"a\\nb\\tc\"}";
    memset(kind, 0, sizeof(kind));
    CHECK(jsonl_get_string(escaped, "kind", kind, sizeof(kind)) == 0);
    CHECK(strcmp(kind, "a\nb\tc") == 0);

    const char *negative_uint = "{\"sequence\":-1}";
    CHECK(jsonl_get_uint64(negative_uint, "sequence", &sequence) == -1);
}

static void test_json_string_write(void)
{
    dynamic_buffer_t buf = {0};
    CHECK(jsonl_write_string(&buf, "a\"b\\c\n") == 0);
    CHECK(strcmp(buf.data, "\"a\\\"b\\\\c\\n\"") == 0);
    dynamic_buffer_free(&buf);
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

int main(void)
{
    test_jsonl_parse();
    test_json_string_write();
    test_jsonl_codec_strict();

    if (failures == 0) {
        printf("OK: all jsonl_codec tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d jsonl_codec check(s)\n", failures);
    return 1;
}