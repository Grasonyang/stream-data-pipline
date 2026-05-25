/*
 * test_libpipeline.c — smoke tests for the v1 libpipeline API.
 */

#include "libpipeline.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } \
} while (0)

static void test_sentinel(void)
{
    CHECK(lp_is_completed_session(".pipeline_end") == 1);
    CHECK(lp_is_completed_session("/tmp/session/.pipeline_end") == 1);
    CHECK(lp_is_completed_session("/tmp/session/.pipeline_end/extra") == 0);
    CHECK(lp_is_completed_session("chunk_0000.bin") == 0);
    CHECK(lp_is_completed_session("") == 0);
    CHECK(lp_is_completed_session(NULL) == 0);
}

static void test_now_ms(void)
{
    int64_t a = pipeline_get_monotonic_time_ms();
    int64_t b = pipeline_get_monotonic_time_ms();
    CHECK(a > 0);
    CHECK(b >= a);
}

static int make_temp_dir(char *path, size_t path_size)
{
    int n = snprintf(path, path_size, "/tmp/libpipeline_test_XXXXXX");
    if (n < 0 || (size_t)n >= path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return mkdtemp(path) == NULL ? -1 : 0;
}

static int make_temp_file(const char *dir, char *path, size_t path_size)
{
    int n = snprintf(path, path_size, "%s/watch_file", dir);
    if (n < 0 || (size_t)n >= path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) {
        return -1;
    }
    if (write(fd, "initial", 7) != 7) {
        int saved = errno;
        close(fd);
        unlink(path);
        errno = saved;
        return -1;
    }
    if (close(fd) != 0) {
        int saved = errno;
        unlink(path);
        errno = saved;
        return -1;
    }
    return 0;
}

static void test_open_dir_watch(void)
{
    char dir[128];
    if (make_temp_dir(dir, sizeof(dir)) != 0) {
        CHECK(0 && "make_temp_dir");
        return;
    }

    int wd = -1;
    int fd = lp_watch_dir(dir, &wd);
    CHECK(fd >= 0);
    CHECK(wd >= 0);
    if (fd >= 0) {
        close(fd);
    }

    char missing[160];
    int n = snprintf(missing, sizeof(missing), "%s/missing", dir);
    CHECK(n >= 0 && (size_t)n < sizeof(missing));

    errno = 0;
    wd = -1;
    fd = lp_watch_dir(missing, &wd);
    CHECK(fd == -1);
    CHECK(errno == ENOENT);

    rmdir(dir);
}

static void test_open_file_watch(void)
{
    char dir[128];
    char file[160];
    if (make_temp_dir(dir, sizeof(dir)) != 0) {
        CHECK(0 && "make_temp_dir");
        return;
    }
    if (make_temp_file(dir, file, sizeof(file)) != 0) {
        CHECK(0 && "make_temp_file");
        rmdir(dir);
        return;
    }

    int wd = -1;
    int watch_fd = lp_watch_file(file, &wd);
    CHECK(watch_fd >= 0);
    CHECK(wd >= 0);
    if (watch_fd < 0) {
        unlink(file);
        rmdir(dir);
        return;
    }

    int out = open(file, O_WRONLY | O_APPEND);
    CHECK(out >= 0);
    if (out >= 0) {
        CHECK(write(out, "x", 1) == 1);
        CHECK(close(out) == 0);
    }

    struct pollfd pfd = { .fd = watch_fd, .events = POLLIN };
    CHECK(poll(&pfd, 1, 1000) == 1);

    char buf[4096];
    ssize_t got = read(watch_fd, buf, sizeof(buf));
    CHECK(got > 0);

    int saw_modify = 0;
    for (ssize_t off = 0; got > 0 && off + (ssize_t)sizeof(struct inotify_event) <= got;) {
        const struct inotify_event *event = (const struct inotify_event *)(buf + off);
        if ((event->mask & IN_MODIFY) != 0) {
            saw_modify = 1;
        }
        off += (ssize_t)sizeof(struct inotify_event) + (ssize_t)event->len;
    }
    CHECK(saw_modify == 1);

    close(watch_fd);

    char missing[180];
    int n = snprintf(missing, sizeof(missing), "%s/missing", dir);
    CHECK(n >= 0 && (size_t)n < sizeof(missing));

    errno = 0;
    wd = -1;
    int missing_fd = lp_watch_file(missing, &wd);
    CHECK(missing_fd == -1);
    CHECK(errno == ENOENT);

    unlink(file);
    rmdir(dir);
}

static void test_consume_inotify_events(void)
{
    char dir[128];
    if (make_temp_dir(dir, sizeof(dir)) != 0) {
        CHECK(0 && "make_temp_dir");
        return;
    }

    int wd = -1;
    int watch_fd = lp_watch_dir(dir, &wd);
    CHECK(watch_fd >= 0);
    CHECK(wd >= 0);
    if (watch_fd < 0) {
        rmdir(dir);
        return;
    }

    char sentinel[160];
    int n = snprintf(sentinel, sizeof(sentinel), "%s/%s", dir, PIPELINE_SENTINEL_NAME);
    CHECK(n >= 0 && (size_t)n < sizeof(sentinel));

    int fd = open(sentinel, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK(close(fd) == 0);
    }

    struct pollfd pfd = { .fd = watch_fd, .events = POLLIN };
    CHECK(poll(&pfd, 1, 1000) == 1);

    int saw_sentinel = 0;
    CHECK(lp_consume_inotify_events(watch_fd, &saw_sentinel) == 0);
    CHECK(saw_sentinel == 1);

    close(watch_fd);
    unlink(sentinel);
    rmdir(dir);
}

static void test_buffer(void)
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

int main(void)
{
    test_sentinel();
    test_now_ms();
    test_open_dir_watch();
    test_open_file_watch();
    test_consume_inotify_events();
    test_buffer();
    test_jsonl_parse();
    test_json_string_write();
    if (failures == 0) {
        printf("OK: all libpipeline tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d check(s)\n", failures);
    return 1;
}
