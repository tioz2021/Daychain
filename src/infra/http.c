#include "infra/http.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

void http_stop(void) {
    g_stop = 1;
}

const char *http_status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}

static void set_err(char *err, size_t errsz, const char *msg) {
    if (err && errsz) snprintf(err, errsz, "%s", msg);
}

/* ---- request parsing ----------------------------------------------------- */

static const char *find_header_end(const char *data, size_t len) {
    if (len < 4) return NULL;
    for (size_t i = 0; i + 3 < len; i++)
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n')
            return data + i;
    return NULL;
}

static void copy_str(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void trim(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t') start++;
    size_t n = strlen(start);
    while (n > 0 && (start[n - 1] == ' ' || start[n - 1] == '\t')) n--;
    memmove(s, start, n);
    s[n] = '\0';
}

static bool parse_headers(HttpRequest *req, char *block, const char **content_length) {
    char *save = NULL;
    char *line = strtok_r(block, "\r\n", &save);
    if (!line) return false;
    char target[2048];
    if (sscanf(line, "%7s %2047s", req->method, target) != 2) return false;

    char *q = strchr(target, '?');
    if (q) {
        *q = '\0';
        copy_str(req->query, sizeof req->query, q + 1);
    }
    copy_str(req->path, sizeof req->path, target);

    *content_length = NULL;
    while ((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *value = colon + 1;
        trim(value);
        trim(line);
        if (strcasecmp(line, "Host") == 0)
            copy_str(req->host, sizeof req->host, value);
        else if (strcasecmp(line, "Origin") == 0)
            copy_str(req->origin, sizeof req->origin, value);
        else if (strcasecmp(line, "X-Requested-With") == 0)
            copy_str(req->requested_with, sizeof req->requested_with, value);
        else if (strcasecmp(line, "Content-Type") == 0)
            copy_str(req->content_type, sizeof req->content_type, value);
        else if (strcasecmp(line, "Content-Length") == 0)
            *content_length = value;
    }
    return true;
}

/* Returns 0 on success, or an HTTP status code on failure. */
static int read_request(int fd, HttpRequest *req) {
    StrBuf raw;
    sb_init(&raw);
    char chunk[8192];
    size_t header_end = 0;

    for (;;) {
        const char *end = find_header_end(raw.data, raw.len);
        if (end) {
            header_end = (size_t)(end - raw.data) + 4;
            break;
        }
        if (raw.len > HTTP_MAX_HEADER_BYTES) {
            sb_free(&raw);
            return 413;
        }
        ssize_t n = recv(fd, chunk, sizeof chunk, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            sb_free(&raw);
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? 408 : 400;
        }
        if (n == 0) {
            sb_free(&raw);
            return 400;
        }
        sb_add(&raw, chunk, (size_t)n);
    }

    char *head = xstrndup(raw.data, header_end);
    const char *clen = NULL;
    bool ok = parse_headers(req, head, &clen);
    char clen_buf[32] = "";
    if (clen) copy_str(clen_buf, sizeof clen_buf, clen);
    free(head);
    if (!ok) {
        sb_free(&raw);
        return 400;
    }

    long long content_length = 0;
    if (clen_buf[0]) {
        char *endp = NULL;
        content_length = strtoll(clen_buf, &endp, 10);
        if (!endp || *endp != '\0' || content_length < 0) {
            sb_free(&raw);
            return 400;
        }
        if (content_length > HTTP_MAX_BODY_BYTES) {
            sb_free(&raw);
            return 413;
        }
    }

    while (raw.len < header_end + (size_t)content_length) {
        ssize_t n = recv(fd, chunk, sizeof chunk, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            sb_free(&raw);
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? 408 : 400;
        }
        if (n == 0) break;
        sb_add(&raw, chunk, (size_t)n);
    }

    req->body_len = raw.len - header_end;
    if (req->body_len > (size_t)content_length) req->body_len = (size_t)content_length;
    req->body = xmalloc(req->body_len + 1);
    memcpy(req->body, raw.data + header_end, req->body_len);
    req->body[req->body_len] = '\0';
    sb_free(&raw);
    return 0;
}

/* ---- response ------------------------------------------------------------ */

static void send_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (n == 0) return;
        off += (size_t)n;
    }
}

static void write_response(int fd, const HttpResponse *res) {
    StrBuf out;
    sb_init(&out);
    sb_addf(&out, "HTTP/1.1 %d %s\r\n", res->status, http_status_text(res->status));
    sb_addf(&out, "Content-Type: %s\r\n",
            res->content_type[0] ? res->content_type : "application/json; charset=utf-8");
    sb_addf(&out, "Content-Length: %zu\r\n", res->body.len);
    sb_adds(&out, "Connection: close\r\n");
    sb_adds(&out, "Cache-Control: no-store\r\n");
    sb_adds(&out, "X-Content-Type-Options: nosniff\r\n");
    if (res->filename[0])
        sb_addf(&out, "Content-Disposition: attachment; filename=\"%s\"\r\n", res->filename);
    sb_adds(&out, "\r\n");
    if (res->status != 204) sb_add(&out, res->body.data, res->body.len);
    send_all(fd, out.data, out.len);
    sb_free(&out);
}

/* ---- server -------------------------------------------------------------- */

static int listen_socket(const char *host, int port, int *out_port, char *err, size_t errsz) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        set_err(err, errsz, strerror(errno));
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        set_err(err, errsz, "invalid bind address");
        close(fd);
        return -1;
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        snprintf(err, errsz, "cannot bind %s:%d: %s", host, port, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 16) != 0) {
        set_err(err, errsz, strerror(errno));
        close(fd);
        return -1;
    }
    socklen_t alen = sizeof addr;
    if (getsockname(fd, (struct sockaddr *)&addr, &alen) == 0 && out_port)
        *out_port = ntohs(addr.sin_port);
    return fd;
}

static void handle_connection(int fd, HttpHandler handler, void *ud) {
    struct timeval tv = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    HttpRequest req;
    memset(&req, 0, sizeof req);
    int status = read_request(fd, &req);
    HttpResponse res;
    memset(&res, 0, sizeof res);
    sb_init(&res.body);

    if (status != 0) {
        res.status = status;
        snprintf(res.content_type, sizeof res.content_type, "text/plain; charset=utf-8");
        sb_addf(&res.body, "%d %s\n", status, http_status_text(status));
    } else {
        res.status = 200;
        snprintf(res.content_type, sizeof res.content_type, "application/json; charset=utf-8");
        handler(&req, &res, ud);
    }
    if (res.status == 204) sb_reset(&res.body);
    write_response(fd, &res);

    sb_free(&res.body);
    free(req.body);
}

int http_serve(const HttpConfig *cfg, int *out_port, char *err, size_t errsz) {
    g_stop = 0;
    int lfd = listen_socket(cfg->host, cfg->port, out_port, err, errsz);
    if (lfd < 0) return -1;

    signal(SIGPIPE, SIG_IGN);
    if (cfg->on_ready) cfg->on_ready(out_port ? *out_port : cfg->port, cfg->ready_ud);

    while (!g_stop) {
        struct pollfd pfd = {lfd, POLLIN, 0};
        int rc = poll(&pfd, 1, 300);
        if (rc < 0) {
            if (errno == EINTR) continue;
            snprintf(err, errsz, "poll: %s", strerror(errno));
            close(lfd);
            return -1;
        }
        if (rc == 0) continue;

        struct sockaddr_in peer;
        socklen_t plen = sizeof peer;
        int cfd = accept(lfd, (struct sockaddr *)&peer, &plen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            continue;
        }
        handle_connection(cfd, cfg->handler, cfg->handler_ud);
        close(cfd);
    }

    close(lfd);
    return 0;
}
