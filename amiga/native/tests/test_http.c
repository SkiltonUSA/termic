#include "native.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void check(const char *wire, int eof, int expected, const char *text)
{
    char *body = NULL;
    int status = 0;
    int got = decode_http(wire, strlen(wire), eof, &status, &body);
    assert(got == expected);
    if (expected == 1) { assert(status >= 200); assert(!strcmp(body, text)); }
    else assert(body == NULL);
    free(body);
}
int main(void)
{
    const char *complete = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    size_t i;
    for (i = 0; i < strlen(complete); ++i) {
        char partial[128];
        memcpy(partial, complete, i); partial[i] = 0;
        check(partial, 0, 0, NULL);
    }
    check(complete, 0, 1, "hello");
    check("HTTP/1.1 401 Unauthorized\r\nContent-Length: 2\r\n\r\n{}", 0, 1, "{}");
    check("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nhe\r\n3;x=1\r\nllo\r\n0\r\n\r\n", 0, 1, "hello");
    check("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nhi\r\n0\r\nTrailer: value\r\n\r\n", 0, 1, "hi");
    check("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nhi", 1, -1, NULL);
    check("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n", 1, -1, NULL);
    check("HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\n", 1, -1, NULL);
    check("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n", 1, -1, NULL);
    check("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 2\r\n\r\nhi", 1, -1, NULL);
    check("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n", 1, -1, NULL);
    check("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\ng\r\n", 1, -1, NULL);
    check("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nhi\r\n0\r\n", 1, -1, NULL);
    check("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nhello", 1, 1, "hello");
    check("garbage\r\n\r\n", 1, -1, NULL);
    puts("HTTP framing tests passed.");
    return 0;
}
