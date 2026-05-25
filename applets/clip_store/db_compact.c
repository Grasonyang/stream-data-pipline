#include "db_compact.h"

#include "db_format.h"
#include "db_query.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int db_rewrite_compact(FILE *fp, const char *db_path, long now) {
    if (fp == NULL || db_path == NULL) {
        return -1;
    }

    /* Rebuild latest key state with a hash index before rewriting the file. */
    row_index_t rows = {0};
    if (load_latest_rows(fp, &rows) != 0) {
        row_index_free(&rows);
        return -1;
    }

    char tmp_path[PATH_MAX];
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", db_path) >= (int)sizeof(tmp_path)) {
        row_index_free(&rows);
        return -1;
    }

    /* Open a temporary file in the same directory to allow atomic rename */
    int tmp_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (tmp_fd < 0) {
        row_index_free(&rows);
        return -1;
    }

    FILE *tmp_fp = fdopen(tmp_fd, "w");
    if (tmp_fp == NULL) {
        close(tmp_fd);
        unlink(tmp_path);
        row_index_free(&rows);
        return -1;
    }

    /* Write only live rows back to the temporary file */
    for (size_t i = 0; i < rows.cap; ++i) {
        if (!rows.used[i]) {
            continue;
        }
        if (!row_is_live(&rows.rows[i], now)) {
            continue;
        }
        if (append_db_row(tmp_fp, rows.rows[i].key, rows.rows[i].value, rows.rows[i].expire_at) != 0) {
            fclose(tmp_fp);
            unlink(tmp_path);
            row_index_free(&rows);
            return -1;
        }
    }

    /* Ensure data is flushed to disk before renaming */
    if (fflush(tmp_fp) != 0 || fsync(fileno(tmp_fp)) != 0) {
        fclose(tmp_fp);
        unlink(tmp_path);
        row_index_free(&rows);
        return -1;
    }

    fclose(tmp_fp);
    row_index_free(&rows);

    /* Atomically replace the old database with the compacted temporary file */
    if (rename(tmp_path, db_path) != 0) {
        unlink(tmp_path);
        return -1;
    }

    return 0;
}
