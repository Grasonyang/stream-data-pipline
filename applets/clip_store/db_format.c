#include "db_format.h"
#include "miniz.h"
#include "base64.h"

#include <stdlib.h>
#include <string.h>

void free_db_row(row_t *row) {
    if (row != NULL && row->_alloc != NULL) {
        free(row->_alloc);
        row->_alloc = NULL;
    }
}

int clip_row_is_expired(const row_t *row, long now) {
    /* NULL rows are considered expired/invalid by default */
    if (row == NULL) {
        return 1;
    }
    /* expire_at == 0 means the row never expires */
    return row->expire_at != 0 && row->expire_at <= now;
}

int clip_row_is_tombstone(const row_t *row) {
    /* A tombstone is explicitly marked by an empty value string */
    return row != NULL && row->value != NULL && row->value[0] == '\0';
}

int parse_db_row(char *line, row_t *row) {
    if (line == NULL || row == NULL) {
        return -1;
    }
    row->_alloc = NULL;

    /* Locate the first and second tab delimiters */
    char *a = strchr(line, '\t');
    if (a == NULL) {
        return -1;
    }
    char *b = strchr(a + 1, '\t');
    if (b == NULL) {
        return -1;
    }

    /* Split the line into key, value, and expire_at strings */
    *a = '\0';
    *b = '\0';

    char *end = NULL;
    long expire_at = strtol(b + 1, &end, 10);
    /* Ensure the entire remainder of the line is a valid integer */
    if (end == b + 1 || *end != '\0') {
        return -1;
    }

    row->key = line;
    row->value = a + 1;
    row->expire_at = expire_at;
    
    if (row->value[0] == 'Z' && row->value[1] == ':') {
        size_t b64_len = strlen(row->value + 2);
        size_t comp_len = 0;
        unsigned char *comp = malloc(b64_len + 1);
        if (!comp) return -1;
        
        if (base64_decode(row->value + 2, b64_len, comp, b64_len + 1, &comp_len) != 0) {
            free(comp);
            return -1;
        }
        
        uLongf uncomp_len = comp_len * 4 + 1024;
        unsigned char *uncomp = malloc(uncomp_len);
        if (!uncomp) {
            free(comp);
            return -1;
        }
        
        int status = uncompress(uncomp, &uncomp_len, comp, (uLong)comp_len);
        while (status == Z_BUF_ERROR) {
            uncomp_len *= 2;
            unsigned char *new_uncomp = realloc(uncomp, uncomp_len);
            if (!new_uncomp) {
                free(uncomp);
                free(comp);
                return -1;
            }
            uncomp = new_uncomp;
            status = uncompress(uncomp, &uncomp_len, comp, (uLong)comp_len);
        }
        
        free(comp);
        if (status != Z_OK) {
            free(uncomp);
            return -1;
        }
        
        uncomp[uncomp_len] = '\0';
        row->value = (char *)uncomp;
        row->_alloc = (char *)uncomp;
    }
    
    return 0;
}

int append_db_row(FILE *fp, const char *key, const char *value, long expire_at) {
    if (fp == NULL || key == NULL || value == NULL) {
        return -1;
    }
    /* Always write to the end of the file in append-only DB mode */
    if (fseek(fp, 0, SEEK_END) != 0) {
        return -1;
    }
    
    if (value[0] == '\0') {
        /* Tombstone */
        if (fprintf(fp, "%s\t\t%ld\n", key, expire_at) < 0) {
            return -1;
        }
    } else {
        /* Compress the value */
        uLongf comp_len = compressBound((uLong)strlen(value));
        unsigned char *comp = malloc(comp_len);
        if (!comp) return -1;
        
        if (compress(comp, &comp_len, (const unsigned char *)value, (uLong)strlen(value)) != Z_OK) {
            free(comp);
            return -1;
        }
        
        size_t b64_len = 4 * ((comp_len + 2) / 3) + 1;
        char *b64 = malloc(b64_len + 3); /* "Z:" + b64 + '\0' */
        if (!b64) {
            free(comp);
            return -1;
        }
        
        b64[0] = 'Z';
        b64[1] = ':';
        if (base64_encode(comp, comp_len, b64 + 2, b64_len) != 0) {
            free(comp);
            free(b64);
            return -1;
        }
        free(comp);
        
        if (fprintf(fp, "%s\t%s\t%ld\n", key, b64, expire_at) < 0) {
            free(b64);
            return -1;
        }
        free(b64);
    }
    
    /* Ensure the write is pushed to the OS immediately */
    return fflush(fp);
}
