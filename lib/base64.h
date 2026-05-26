#ifndef BASE64_H
#define BASE64_H
#include <stddef.h>
int base64_encode(const unsigned char *src, size_t len, char *out, size_t out_max);
int base64_decode(const char *src, size_t len, unsigned char *out, size_t out_max, size_t *out_len);
#endif
