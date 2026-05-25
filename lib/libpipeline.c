/*
 * Shared OS-facing helpers for pipeline applets.
 *
 * This file intentionally keeps low-level filesystem/watch utilities separate
 * from applet policy. Callers own returned file descriptors and allocated
 * strings unless a function explicitly returns an error.
 */

#include "libpipeline.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <time.h>
#include <unistd.h>

int lp_watch_dir(const char *dir_path, int *watch_descriptor) {
    if (dir_path == NULL || watch_descriptor == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Use a non-blocking inotify fd so the caller can integrate it into polling loops. */
    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    /* Report files that become ready in the directory, either after close or rename into place. */
    int wd = inotify_add_watch(fd, dir_path, IN_CLOSE_WRITE | IN_MOVED_TO);
    if (wd < 0) {
        /* Preserve the original watch failure even if close() touches errno. */
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    *watch_descriptor = wd;
    return fd;
}

int lp_watch_file(const char *file_path, int *watch_descriptor) {
    if (file_path == NULL || watch_descriptor == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* The file watch uses the same non-blocking fd contract as directory watches. */
    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    /*
     * IN_MODIFY reports growing-file updates while the writer still holds the
     * fd open; IN_CLOSE_WRITE reports the eventual writer-close completion.
     */
    int wd = inotify_add_watch(fd, file_path, IN_MODIFY | IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF);
    if (wd < 0) {
        /* Preserve the original watch failure even if close() touches errno. */
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    *watch_descriptor = wd;
    return fd;
}

int lp_consume_inotify_events(int fd, int *saw_sentinel) {
    if (fd < 0 || saw_sentinel == NULL) {
        errno = EINVAL;
        return -1;
    }

    char buf[4096];
    for (;;) {
        ssize_t got = read(fd, buf, sizeof(buf));
        if (got <= 0) {
            if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return 0;
            }
            return got == 0 ? 0 : -1;
        }

        for (ssize_t off = 0; off + (ssize_t)sizeof(struct inotify_event) <= got;) {
            const struct inotify_event *ev = (const struct inotify_event *)(buf + off);
            if (ev->len > 0 && lp_is_completed_session(ev->name)) {
                *saw_sentinel = 1;
            }
            off += (ssize_t)sizeof(struct inotify_event) + (ssize_t)ev->len;
        }
    }
}

int64_t pipeline_get_monotonic_time_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    /* Convert seconds plus nanoseconds to a monotonic millisecond counter. */
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


int lp_is_completed_session(const char *filename) {
    if (filename == NULL) {
        return 0;
    }

    /* Only the basename is meaningful; parent directory names are ignored. */
    const char *base = strrchr(filename, '/');
    if (base != NULL) {
        filename = base + 1;
    }

    return strcmp(filename, PIPELINE_SENTINEL_NAME) == 0 ? 1 : 0;
}

int lp_build_artifact_path(char *out, size_t sz, const char *dir, const char *name) {
    if (out == NULL || dir == NULL || name == NULL) {
        return -1;
    }
    /* snprintf gives one overflow check for both formatting and NUL termination. */
    int n = snprintf(out, sz, "%s/%s", dir, name);
    return n >= 0 && (size_t)n < sz ? 0 : -1;
}

char *lp_strndup(const char *src, size_t len) {
    if (src == NULL || len > SIZE_MAX - 1) {
        return NULL;
    }
    char *out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, src, len);
    out[len] = '\0';
    return out;
}
