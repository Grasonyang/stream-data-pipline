#ifndef CLIP_STORE_DB_COMPACT_H
#define CLIP_STORE_DB_COMPACT_H

#include <stdio.h>

/**
 * @brief Performs garbage collection on the database to reclaim space.
 * 
 * Rebuilds an in-memory hash index from the append-only database, writes only
 * latest live records to a temporary file, and then atomically replaces the
 * original database file. This removes tombstones, expired records, and older
 * overwritten versions of keys.
 * 
 * @param fp Open file pointer to the current database.
 * @param db_path Path to the current database file.
 * @param now Current UNIX timestamp to evaluate expirations.
 * @return 0 on success, -1 on failure.
 * 
 * @note This function handles renaming and unlinking of the temporary file internally.
 */
int db_rewrite_compact(FILE *fp, const char *db_path, long now);

#endif
