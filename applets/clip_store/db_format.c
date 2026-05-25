#include "db_format.h"

#include <stdlib.h>
#include <string.h>

int clip_row_is_expired(const row_t *row, long now) {
    /* NULL rows are considered expired/invalid by default */
    if (row == NULL) {
        return 1;
    }
    /* expire_at == 0 means the row never expires */
    return row->expire_at != 0 && row->expire_at <= now;
}

int clip_row_is_tombstone(const row_t *row) {
    /* A tombstone is explicitly marked by an empty value string */
    return row != NULL && row->value != NULL && row->value[0] == '\0';
}

int parse_db_row(char *line, row_t *row) {
    if (line == NULL || row == NULL) {
        return -1;
    }

    /* Locate the first and second tab delimiters */
    char *a = strchr(line, '\t');
    if (a == NULL) {
        return -1;
    }
    char *b = strchr(a + 1, '\t');
    if (b == NULL) {
        return -1;
    }

    /* Split the line into key, value, and expire_at strings */
    *a = '\0';
    *b = '\0';

    char *end = NULL;
    long expire_at = strtol(b + 1, &end, 10);
    /* Ensure the entire remainder of the line is a valid integer */
    if (end == b + 1 || *end != '\0') {
        return -1;
    }

    row->key = line;
    row->value = a + 1;
    row->expire_at = expire_at;
    return 0;
}

int append_db_row(FILE *fp, const char *key, const char *value, long expire_at) {
    if (fp == NULL || key == NULL || value == NULL) {
        return -1;
    }
    /* Always write to the end of the file in append-only DB mode */
    if (fseek(fp, 0, SEEK_END) != 0) {
        return -1;
    }
    if (fprintf(fp, "%s\t%s\t%ld\n", key, value, expire_at) < 0) {
        return -1;
    }
    /* Ensure the write is pushed to the OS immediately */
    return fflush(fp);
}
