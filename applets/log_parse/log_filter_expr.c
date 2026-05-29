#include "log_filter_expr.h"

#include "libpipeline.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *log_get(const log_t *log, const char *key) {
    for (size_t i = 0; i < log->count; ++i)
        if (strcmp(log->names[i], key) == 0)
            return log->values[i];
    return NULL;
}

static int parse_i64(const char *s, int64_t *out) {
    if (s == NULL || out == NULL)
        return -1;

    errno = 0;
    char *end = NULL;
    /*
    strtoll is used to handle 64-bit integers,
    - 10 means base 10, 
    - end means the pointer to the first character after the number
    */
    long long v = strtoll(s, &end, 10); 
    if (end == s || *end != '\0' || errno != 0)
        return -1;
        
    *out = (int64_t)v;
    return 0;
}

int log_filter_parse(char *expr, filter_t *filter) {
    if (expr == NULL || filter == NULL)
        return -1;

    char *op = strstr(expr, "!=");
    if (op != NULL) {
        filter->op = FILTER_NOT_EQUALS;
        *op = '\0';
        filter->key = expr;
        filter->value = op + 2;
    } else if ((op = strchr(expr, '>')) != NULL) {
        filter->op = FILTER_GREATER_THAN;
        *op = '\0';
        filter->key = expr;
        filter->value = op + 1;
    } else if ((op = strchr(expr, '~')) != NULL) {
        filter->op = FILTER_CONTAINS;
        *op = '\0';
        filter->key = expr;
        filter->value = op + 1;
    } else if ((op = strchr(expr, '=')) != NULL) {
        filter->op = FILTER_EQUALS;
        *op = '\0';
        filter->key = expr;
        filter->value = op + 1;
    } else {
        return -1;
    }

    return filter->key[0] != '\0' && filter->value[0] != '\0' ? 0 : -1;
}

int log_filter_match_fields(const log_t *log, const filter_t *filter) {
    if (filter == NULL || filter->key == NULL)
        return 1;

    const char *value = log_get(log, filter->key);
    if (value == NULL)
        return 0;

    switch (filter->op) {
        case FILTER_EQUALS:
            return strcmp(value, filter->value) == 0;
        case FILTER_NOT_EQUALS:
            return strcmp(value, filter->value) != 0;
        case FILTER_CONTAINS:
            return strstr(value, filter->value) != NULL;
        case FILTER_GREATER_THAN: {
            int64_t lhs = 0;
            int64_t rhs = 0;
            if (parse_i64(value, &lhs) != 0 || parse_i64(filter->value, &rhs) != 0) {
                return 0;
            }
            return lhs > rhs;
        }
        default:
            return 0;
    }
}

int log_filter_match_jsonl(const char *line, const filter_t *filter, int *matched) {
    if (line == NULL || matched == NULL)
        return -1;

    *matched = 0;
    
    if (!jsonl_is_object(line))
        return -1;

    if (filter == NULL || filter->key == NULL) {
        *matched = 1;
        return 0;
    }

    switch (filter->op) {
        case FILTER_EQUALS:
        case FILTER_NOT_EQUALS:
        case FILTER_CONTAINS: {
            char value[1024] = {0};
            if (jsonl_get_scalar_text(line, filter->key, value, sizeof(value)) != 0) {
                return -1;
            }
            
            if (filter->op == FILTER_EQUALS) {
                *matched = strcmp(value, filter->value) == 0;
            } else if (filter->op == FILTER_NOT_EQUALS) {
                *matched = strcmp(value, filter->value) != 0;
            } else {
                *matched = strstr(value, filter->value) != NULL;
            }
            return 0;
        }
        case FILTER_GREATER_THAN: {
            int64_t lhs = 0;
            int64_t rhs = 0;
            if (jsonl_get_int64(line, filter->key, &lhs) != 0 ||
                parse_i64(filter->value, &rhs) != 0) {
                return 0;
            }
            *matched = lhs > rhs;
            return 0;
        }
        default:
            return -1;
    }
}
