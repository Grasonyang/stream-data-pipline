#ifndef CLIP_STORE_DB_QUERY_H
#define CLIP_STORE_DB_QUERY_H

#include "clip_store.h"

#include <stddef.h>

/**
 * @brief Dynamic array for storing database rows.
 * 
 * Used for maintaining a deduplicated list of rows read from the database.
 */
typedef struct {
    row_t *rows;      /**< Array of rows */
    size_t len;       /**< Number of active rows in the array */
    size_t cap;       /**< Allocated capacity of the array */
} row_list_t;

/**
 * @brief Frees all resources held by a row list.
 * 
 * Releases dynamically allocated memory for keys, values, and the array itself.
 * 
 * @param list Pointer to the row list to free.
 */
void row_list_free(row_list_t *list);

/**
 * @brief Retrieves the latest record for a specific key.
 * 
 * Scans the database file to find the most recent entry matching the given key.
 * 
 * @param fp Open file pointer to the database.
 * @param key The key to search for.
 * @param out Output row structure to populate. Caller must free key and value.
 * @return 1 if found, 0 if not found, -1 on error.
 */
int load_latest_row(FILE *fp, const char *key, row_t *out);

/**
 * @brief Loads the latest record for all unique keys in the database.
 * 
 * Reads the entire database, keeping only the most recent entry for each key.
 * 
 * @param fp Open file pointer to the database.
 * @param out Output row list to populate.
 * @return 0 on success, -1 on allocation or read errors.
 */
int load_latest_rows(FILE *fp, row_list_t *out);

/**
 * @brief Determines if a row is active (not expired and not a tombstone).
 * 
 * @param row Pointer to the row to evaluate.
 * @param now Current UNIX timestamp.
 * @return 1 if the row is live, 0 if it is deleted or expired.
 */
int row_is_live(const row_t *row, long now);

#endif
