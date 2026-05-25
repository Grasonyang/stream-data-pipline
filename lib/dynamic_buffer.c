#include "dynamic_buffer.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * dynamic_buffer keeps a simple append-only invariant: data[0..len) is caller
 * content, data[len] is always '\0', and failed prevents accidental reuse after
 * an allocation or contract error.
 */

void dynamic_buffer_free(dynamic_buffer_t *buf) {
    if (buf == NULL)
        return;
    
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->failed = 0;
}

void dynamic_buffer_reset(dynamic_buffer_t *buf) {
    if (buf == NULL)
        return;

    buf->len = 0;
    buf->failed = 0;
    if (buf->data != NULL)
        buf->data[0] = '\0';
    else
        buf->cap = 0;
}

int dynamic_buffer_has_failed(const dynamic_buffer_t *buf) {
    return buf != NULL && buf->failed ? 1 : 0;
}

int dynamic_buffer_reserve(dynamic_buffer_t *buf, size_t extra) {
    if (buf == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (buf->failed) {
        errno = EIO;
        return -1;
    }
    if (buf->cap > 0 && buf->data == NULL) {
        buf->failed = 1;
        errno = EINVAL;
        return -1;
    }
    if (buf->data != NULL && buf->len >= buf->cap) {
        buf->failed = 1;
        errno = EINVAL;
        return -1;
    }

    /* Need = len + extra + trailing NUL; guard overflow before addition. */
    if (extra > SIZE_MAX - 1 - buf->len) {
        buf->failed = 1;
        errno = EOVERFLOW;
        return -1;
    }

    size_t need = buf->len + extra + 1;
    if (need <= buf->cap)
        return 0;

    size_t next = buf->cap == 0 ? 128 : buf->cap;
    /* Double capacity for amortized append cost, falling back to exact need near SIZE_MAX. */
    while (next < need) {
        if (next > SIZE_MAX / 2) {
            next = need;
            break;
        }
        next *= 2;
    }

    char *data = realloc(buf->data, next);
    if (data == NULL) {
        buf->failed = 1;
        errno = ENOMEM;
        return -1;
    }

    buf->data = data;
    buf->cap = next;
    /* Enforce the NUL-terminator invariant immediately after allocation */
    buf->data[buf->len] = '\0';
    return 0;
}

int dynamic_buffer_append_char(dynamic_buffer_t *buf, char c) {
    if (dynamic_buffer_reserve(buf, 1) != 0) {
        if (buf != NULL)
            buf->failed = 1;
        return -1;
    }
    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
    return 0;
}

/* Unlike append_str, this accepts embedded NUL bytes and a caller-provided length. */
int dynamic_buffer_append_mem(dynamic_buffer_t *buf, const void *src, size_t len) {
    if (buf != NULL && buf->failed) {
        errno = EIO;
        return -1;
    }
    if (buf == NULL || (src == NULL && len > 0)) {
        if (buf != NULL) {
            buf->failed = 1;
        }
        errno = EINVAL;
        return -1;
    }
    if (dynamic_buffer_reserve(buf, len) != 0) {
        if (buf != NULL) {
            buf->failed = 1;
        }
        return -1;
    }
    if (len > 0) {
        memcpy(buf->data + buf->len, src, len);
        buf->len += len;
    }
    buf->data[buf->len] = '\0';
    return 0;
}

int dynamic_buffer_append_str(dynamic_buffer_t *buf, const char *s) {
    if (buf != NULL && buf->failed) {
        errno = EIO;
        return -1;
    }
    if (s == NULL) {
        if (buf != NULL) {
            buf->failed = 1;
        }
        errno = EINVAL;
        return -1;
    }
    return dynamic_buffer_append_mem(buf, s, strlen(s));
}
