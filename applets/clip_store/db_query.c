#include "db_query.h"

#include "db_format.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_BUCKETS 64
#define FNV1A_64_OFFSET_BASIS 14695981039346656037ULL
#define FNV1A_64_PRIME 1099511628211ULL

/**
 * Release strings owned by a row and reset it to an empty state.
 */
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

void row_index_free(row_index_t *index) {
    if (index == NULL) {
        return;
    }
    for (size_t i = 0; i < index->cap; ++i) {
        if (index->used[i]) {
            free_row_fields(&index->rows[i]);
        }
    }
    free(index->rows);
    free(index->used);
    index->rows = NULL;
    index->used = NULL;
    index->len = 0;
    index->cap = 0;
}

/**
 * Hash a key with the standard 64-bit FNV-1a algorithm.
 *
 * FNV-1a is small, deterministic, and good enough for short internal keys such
 * as "session_id:timestamp". It is not intended for cryptographic use.
 */
static uint64_t hash_key(const char *key) {
    uint64_t h = FNV1A_64_OFFSET_BASIS;
    for (const unsigned char *p = (const unsigned char *)key; *p != '\0'; ++p) {
        h ^= *p;
        h *= FNV1A_64_PRIME;
    }
    return h;
}

/**
 * Find the bucket for a key using open addressing with linear probing.
 *
 * If the key already exists, found is set to 1 and the existing bucket index is
 * returned. Otherwise found is set to 0 and the first empty insertion slot is
 * returned. The table capacity must be a power of two.
 */
static size_t find_slot(const row_index_t *index, const char *key, int *found) {
    size_t mask = index->cap - 1;
    size_t idx = (size_t)hash_key(key) & mask;
    while (index->used[idx]) {
        if (strcmp(index->rows[idx].key, key) == 0) {
            *found = 1;
            return idx;
        }
        idx = (idx + 1) & mask;
    }
    *found = 0;
    return idx;
}

/**
 * Allocate empty hash buckets and occupancy flags.
 */
static int allocate_buckets(row_index_t *index, size_t cap) {
    index->rows = calloc(cap, sizeof(index->rows[0]));
    index->used = calloc(cap, sizeof(index->used[0]));
    if (index->rows == NULL || index->used == NULL) {
        free(index->rows);
        free(index->used);
        index->rows = NULL;
        index->used = NULL;
        return -1;
    }
    index->cap = cap;
    return 0;
}

/**
 * Grow the hash index before the load factor reaches 75%.
 *
 * Existing rows are moved into the new bucket array without duplicating their
 * strings. Ownership transfers from index to grown, so the old arrays can be
 * freed after rehashing without freeing row contents.
 */
static int row_index_grow(row_index_t *index, size_t next_len) {
    size_t next_cap = index->cap == 0 ? INITIAL_BUCKETS : index->cap;
    while (next_len * 4 >= next_cap * 3) {
        next_cap *= 2;
    }
    if (next_cap == index->cap) {
        return 0;
    }

    row_index_t grown = {0};
    if (allocate_buckets(&grown, next_cap) != 0) {
        return -1;
    }

    for (size_t i = 0; i < index->cap; ++i) {
        if (!index->used[i]) {
            continue;
        }
        int found = 0;
        size_t slot = find_slot(&grown, index->rows[i].key, &found);
        grown.rows[slot] = index->rows[i];
        grown.used[slot] = 1;
        grown.len += 1;
    }

    free(index->rows);
    free(index->used);
    *index = grown;
    return 0;
}

/**
 * Deep-copy one row into another row slot.
 *
 * Used when inserting parsed rows into the index because parse_db_row() returns
 * pointers into the temporary input line buffer.
 */
static int replace_row(row_t *dst, const row_t *src) {
    char *key = strdup(src->key);
    char *value = strdup(src->value);
    if (key == NULL || value == NULL) {
        free(key);
        free(value);
        return -1;
    }
    free(dst->key);
    free(dst->value);
    dst->key = key;
    dst->value = value;
    dst->expire_at = src->expire_at;
    return 0;
}

/**
 * Insert or overwrite the latest row for a key.
 *
 * The database file is append-only, so later rows win. A later tombstone row is
 * stored in the same way as any other row and interpreted by row_is_live().
 * This function handles table-level concerns such as growth, probing, and
 * bucket occupancy. The actual deep-copy into the selected bucket is delegated
 * to replace_row().
 */
static int row_index_put(row_index_t *index, const row_t *row) {
    if (row_index_grow(index, index->len + 1) != 0) {
        return -1;
    }

    int found = 0;
    size_t slot = find_slot(index, row->key, &found);
    if (!found) {
        if (replace_row(&index->rows[slot], row) != 0) {
            return -1;
        }
        index->used[slot] = 1;
        index->len += 1;
        return 0;
    }

    return replace_row(&index->rows[slot], row);
}

const row_t *row_index_get(const row_index_t *index, const char *key) {
    if (index == NULL || key == NULL || index->cap == 0) {
        return NULL;
    }
    int found = 0;
    size_t slot = find_slot(index, key, &found);
    return found ? &index->rows[slot] : NULL;
}

int load_latest_rows(FILE *fp, row_index_t *out) {
    if (fp == NULL || out == NULL) {
        return -1;
    }

    char *line = NULL;
    size_t cap = 0;
    rewind(fp);

    while (getline(&line, &cap, fp) > 0) {
        line[strcspn(line, "\r\n")] = '\0';
        row_t parsed = {0};
        if (parse_db_row(line, &parsed) != 0) {
            continue;
        }
        if (row_index_put(out, &parsed) != 0) {
            free(line);
            return -1;
        }
    }

    free(line);
    return ferror(fp) ? -1 : 0;
}

int load_latest_row(FILE *fp, const char *key, row_t *out) {
    if (fp == NULL || key == NULL || out == NULL) {
        return -1;
    }

    row_index_t rows = {0};
    if (load_latest_rows(fp, &rows) != 0) {
        row_index_free(&rows);
        return -1;
    }

    const row_t *row = row_index_get(&rows, key);
    if (row == NULL) {
        row_index_free(&rows);
        return 0;
    }

    if (replace_row(out, row) != 0) {
        row_index_free(&rows);
        return -1;
    }

    row_index_free(&rows);
    return 1;
}

int row_is_live(const row_t *row, long now) {
    if (row == NULL) {
        return 0;
    }
    if (clip_row_is_tombstone(row)) {
        return 0;
    }
    return !clip_row_is_expired(row, now);
}
