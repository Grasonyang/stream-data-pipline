#ifndef SM_READER_H
#define SM_READER_H

#include "sm_fsm.h"

/**
 * @brief Parse one JSONL sidecar line into a normalized metadata record.
 *
 * Required fields are kind, sequence, offset, length, and ts_ms. Legacy seq is
 * intentionally rejected so the sidecar schema remains canonical. Optional
 * nested fields are deferred until the shared JSONL codec supports arrays.
 *
 * @param line NUL-terminated JSON object line.
 * @param out Destination record.
 * @return 0 on success, -1 on malformed or incomplete metadata.
 */
int sm_reader_parse_line(const char *line, sm_meta_record_t *out);

#endif
