#ifndef CLIP_STORE_H
#define CLIP_STORE_H

#include <stdio.h>

/**
 * @brief Represents a single key-value record in the database.
 * 
 * Keys and values are NUL-terminated strings. expire_at is a UNIX timestamp
 * indicating when the record becomes invalid.
 */
typedef struct {
    char *key;        /**< Unique identifier for the record. */
    char *value;      /**< Data associated with the key, or empty string for tombstone. */
    long expire_at;   /**< Expiration timestamp in seconds, or 0 for no expiration. */
    char *_alloc;     /**< Internal pointer to dynamically allocated value string, if any. */
} row_t;

/**
 * @brief Checks if a database row has expired.
 * 
 * @param row Pointer to the row to check.
 * @param now Current UNIX timestamp.
 * @return 1 if the row is expired or invalid, 0 otherwise.
 */
int clip_row_is_expired(const row_t *row, long now);

/**
 * @brief Checks if a database row is a tombstone (deleted marker).
 * 
 * A tombstone is represented by an empty value string.
 * 
 * @param row Pointer to the row to check.
 * @return 1 if the row is a tombstone, 0 otherwise.
 */
int clip_row_is_tombstone(const row_t *row);

#endif
