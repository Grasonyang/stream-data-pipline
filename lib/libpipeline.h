/*
 * libpipeline.h
 * version: v2.3
 *
 * Shared low-level helpers used by all applets:
 *   - inotify directory/file watching
 *   - monotonic time
 *   - dynamic byte buffer
 *   - sentinel filename detection
 *
 * See .docs/core/overview.md for the repo-level contract.
 */

#ifndef LIBPIPELINE_H
#define LIBPIPELINE_H

#include <stddef.h>
#include <stdint.h>

#include "dynamic_buffer.h"
#include "jsonl_codec.h"
#include "stream_logger.h"

/**
 * @brief Sentinel filename created by the upstream writer when a session is complete.
 */
#define PIPELINE_SENTINEL_NAME ".pipeline_end"

/**
 * @brief Open an inotify fd that watches a directory for completed writes.
 *
 * Watches at least IN_CLOSE_WRITE | IN_MOVED_TO. Callers own the returned fd
 * and must close it when the watch is no longer needed.
 *
 * @param dir_path Absolute or relative directory path; must already exist.
 * @param watch_descriptor Output location for the inotify watch descriptor.
 * @return inotify fd (>= 0), or -1 on failure with errno set.
 * @note The returned fd is non-blocking and must be closed by the caller.
 */
int lp_watch_dir(const char *dir_path, int *watch_descriptor);

/**
 * @brief Open an inotify fd that watches a file for append/modify events.
 *
 * Watches at least IN_MODIFY. IN_MODIFY lets callers observe append/growing-file
 * updates before the writer closes the fd; IN_CLOSE_WRITE reports writer-close
 * completion. Callers own the returned fd and must close it when the watch is
 * no longer needed.
 *
 * @param file_path Absolute or relative file path; must already exist.
 * @param watch_descriptor Output location for the inotify watch descriptor.
 * @return inotify fd (>= 0), or -1 on failure with errno set.
 * @note The returned fd is non-blocking and must be closed by the caller.
 */
int lp_watch_file(const char *file_path, int *watch_descriptor);

/**
 * @brief Drain one non-blocking inotify fd and detect the pipeline sentinel.
 *
 * Reads and parses all currently queued inotify events from fd. If any event
 * names `.pipeline_end`, `*saw_sentinel` is set to 1. The function tolerates
 * EAGAIN/EWOULDBLOCK and returns once the fd is drained.
 *
 * @param fd Non-blocking inotify fd returned by lp_watch_dir() or lp_watch_file().
 * @param saw_sentinel Output flag updated to 1 when the sentinel appears.
 * @return 0 on success, -1 on invalid args or read failure with errno preserved.
 */
int lp_consume_inotify_events(int fd, int *saw_sentinel);

/**
 * @brief Read the current monotonic time in milliseconds.
 *
 * Uses CLOCK_MONOTONIC so it is not affected by wall-clock changes.
 *
 * @return monotonic timestamp in milliseconds, or 0 if unavailable.
 */
int64_t pipeline_get_monotonic_time_ms(void);

/**
 * @brief Check whether a filename/path refers to the pipeline sentinel.
 *
 * Compares the basename against PIPELINE_SENTINEL_NAME and does not read file contents.
 *
 * @param filename Filename or path to check.
 * @return 1 for sentinel, 0 otherwise.
 * @note Only the basename is compared against PIPELINE_SENTINEL_NAME.
 */
int lp_is_completed_session(const char *filename);

/**
 * @brief Build a path by joining dir and name with a '/' separator.
 *
 * @param out Output buffer.
 * @param sz Size of output buffer in bytes.
 * @param dir Directory path.
 * @param name Filename to append.
 * @return 0 on success, -1 if the result would overflow or args are NULL.
 * @note This always inserts one '/' separator; callers should avoid passing a
 * trailing slash in dir if duplicate separators are undesirable.
 */
int lp_build_artifact_path(char *out, size_t sz, const char *dir, const char *name);

/**
 * @brief Duplicate at most len bytes of src into a newly allocated string.
 *
 * Equivalent to POSIX strndup(). Always NUL-terminates the result.
 *
 * @param src Source buffer. It need not be NUL-terminated within len bytes.
 * @param len Number of bytes to copy.
 * @return Newly allocated NUL-terminated string, or NULL on allocation failure or NULL src.
 * @note The caller owns the returned string and must free it.
 */
char *lp_strndup(const char *src, size_t len);

#endif
