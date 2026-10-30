#ifndef HABITS_INFRA_UTIL_H
#define HABITS_INFRA_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- dynamic string buffer ---------------------------------------------- */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_reset(StrBuf *sb);
void sb_add(StrBuf *sb, const char *s, size_t n);
void sb_adds(StrBuf *sb, const char *s);
void sb_addc(StrBuf *sb, char c);
void sb_addf(StrBuf *sb, const char *fmt, ...);
/* Detach the buffer as a NUL-terminated string owned by the caller. */
char *sb_take(StrBuf *sb);

/* ---- allocation (abort on OOM) ------------------------------------------ */

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* ---- UTF-8 --------------------------------------------------------------- */

bool utf8_valid(const char *s, size_t n);
size_t utf8_chars(const char *s);
/* Cut the string in place so it holds at most max_chars code points. */
void utf8_trunc_chars(char *s, size_t max_chars);

/* ---- civil dates (days since 1970-01-01, proleptic Gregorian) ------------ */

/* Strict YYYY-MM-DD parse; returns false on bad format or impossible date. */
bool date_parse(const char *s, int32_t *out_days);
void date_format(int32_t days, char out[11]);
bool date_valid(const char *s);
/* ISO weekday: Monday = 1 ... Sunday = 7. */
int date_iso_weekday(int32_t days);
/* Monday of the ISO week containing days. */
int32_t date_week_start(int32_t days);
int32_t date_today(void);

/* Local time, e.g. "2026-10-30T19:45:00+02:00". Buffer must hold 32 bytes. */
void now_iso8601(char out[32]);
void iso_from_epoch(int64_t epoch, char out[32]);
bool datetime_valid(const char *s);
/* Parse "YYYY-MM-DDTHH:MM:SS(+HH:MM|Z)" into seconds since the Unix epoch. */
bool datetime_parse_epoch(const char *s, int64_t *out);
int64_t now_epoch(void);

/* ---- files --------------------------------------------------------------- */

int mkdir_p(const char *path);
bool file_read_all(const char *path, char **out, size_t *out_len);
bool file_write_atomic(const char *path, const char *data, size_t len);
bool file_copy(const char *src, const char *dst);
bool dir_writable(const char *path);
char *path_join(const char *dir, const char *name);
/* Absolute path of the running executable, or NULL. */
char *exe_dir(void);

#endif
