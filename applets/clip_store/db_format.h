#ifndef CLIP_STORE_DB_FORMAT_H
#define CLIP_STORE_DB_FORMAT_H

#include "clip_store.h"

/**
 * @brief Parses a raw line of text into a database row structure.
 * 
 * The expected format is: key<TAB>value<TAB>expire_at
 * Modifies the input line in place by replacing tabs with NUL characters.
 * 
 * @param line The raw NUL-terminated string read from the database file.
 * @param row Output structure to hold the parsed fields.
 * @return 0 on success, -1 if the format is invalid.
 * @note The key and value pointers in row will point directly into the modified line buffer.
 */
int parse_db_row(char *line, row_t *row);

/**
 * @brief Appends a new key-value row to the database file.
 * 
 * Ensures the record is written to the end of the file and flushed.
 * 
 * @param fp Open file pointer to the database.
 * @param key The record's unique identifier.
 * @param value The record's data.
 * @param expire_at The UNIX timestamp when the record expires (0 for never).
 * @return 0 on success, -1 on write or seek failure.
 */
int append_db_row(FILE *fp, const char *key, const char *value, long expire_at);

#endif
