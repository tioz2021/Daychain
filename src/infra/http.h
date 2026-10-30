#ifndef HABITS_INFRA_HTTP_H
#define HABITS_INFRA_HTTP_H

#include <stdbool.h>
#include <stddef.h>

#include "infra/util.h"

#define HTTP_MAX_HEADERS 64
#define HTTP_MAX_HEADER_BYTES (16 * 1024)
#define HTTP_MAX_BODY_BYTES (1024 * 1024)

typedef struct {
    char method[8];
    char path[1024];
    char query[2048];
    char host[256];
    char origin[256];
    char requested_with[64];
    char content_type[128];
    char *body;
    size_t body_len;
} HttpRequest;

typedef struct {
    int status;
    char content_type[128];
    StrBuf body;
    char filename[128]; /* non-empty -> Content-Disposition attachment */
} HttpResponse;

typedef void (*HttpHandler)(const HttpRequest *req, HttpResponse *res, void *ud);
typedef void (*HttpReadyFn)(int port, void *ud);

typedef struct {
    const char *host;
    int port;
    HttpHandler handler;
    void *handler_ud;
    HttpReadyFn on_ready; /* called once the socket is listening; may be NULL */
    void *ready_ud;
} HttpConfig;

/* Serve until http_stop() is called. Returns 0 on success, -1 on setup failure. */
int http_serve(const HttpConfig *cfg, int *out_port, char *err, size_t errsz);

void http_stop(void);
const char *http_status_text(int status);

#endif
