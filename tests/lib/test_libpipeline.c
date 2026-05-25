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

int main(void)
{
    test_sentinel();
    test_now_ms();
    test_open_dir_watch();
    test_open_file_watch();
    test_consume_inotify_events();
    test_libpipeline_helpers_strict();

    if (failures == 0) {
        printf("OK: all libpipeline tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d libpipeline check(s)\n", failures);
    return 1;
}