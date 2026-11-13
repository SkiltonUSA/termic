/* AmiSSL 5.x + Roadshow. No proxy, plaintext fallback or disabled verification. */
#include "native.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#ifdef __VBCC__
/* SDK-derived subset avoids unused stack wrappers in native builds. */
#include "vendor/amissl_tls_vbcc.h"
#else
#include <proto/amissl.h>
#endif
#include <proto/amisslmaster.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>

struct Library *SocketBase, *AmiSSLMasterBase, *AmiSSLBase, *AmiSSLExtBase;
static const char host[] = "api.openai.com";
static long deadline;

static long seconds_now(void)
{
    struct DateStamp now;
    DateStamp(&now);
    return now.ds_Days * 86400L + now.ds_Minute * 60L + now.ds_Tick / 50;
}

static int wait_socket(int fd, int writing)
{
    fd_set rd, wr;
    struct timeval timeout;
    ULONG signals = SIGBREAKF_CTRL_C;
    long remaining = deadline - seconds_now();
    int n;
    if (remaining <= 0) { puts("HTTPS timed out."); return 0; }
    FD_ZERO(&rd); FD_ZERO(&wr);
    if (writing) FD_SET(fd, &wr); else FD_SET(fd, &rd);
    timeout.tv_sec = remaining;
    timeout.tv_usec = 0;
    n = WaitSelect(fd + 1, &rd, &wr, NULL, &timeout, &signals);
    if (signals & SIGBREAKF_CTRL_C) { puts("Cancelled."); return 0; }
    if (n <= 0) { puts("HTTPS timeout or socket error."); return 0; }
    return 1;
}

static int retry_ssl(SSL *ssl, int fd, int result)
{
    int error = SSL_get_error(ssl, result);
    if (error == SSL_ERROR_WANT_READ) return wait_socket(fd, 0);
    if (error == SSL_ERROR_WANT_WRITE) return wait_socket(fd, 1);
    return 0;
}

int cloud_request(const char *key, const char *json, char **reply)
{
    struct hostent *he;
    struct sockaddr_in addr;
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    struct DateStamp date;
    int fd = -1, rc, status = 0, ready = 0, error;
    socklen_t error_size;
    long nonblock = 1;
    size_t sent = 0, used = 0, bytes;
    char *request = NULL, *raw = NULL;
    *reply = NULL;
    DateStamp(&date);
    if (date.ds_Days < 16000) {
        puts("Set the Amiga date and time correctly before HTTPS. Verification remains enabled.");
        return 0;
    }
    if (key && (strlen(key) > 512 || strpbrk(key, "\r\n"))) return 0;
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) { puts("Start Roadshow networking first."); goto done; }
    SetErrnoPtr(&errno, sizeof(errno));
    AmiSSLMasterBase = OpenLibrary("amisslmaster.library", AMISSLMASTER_MIN_VERSION);
    if (!AmiSSLMasterBase || OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
            AmiSSL_UsesOpenSSLStructs, FALSE,
            AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase,
            AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
            AmiSSL_SocketBase, (ULONG)SocketBase, AmiSSL_ErrNoPtr, (ULONG)&errno, TAG_DONE)) {
        puts("AmiSSL 5.23 or compatible library is required."); goto done;
    }
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx || !SSL_CTX_set_default_verify_paths(ctx) ||
        !SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)) {
        puts("Cannot load AmiSSL CA certificates or TLS settings."); goto done;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    ssl = SSL_new(ctx);
    if (!ssl || !SSL_set1_host(ssl, host) || !SSL_set_tlsext_host_name(ssl, host)) goto done;
    puts("Connecting directly to api.openai.com with certificate verification...");
    fflush(stdout);
    he = gethostbyname((STRPTR)host);
    if (!he || he->h_length != 4) { puts("DNS lookup failed."); goto done; }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(443);
    memcpy(&addr.sin_addr, he->h_addr, 4);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || IoctlSocket(fd, FIONBIO, (char *)&nonblock) < 0) goto done;
    deadline = seconds_now() + 120;
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0) {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK) { puts("TCP connect failed."); goto done; }
        if (!wait_socket(fd, 1)) goto done;
        error = 0; error_size = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) < 0 || error) goto done;
    }
    if (!SSL_set_fd(ssl, fd)) goto done;
    while ((rc = SSL_connect(ssl)) != 1) {
        if (!retry_ssl(ssl, fd, rc)) {
            printf("TLS failed: %s. Check clock, CA certificates and network.\n",
                   X509_verify_cert_error_string(SSL_get_verify_result(ssl)));
            goto done;
        }
    }
    bytes = (json ? strlen(json) : 0) + (key ? strlen(key) : 0) + 512;
    request = malloc(bytes);
    raw = malloc(RESPONSE_LIMIT + 1);
    if (!request || !raw) goto done;
    if (json) sprintf(request,
        "POST /v1/responses HTTP/1.1\r\nHost: %s\r\nAuthorization: Bearer %s\r\n"
        "Content-Type: application/json\r\nAccept-Encoding: identity\r\n"
        "Content-Length: %lu\r\nConnection: close\r\n\r\n%s",
        host, key, (unsigned long)strlen(json), json);
    else sprintf(request, "GET /v1/models HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
    bytes = strlen(request);
    while (sent < bytes) {
        rc = SSL_write(ssl, request + sent, (int)(bytes - sent));
        if (rc > 0) sent += rc;
        else if (!retry_ssl(ssl, fd, rc)) { puts("HTTPS write failed."); goto done; }
    }
    while (used < RESPONSE_LIMIT) {
        rc = SSL_read(ssl, raw + used, (int)(RESPONSE_LIMIT - used));
        if (rc > 0) used += rc;
        else if (SSL_get_error(ssl, rc) != SSL_ERROR_ZERO_RETURN) {
            if (retry_ssl(ssl, fd, rc)) continue;
            puts("HTTPS read failed."); goto done;
        }
        raw[used] = 0;
        ready = decode_http(raw, used, rc <= 0, &status, reply);
        if (ready < 0) { puts("Malformed, oversized or unsupported HTTP response."); goto done; }
        if (ready > 0) break;
        if (rc <= 0) goto done;
    }
    if (ready != 1) { puts("HTTP response exceeded 256 KiB."); status = 0; }
done:
    if (ready != 1) { free(*reply); *reply = NULL; status = 0; }
    if (request) { memset(request, 0, strlen(request)); free(request); }
    free(raw);
    if (ssl) SSL_free(ssl);
    if (ctx) SSL_CTX_free(ctx);
    if (fd >= 0) CloseSocket(fd);
    if (AmiSSLBase) CloseAmiSSL();
    if (AmiSSLMasterBase) CloseLibrary(AmiSSLMasterBase);
    if (SocketBase) CloseLibrary(SocketBase);
    AmiSSLBase = AmiSSLExtBase = AmiSSLMasterBase = SocketBase = NULL;
    return status;
}
