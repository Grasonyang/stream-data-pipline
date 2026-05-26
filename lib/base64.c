#include "base64.h"
#include <stdint.h>
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int base64_encode(const unsigned char *src, size_t len, char *out, size_t out_max) {
    size_t out_len = 4 * ((len + 2) / 3);
    if (out_max < out_len + 1) return -1;
    size_t i = 0, j = 0;
    for (i = 0; i < len;) {
        uint32_t octet_a = i < len ? src[i++] : 0;
        uint32_t octet_b = i < len ? src[i++] : 0;
        uint32_t octet_c = i < len ? src[i++] : 0;
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        out[j++] = b64_table[(triple >> 3 * 6) & 0x3F];
        out[j++] = b64_table[(triple >> 2 * 6) & 0x3F];
        out[j++] = b64_table[(triple >> 1 * 6) & 0x3F];
        out[j++] = b64_table[(triple >> 0 * 6) & 0x3F];
    }
    for (int pad = 0; pad < (3 - len % 3) % 3; pad++) out[out_len - 1 - pad] = '=';
    out[out_len] = '\0';
    return 0;
}
static int b64_index(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
int base64_decode(const char *src, size_t len, unsigned char *out, size_t out_max, size_t *out_len) {
    if (len % 4 != 0) return -1;
    size_t ol = len / 4 * 3;
    if (src[len - 1] == '=') ol--;
    if (len > 1 && src[len - 2] == '=') ol--;
    if (out_max < ol) return -1;
    size_t i = 0, j = 0;
    for (i = 0; i < len;) {
        int a = src[i] == '=' ? 0 : b64_index(src[i]); i++;
        int b = src[i] == '=' ? 0 : b64_index(src[i]); i++;
        int c = src[i] == '=' ? 0 : b64_index(src[i]); i++;
        int d = src[i] == '=' ? 0 : b64_index(src[i]); i++;
        if (a < 0 || b < 0 || c < 0 || d < 0) return -1;
        uint32_t triple = (a << 3 * 6) + (b << 2 * 6) + (c << 1 * 6) + d;
        if (j < ol) out[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < ol) out[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < ol) out[j++] = (triple >> 0 * 8) & 0xFF;
    }
    *out_len = ol;
    return 0;
}
