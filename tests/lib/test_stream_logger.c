#include "libpipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } \
} while (0)

static size_t read_file(FILE *file, char *buf, size_t buf_size)
{
    rewind(file);
    size_t n = fread(buf, 1, buf_size - 1, file);
    buf[n] = '\0';
    return n;
}

static void test_logger_writes_stderr_only(void)
{
    FILE *captured_stdout = tmpfile();
    FILE *captured_stderr = tmpfile();
    CHECK(captured_stdout != NULL);
    CHECK(captured_stderr != NULL);
    if (captured_stdout == NULL || captured_stderr == NULL) {
        if (captured_stdout != NULL) fclose(captured_stdout);
        if (captured_stderr != NULL) fclose(captured_stderr);
        return;
    }

    int saved_stdout = dup(STDOUT_FILENO);
    int saved_stderr = dup(STDERR_FILENO);
    CHECK(saved_stdout >= 0);
    CHECK(saved_stderr >= 0);
    if (saved_stdout < 0 || saved_stderr < 0) {
        fclose(captured_stdout);
        fclose(captured_stderr);
        return;
    }

    fflush(stdout);
    fflush(stderr);
    CHECK(dup2(fileno(captured_stdout), STDOUT_FILENO) >= 0);
    CHECK(dup2(fileno(captured_stderr), STDERR_FILENO) >= 0);

    stream_logger_set_tag("unit_test");
    LOG_DEBUG("debug message");
    LOG_INFO("info %d", 42);
    LOG_WARN("warn message");
    stream_logger_set_tag(NULL);
    LOG_ERROR("error message");

    fflush(stdout);
    fflush(stderr);

    CHECK(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    CHECK(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stdout);
    close(saved_stderr);

    char out_buf[128];
    char err_buf[2048];
    size_t out_len = read_file(captured_stdout, out_buf, sizeof(out_buf));
    read_file(captured_stderr, err_buf, sizeof(err_buf));

    CHECK(out_len == 0);
    CHECK(strstr(err_buf, "[DEBUG] unit_test: debug message") != NULL);
    CHECK(strstr(err_buf, "[INFO] unit_test: info 42") != NULL);
    CHECK(strstr(err_buf, "[WARN] unit_test: warn message") != NULL);
    CHECK(strstr(err_buf, "[ERROR] unit_test: error message") != NULL);

    fclose(captured_stdout);
    fclose(captured_stderr);
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
    test_logger_writes_stderr_only();
    test_stream_logger_strict();
    if (failures == 0) {
        printf("OK: all stream_logger tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d check(s)\n", failures);
    return 1;
}