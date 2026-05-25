#ifndef CLIP_STORE_DB_QUERY_H
#define CLIP_STORE_DB_QUERY_H

#include "clip_store.h"

#include <stddef.h>

/**
 * @brief In-memory hash index for latest database rows.
 *
 * The file remains append-only. This index is rebuilt from the file when query
 * or compaction operations need the latest row for each key. Buckets use open
 * addressing and grow when the load factor reaches 75%.
 */
typedef struct {
    row_t *rows;          /**< Bucket storage for rows. */
    unsigned char *used;  /**< Bucket occupancy flags: default 0 means empty, 1 means rows[i] is valid. */
    size_t len;           /**< Number of occupied buckets. */
    size_t cap;           /**< Number of allocated buckets. */
} row_index_t;

/**
 * @brief Frees all resources held by a row index.
 * 
 * Releases dynamically allocated memory for keys, values, bucket flags, and the
 * bucket array itself.
 * 
 * @param index Pointer to the row index to free.
 */
void row_index_free(row_index_t *index);

/**
 * @brief Look up a row by key in a loaded row index.
 *
 * @param index Loaded row index.
 * @param key Key to search for.
 * @return Pointer to the row if found, otherwise NULL.
 */
const row_t *row_index_get(const row_index_t *index, const char *key);

/**
 * @brief Loads the latest record for all unique keys into a hash index.
 * 
 * Reads the entire database sequentially. Later rows overwrite earlier rows for
 * the same key, so tombstones and updates become the latest visible state.
 * 
 * @param fp Open file pointer to the database.
 * @param out Output row index to populate.
 * @return 0 on success, -1 on allocation or read errors.
 */
int load_latest_rows(FILE *fp, row_index_t *out);

/**
 * @brief Load and copy the latest record for a specific key.
 *
 * This is the single-key convenience wrapper around load_latest_rows(). It
 * builds the same latest-state hash index, looks up key, and deep-copies the
 * matching row into out.
 *
 * @param fp Open file pointer to the database.
 * @param key The key to search for.
 * @param out Output row structure to populate. Caller must free key and value.
 * @return 1 if found, 0 if not found, -1 on error.
 */
int load_latest_row(FILE *fp, const char *key, row_t *out);

/**
 * @brief Determines if a row is active (not expired and not a tombstone).
 * 
 * @param row Pointer to the row to evaluate.
 * @param now Current UNIX timestamp.
 * @return 1 if the row is live, 0 if it is deleted or expired.
 */
int row_is_live(const row_t *row, long now);

#endif
