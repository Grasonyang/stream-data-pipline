#include "sm_reader.h"

#include "libpipeline.h"

#include <string.h>

int sm_reader_parse_line(const char *line, sm_meta_record_t *out) {
    if (line == NULL || out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    if (jsonl_get_string(line, "kind", out->kind, sizeof(out->kind)) != 0 ||
        jsonl_get_uint64(line, "sequence", &out->seq) != 0 ||
        jsonl_get_uint64(line, "offset", &out->offset) != 0 ||
        jsonl_get_uint64(line, "length", &out->length) != 0 ||
        jsonl_get_int64(line, "ts_ms", &out->ts_ms) != 0) {
        return -1;
    }

    /* Optional nested fields such as events are intentionally deferred until
     * jsonl_codec exposes an array helper or stream_merge owns a documented
     * local parser for that specific schema. Keep required scalar parsing on
     * the shared JSONL helpers so stream_merge does not fork JSON behavior. */
    out->valid = 1;
    return 0;
}
