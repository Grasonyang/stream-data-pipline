#include "log_output_format.h"

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

static size_t read_file(FILE *file, char *buf, size_t buf_size)
{
    rewind(file);
    size_t n = fread(buf, 1, buf_size - 1, file);
    buf[n] = '\0';
    return n;
}

static void test_emit_json(void)
{
    log_t log = {0};
    char *names[] = {"type", "duration"};
    char *values[] = {"clip", "300"};
    log.names = names;
    log.values = values;
    log.count = 2;

    FILE *captured_stdout = tmpfile();
    CHECK(captured_stdout != NULL);
    if (!captured_stdout) return;

    int saved_stdout = dup(STDOUT_FILENO);
    fflush(stdout);
    CHECK(dup2(fileno(captured_stdout), STDOUT_FILENO) >= 0);

    CHECK(log_output_emit_json(&log) == 0);
    
    fflush(stdout);
    CHECK(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    close(saved_stdout);

    char out_buf[128];
    read_file(captured_stdout, out_buf, sizeof(out_buf));
    
    CHECK(strstr(out_buf, "\"type\":\"clip\"") != NULL);
    CHECK(strstr(out_buf, "\"duration\":\"300\"") != NULL);

    fclose(captured_stdout);
}

static void test_emit_csv(void)
{
    log_t log = {0};
    char *names[] = {"type", "duration"};
    char *values[] = {"clip", "300"};
    log.names = names;
    log.values = values;
    log.count = 2;

    FILE *captured_stdout = tmpfile();
    CHECK(captured_stdout != NULL);
    if (!captured_stdout) return;

    int saved_stdout = dup(STDOUT_FILENO);
    fflush(stdout);
    CHECK(dup2(fileno(captured_stdout), STDOUT_FILENO) >= 0);

    CHECK(log_output_emit_csv(&log) == 0);
    
    fflush(stdout);
    CHECK(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    close(saved_stdout);

    char out_buf[128];
    read_file(captured_stdout, out_buf, sizeof(out_buf));
    
    CHECK(strcmp(out_buf, "clip,300\n") == 0);

    fclose(captured_stdout);
}

int main(void)
{
    test_emit_json();
    test_emit_csv();

    if (failures == 0) {
        printf("OK: log_output_format tests passed\n");
        return 0;
    }
    fprintf(stderr, "FAILED: %d log_output_format check(s)\n", failures);
    return 1;
}