#include "jsonl_codec.h"

#include "cJSON.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

/*
 * cJSON stores numbers as double. Keep the public integer getters conservative
 * so callers never receive silently rounded sequence, offset, or timestamp data.
 */
#define JSONL_MAX_SAFE_INTEGER 9007199254740991.0

static cJSON *parse_object(const char *line) {
    if (line == NULL) {
        return NULL;
    }

    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithOpts(line, &parse_end, 0);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    while (parse_end != NULL && *parse_end != '\0' && isspace((unsigned char)*parse_end)) {
        ++parse_end;
    }
    if (parse_end == NULL || *parse_end != '\0') {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

static cJSON *get_item(cJSON *root, const char *key) {
    if (key == NULL) {
        return NULL;
    }
    return cJSON_GetObjectItemCaseSensitive(root, key);
}

/* Each getter owns the cJSON tree locally to keep the public API stateless. */
int jsonl_get_string(const char *line, const char *key, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return -1;
    }

    cJSON *root = parse_object(line);
    cJSON *item = get_item(root, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        cJSON_Delete(root);
        return -1;
    }

    size_t len = strlen(item->valuestring);
    if (len >= out_size) {
        cJSON_Delete(root);
        return -1;
    }
    memcpy(out, item->valuestring, len + 1);
    cJSON_Delete(root);
    return 0;
}

int jsonl_get_int64(const char *line, const char *key, int64_t *out) {
    if (out == NULL) {
        return -1;
    }

    cJSON *root = parse_object(line);
    cJSON *item = get_item(root, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < -JSONL_MAX_SAFE_INTEGER ||
        item->valuedouble > JSONL_MAX_SAFE_INTEGER) {
        cJSON_Delete(root);
        return -1;
    }

    int64_t v = (int64_t)item->valuedouble;
    /* Reject fractional values that cJSON otherwise accepts as numbers. */
    if ((double)v != item->valuedouble) {
        cJSON_Delete(root);
        return -1;
    }
    *out = v;
    cJSON_Delete(root);
    return 0;
}

int jsonl_get_uint64(const char *line, const char *key, uint64_t *out) {
    if (out == NULL) {
        return -1;
    }

    cJSON *root = parse_object(line);
    cJSON *item = get_item(root, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > JSONL_MAX_SAFE_INTEGER) {
        cJSON_Delete(root);
        return -1;
    }

    uint64_t v = (uint64_t)item->valuedouble;
    /* Reject fractional values and any value that would lose precision. */
    if ((double)v != item->valuedouble) {
        cJSON_Delete(root);
        return -1;
    }
    *out = v;
    cJSON_Delete(root);
    return 0;
}

int jsonl_get_bool(const char *line, const char *key, int *out) {
    if (out == NULL) {
        return -1;
    }

    cJSON *root = parse_object(line);
    cJSON *item = get_item(root, key);
    if (cJSON_IsTrue(item)) {
        *out = 1;
    } else if (cJSON_IsFalse(item)) {
        *out = 0;
    } else {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);
    return 0;
}

int jsonl_is_object(const char *line) {
    cJSON *root = parse_object(line);
    if (root == NULL) {
        return 0;
    }
    cJSON_Delete(root);
    return 1;
}

int jsonl_get_scalar_text(const char *line, const char *key, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return -1;
    }

    cJSON *root = parse_object(line);
    cJSON *item = get_item(root, key);
    if (item == NULL || cJSON_IsObject(item) || cJSON_IsArray(item)) {
        cJSON_Delete(root);
        return -1;
    }

    if (cJSON_IsString(item)) {
        if (item->valuestring == NULL) {
            cJSON_Delete(root);
            return -1;
        }
        size_t len = strlen(item->valuestring);
        if (len >= out_size) {
            cJSON_Delete(root);
            return -1;
        }
        memcpy(out, item->valuestring, len + 1);
        cJSON_Delete(root);
        return 0;
    }

    char *printed = cJSON_PrintUnformatted(item);
    if (printed == NULL) {
        cJSON_Delete(root);
        return -1;
    }
    size_t len = strlen(printed);
    if (len >= out_size) {
        cJSON_free(printed);
        cJSON_Delete(root);
        return -1;
    }
    memcpy(out, printed, len + 1);
    cJSON_free(printed);
    cJSON_Delete(root);
    return 0;
}

int jsonl_write_string(dynamic_buffer_t *buf, const char *value) {
    if (buf == NULL || value == NULL) {
        return -1;
    }

    cJSON *string = cJSON_CreateString(value);
    if (string == NULL) {
        return -1;
    }

    char *printed = cJSON_PrintUnformatted(string);
    cJSON_Delete(string);
    if (printed == NULL) {
        return -1;
    }
    int rc = dynamic_buffer_append_str(buf, printed);
    cJSON_free(printed);
    return rc;
}
