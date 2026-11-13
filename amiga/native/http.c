/* Bounded HTTP/1.x response framing, shared by native transport and host tests. */
#include "native.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static int equal(const char *s, size_t n, const char *want)
{
    size_t i;
    if (strlen(want) != n) return 0;
    for (i = 0; i < n; ++i)
        if (tolower((unsigned char)s[i]) != (unsigned char)want[i]) return 0;
    return 1;
}

int decode_http(const char *raw, size_t size, int eof, int *status, char **body)
{
    const char *end, *line, *next, *colon, *p, *q;
    size_t header, length = 0, available, used = 0, chunk;
    int have_length = 0, chunked = 0, code;
    char *out;
    *body = NULL;
    if (size > RESPONSE_LIMIT || memchr(raw, 0, size)) return -1;
    /* Caller supplies a NUL after size. */
    end = strstr(raw, "\r\n\r\n");
    if (!end) return (eof || size > 16384) ? -1 : 0;
    header = (size_t)(end + 4 - raw);
    if (header > 16384 || sscanf(raw, "HTTP/1.%*d %d", &code) != 1 ||
        code < 200 || code > 599) return -1;
    *status = code;
    line = strstr(raw, "\r\n") + 2;
    while (line < end) {
        next = strstr(line, "\r\n");
        colon = memchr(line, ':', (size_t)(next - line));
        if (!colon) return -1;
        p = colon + 1;
        while (p < next && (*p == ' ' || *p == '\t')) ++p;
        q = next;
        while (q > p && (q[-1] == ' ' || q[-1] == '\t')) --q;
        if (equal(line, (size_t)(colon - line), "content-length")) {
            if (have_length || p == q) return -1;
            have_length = 1;
            for (; p < q; ++p) {
                if (*p < '0' || *p > '9' || length > RESPONSE_LIMIT / 10) return -1;
                length = length * 10 + (*p - '0');
                if (length > RESPONSE_LIMIT) return -1;
            }
        } else if (equal(line, (size_t)(colon - line), "transfer-encoding")) {
            if (chunked || !equal(p, (size_t)(q - p), "chunked")) return -1;
            chunked = 1;
        } else if (equal(line, (size_t)(colon - line), "content-encoding") &&
                   !equal(p, (size_t)(q - p), "identity")) return -1;
        line = next + 2;
    }
    if (have_length && chunked) return -1;
    available = size - header;
    if (!chunked) {
        if (have_length && available < length) return eof ? -1 : 0;
        if (!have_length && !eof) return 0;
        if (!have_length) length = available;
        out = malloc(length + 1);
        if (!out) return -1;
        memcpy(out, raw + header, length);
        out[length] = 0;
        *body = out;
        return 1;
    }
    out = malloc(available + 1);
    if (!out) return -1;
    p = raw + header;
    while (p < raw + size) {
        next = strstr(p, "\r\n");
        if (!next) break;
        chunk = 0;
        q = p;
        while (p < next && isxdigit((unsigned char)*p)) {
            int v = isdigit((unsigned char)*p) ? *p - '0' : tolower((unsigned char)*p) - 'a' + 10;
            if (chunk > RESPONSE_LIMIT / 16) goto bad;
            chunk = chunk * 16 + v;
            ++p;
        }
        if (p == q || (p != next && *p != ';') || chunk > RESPONSE_LIMIT) goto bad;
        p = next + 2;
        if (!chunk) {
            if ((size_t)(raw + size - p) < 2) break;
            if (strncmp(p, "\r\n", 2) && !strstr(p, "\r\n\r\n")) break;
            out[used] = 0;
            *body = out;
            return 1;
        }
        if ((size_t)(raw + size - p) < chunk + 2) break;
        if (p[chunk] != '\r' || p[chunk + 1] != '\n' || used + chunk > RESPONSE_LIMIT) goto bad;
        memcpy(out + used, p, chunk);
        used += chunk;
        p += chunk + 2;
    }
    free(out);
    return eof ? -1 : 0;
bad:
    free(out);
    return -1;
}
