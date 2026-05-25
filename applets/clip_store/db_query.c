#include "db_query.h"

#include "db_format.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static void free_row_fields(row_t *row) {
    if (row == NULL) {
        return;
    }
    free(row->key);
    free(row->value);
    row->key = NULL;
    row->value = NULL;
    row->expire_at = 0;
}

void row_list_free(row_list_t *list) {
    if (list == NULL) {
        return;
    }
    /* Free contents of each row to prevent memory leaks */
    for (size_t i = 0; i < list->len; ++i) {
        free_row_fields(&list->rows[i]);
    }
    free(list->rows);
    list->rows = NULL;
    list->len = 0;
    list->cap = 0;
}

static int row_list_reserve(row_list_t *list, size_t next_len) {
    if (next_len <= list->cap) {
        return 0;
    }
    size_t next_cap = list->cap == 0 ? 16 : list->cap;
    /* Exponentially grow capacity to avoid frequent reallocations */
    while (next_cap < next_len) {
        next_cap *= 2;
    }
    row_t *grown = realloc(list->rows, next_cap * sizeof(list->rows[0]));
    if (grown == NULL) {
        return -1;
    }
    /* Initialize new capacity slots to zero */
    for (size_t i = list->cap; i < next_cap; ++i) {
        grown[i] = (row_t){0};
    }
    list->rows = grown;
    list->cap = next_cap;
    return 0;
}

static ssize_t find_row_index(const row_list_t *list, const char *key) {
    /* Linear search is sufficient for typical DB sizes in this context */
    for (size_t i = 0; i < list->len; ++i) {
        if (strcmp(list->rows[i].key, key) == 0) {
            return (ssize_t)i;
        }
    }
    return -1;
}

static int replace_row(row_t *dst, const row_t *src) {
    char *key = strdup(src->key);
    char *value = strdup(src->value);
    if (key == NULL || value == NULL) {
        free(key);
        free(value);
        return -1;
    }
    /* Clean up old values before overwriting */
    free(dst->key);
    free(dst->value);
    dst->key = key;
    dst->value = value;
    dst->expire_at = src->expire_at;
    return 0;
}

int load_latest_rows(FILE *fp, row_list_t *out) {
    if (fp == NULL || out == NULL) {
        return -1;
    }

    char *line = NULL;
    size_t cap = 0;
    rewind(fp);

    /* Read sequentially to ensure later rows overwrite earlier ones */
    while (getline(&line, &cap, fp) > 0) {
        line[strcspn(line, "\r\n")] = '\0';
        row_t parsed = {0};
        /* Skip malformed rows seamlessly */
        if (parse_db_row(line, &parsed) != 0) {
            continue;
        }

        ssize_t idx = find_row_index(out, parsed.key);
        if (idx < 0) {
            if (row_list_reserve(out, out->len + 1) != 0) {
                free(line);
                return -1;
            }
            idx = (ssize_t)out->len;
            out->len += 1;
        }

        /* Overwrite with the newest parsed data for this key */
        if (replace_row(&out->rows[(size_t)idx], &parsed) != 0) {
            free(line);
            return -1;
        }
    }

    free(line);
    return ferror(fp) ? -1 : 0;
}

int row_is_live(const row_t *row, long now) {
    if (row == NULL) {
        return 0;
    }
    /* Tombstones and expired rows are considered dead */
    if (clip_row_is_tombstone(row)) {
        return 0;
    }
    return !clip_row_is_expired(row, now);
}

int load_latest_row(FILE *fp, const char *key, row_t *out) {
    if (fp == NULL || key == NULL || out == NULL) {
        return -1;
    }

    row_list_t rows = {0};
    if (load_latest_rows(fp, &rows) != 0) {
        row_list_free(&rows);
        return -1;
    }

    ssize_t idx = find_row_index(&rows, key);
    if (idx < 0) {
        row_list_free(&rows);
        return 0;
    }

    /* Copy the found row into the output buffer */
    if (replace_row(out, &rows.rows[(size_t)idx]) != 0) {
        row_list_free(&rows);
        return -1;
    }

    row_list_free(&rows);
    return 1;
}
