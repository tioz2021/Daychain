#include "infra/util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ---- StrBuf -------------------------------------------------------------- */

void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb_init(sb);
}

void sb_reset(StrBuf *sb) {
    sb->len = 0;
}

static void sb_reserve(StrBuf *sb, size_t extra) {
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return;
    size_t cap = sb->cap ? sb->cap : 64;
    while (cap < need) cap *= 2;
    sb->data = xrealloc(sb->data, cap);
    sb->cap = cap;
}

void sb_add(StrBuf *sb, const char *s, size_t n) {
    if (n == 0) return;
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_adds(StrBuf *sb, const char *s) {
    if (s) sb_add(sb, s, strlen(s));
}

void sb_addc(StrBuf *sb, char c) {
    sb_add(sb, &c, 1);
}

void sb_addf(StrBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[512];
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) {
        sb_add(sb, tmp, (size_t)n);
        return;
    }
    sb_reserve(sb, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sb->len += (size_t)n;
}

char *sb_take(StrBuf *sb) {
    if (!sb->data) {
        char *empty = xmalloc(1);
        empty[0] = '\0';
        return empty;
    }
    char *out = sb->data;
    sb_init(sb);
    return out;
}

/* ---- allocation ---------------------------------------------------------- */

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "habits: out of memory\n");
        exit(70);
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "habits: out of memory\n");
        exit(70);
    }
    return q;
}

char *xstrdup(const char *s) {
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ---- UTF-8 --------------------------------------------------------------- */

bool utf8_valid(const char *s, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    const unsigned char *e = p + n;
    while (p < e) {
        unsigned c = *p++;
        if (c < 0x80) continue;
        int extra;
        uint32_t cp;
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if ((size_t)(e - p) < (size_t)extra) return false;
        for (int i = 0; i < extra; i++) {
            if ((p[i] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i] & 0x3Fu);
        }
        p += extra;
        if (extra == 1 && cp < 0x80) return false;
        if (extra == 2 && cp < 0x800) return false;
        if (extra == 3 && cp < 0x10000) return false;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    }
    return true;
}

size_t utf8_chars(const char *s) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if ((*p & 0xC0) != 0x80) n++;
    return n;
}

void utf8_trunc_chars(char *s, size_t max_chars) {
    size_t n = 0;
    for (char *p = s; *p;) {
        unsigned char c = (unsigned char)*p;
        size_t step = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        if (n == max_chars) {
            *p = '\0';
            return;
        }
        p += step;
        n++;
    }
}

/* ---- civil dates --------------------------------------------------------- */

static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? (unsigned)-3 : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned dd = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned mm = mp + (mp < 10u ? 3u : (unsigned)-9);
    *y = (int)(yy + (mm <= 2));
    *m = mm;
    *d = dd;
}

static bool is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int month_len(int y, int m) {
    static const int ml[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    if (m == 2 && is_leap(y)) return 29;
    return ml[m - 1];
}

bool date_parse(const char *s, int32_t *out_days) {
    if (!s || strlen(s) != 10) return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            if (s[i] != '-') return false;
        } else if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    int m = (s[5] - '0') * 10 + (s[6] - '0');
    int d = (s[8] - '0') * 10 + (s[9] - '0');
    if (y < 1900 || y > 9999) return false;
    if (m < 1 || m > 12) return false;
    if (d < 1 || d > month_len(y, m)) return false;
    if (out_days) *out_days = (int32_t)days_from_civil(y, (unsigned)m, (unsigned)d);
    return true;
}

void date_format(int32_t days, char out[11]) {
    int y;
    unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    char tmp[32];
    int n = snprintf(tmp, sizeof tmp, "%04d-%02u-%02u", y, m, d);
    if (n != 10)
        memcpy(out, "0000-00-00", 11);
    else
        memcpy(out, tmp, 11);
}

bool date_valid(const char *s) {
    return date_parse(s, NULL);
}

int date_iso_weekday(int32_t days) {
    int r = (int)((days + 3) % 7);
    if (r < 0) r += 7;
    return r + 1;
}

int32_t date_week_start(int32_t days) {
    return days - (date_iso_weekday(days) - 1);
}

int32_t date_today(void) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    return (int32_t)days_from_civil(tm.tm_year + 1900, (unsigned)(tm.tm_mon + 1),
                                    (unsigned)tm.tm_mday);
}

void iso_from_epoch(int64_t epoch, char out[32]) {
    time_t t = (time_t)epoch;
    struct tm tm;
    if (!localtime_r(&t, &tm)) {
        snprintf(out, 32, "1970-01-01T00:00:00+00:00");
        return;
    }
    char base[20];
    char zone[8] = "+00:00";
    strftime(base, sizeof base, "%Y-%m-%dT%H:%M:%S", &tm);
    char raw[8] = "";
    if (strftime(raw, sizeof raw, "%z", &tm) > 0 && strlen(raw) >= 5)
        snprintf(zone, sizeof zone, "%.3s:%.2s", raw, raw + 3);
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%s%s", base, zone);
    size_t len = strlen(tmp);
    if (len > 31) len = 31;
    memcpy(out, tmp, len);
    out[len] = '\0';
}

void now_iso8601(char out[32]) {
    iso_from_epoch((int64_t)time(NULL), out);
}

int64_t now_epoch(void) {
    return (int64_t)time(NULL);
}

bool datetime_parse_epoch(const char *s, int64_t *out) {
    if (!s || !datetime_valid(s)) return false;
    int32_t day = 0;
    char date[11];
    memcpy(date, s, 10);
    date[10] = '\0';
    if (!date_parse(date, &day)) return false;
    int hh = (s[11] - '0') * 10 + (s[12] - '0');
    int mi = (s[14] - '0') * 10 + (s[15] - '0');
    int ss = (s[17] - '0') * 10 + (s[18] - '0');
    if (hh > 23 || mi > 59 || ss > 60) return false;

    size_t i = 19;
    if (s[i] == '.') { /* fractional seconds are accepted and ignored */
        i++;
        while (s[i] >= '0' && s[i] <= '9') i++;
    }
    int64_t offset = 0;
    if (s[i] == '+' || s[i] == '-') {
        char sign = s[i];
        if (!s[i + 1] || !s[i + 2] || s[i + 3] != ':' || !s[i + 4] || !s[i + 5]) return false;
        int oh = (s[i + 1] - '0') * 10 + (s[i + 2] - '0');
        int om = (s[i + 4] - '0') * 10 + (s[i + 5] - '0');
        if (oh > 23 || om > 59) return false;
        offset = (int64_t)(oh * 3600 + om * 60);
        if (sign == '-') offset = -offset;
    } else if (s[i] != 'Z') {
        return false;
    }
    *out = (int64_t)day * 86400 + hh * 3600 + mi * 60 + ss - offset;
    return true;
}

bool datetime_valid(const char *s) {
    if (!s) return false;
    size_t n = strlen(s);
    if (n < 19 || n > 40) return false;
    for (int i = 0; i < 19; i++) {
        if (i == 4 || i == 7) {
            if (s[i] != '-') return false;
        } else if (i == 10) {
            if (s[i] != 'T' && s[i] != ' ') return false;
        } else if (i == 13 || i == 16) {
            if (s[i] != ':') return false;
        } else if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

/* ---- files --------------------------------------------------------------- */

int mkdir_p(const char *path) {
    char *tmp = xstrdup(path);
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            free(tmp);
            return -1;
        }
        *p = '/';
    }
    int rc = 0;
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) rc = -1;
    free(tmp);
    return rc;
}

bool file_read_all(const char *path, char **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    StrBuf sb;
    sb_init(&sb);
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) sb_add(&sb, buf, n);
    bool ok = !ferror(f);
    fclose(f);
    if (!ok) {
        sb_free(&sb);
        return false;
    }
    *out_len = sb.len;
    *out = sb_take(&sb);
    return true;
}

bool file_write_atomic(const char *path, const char *data, size_t len) {
    char tmp[4096];
    if (snprintf(tmp, sizeof tmp, "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof tmp)
        return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    if (ok && fflush(f) != 0) ok = false;
    if (ok && fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        unlink(tmp);
        return false;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return false;
    }
    return true;
}

bool file_copy(const char *src, const char *dst) {
    char *data = NULL;
    size_t len = 0;
    if (!file_read_all(src, &data, &len)) return false;
    bool ok = file_write_atomic(dst, data, len);
    free(data);
    return ok;
}

bool dir_writable(const char *path) {
    char *probe = path_join(path, ".write-probe");
    FILE *f = fopen(probe, "wb");
    if (!f) {
        free(probe);
        return false;
    }
    fputs("ok", f);
    fclose(f);
    unlink(probe);
    free(probe);
    return true;
}

char *path_join(const char *dir, const char *name) {
    size_t a = strlen(dir), b = strlen(name);
    char *p = xmalloc(a + b + 2);
    memcpy(p, dir, a);
    if (a && p[a - 1] != '/') p[a++] = '/';
    memcpy(p + a, name, b);
    p[a + b] = '\0';
    return p;
}

char *exe_dir(void) {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return NULL;
    buf[n] = '\0';
    char *slash = strrchr(buf, '/');
    if (!slash) return NULL;
    *slash = '\0';
    return xstrdup(buf);
}
