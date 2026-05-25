#include "sm_events.h"

#include <string.h>

void sm_event_set_reset(sm_event_set_t *set) {
    if (set != NULL)
        memset(set, 0, sizeof(*set));
}

int sm_event_set_add_tag(sm_event_set_t *set, const char *tag) {
    if (set == NULL || tag == NULL || tag[0] == '\0') {
        return -1;
    }

    size_t len = strlen(tag);
    if (len >= SM_EVENT_MAX_LEN) {
        return -1;
    }

    for (size_t i = 0; i < set->count; ++i) {
        if (strcmp(set->items[i], tag) == 0) {
            return 0;
        }
    }

    if (set->count >= SM_EVENT_MAX_ITEMS) {
        return -1;
    }

    memcpy(set->items[set->count], tag, len + 1);
    set->count++;
    return 0;
}

int sm_event_set_add_all(sm_event_set_t *dst, const sm_event_set_t *src) {
    if (dst == NULL || src == NULL) {
        return -1;
    }

    for (size_t i = 0; i < src->count; ++i) {
        if (sm_event_set_add_tag(dst, src->items[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int sm_event_set_append_json(const sm_event_set_t *set, dynamic_buffer_t *out) {
    if (set == NULL || out == NULL) {
        return -1;
    }

    if (dynamic_buffer_append_char(out, '[') != 0) {
        return -1;
    }
    for (size_t i = 0; i < set->count; ++i) {
        if (i > 0 && dynamic_buffer_append_char(out, ',') != 0) {
            return -1;
        }
        if (jsonl_write_string(out, set->items[i]) != 0) {
            return -1;
        }
    }
    return dynamic_buffer_append_char(out, ']');
}
