#ifndef SM_EVENTS_H
#define SM_EVENTS_H

#include "libpipeline.h"

#include <stddef.h>

#define SM_EVENT_MAX_ITEMS 16
#define SM_EVENT_MAX_LEN   64

/**
 * @brief Small string set used to aggregate chunk-level events into a clip.
 *
 * Event volume is expected to be small for each time window, so a fixed-size
 * linear set keeps the implementation deterministic and avoids heap ownership
 * complexity inside the state machine.
 */
typedef struct {
    char items[SM_EVENT_MAX_ITEMS][SM_EVENT_MAX_LEN];
    size_t count;
} sm_event_set_t;

/**
 * @brief Clear all stored events.
 *
 * @param set Event set to reset.
 */
void sm_event_set_reset(sm_event_set_t *set);

/**
 * @brief Add one event tag to a set if it is not already present.
 *
 * Use this when parsing one tag from a chunk-level events array, e.g. adding
 * "motion" into rec.events. This function stores names uniquely within set.
 *
 * @param set Event set that receives the tag.
 * @param tag NUL-terminated event tag/name.
 * @return 0 on success, -1 if the set is full, the tag is too long, or args are invalid.
 */
int sm_event_set_add_tag(sm_event_set_t *set, const char *tag);

/**
 * @brief Add every tag from one event set into another event set.
 *
 * Use this when the FSM appends one chunk's rec.events into the current clip's
 * fsm.events. Each tag is still kept unique in the destination set.
 *
 * @param dst Destination event set, usually the current clip-level set.
 * @param src Source event set, usually the chunk-level set.
 * @return 0 on success, -1 if any tag cannot be copied.
 */
int sm_event_set_add_all(sm_event_set_t *dst, const sm_event_set_t *src);

/**
 * @brief Append a JSON array representation of events to a dynamic buffer.
 *
 * @param set Event set to render.
 * @param out Destination dynamic buffer.
 * @return 0 on success, -1 on allocation failure.
 */
int sm_event_set_append_json(const sm_event_set_t *set, dynamic_buffer_t *out);

#endif
