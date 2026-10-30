#include "infra/json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- parser -------------------------------------------------------------- */

typedef struct {
    const char *p;
    const char *end;
    char *err;
    size_t errsz;
    int depth;
    bool failed;
} P;

static void fail(P *ps, const char *msg) {
    if (!ps->failed) {
        ps->failed = true;
        if (ps->err && ps->errsz) snprintf(ps->err, ps->errsz, "%s", msg);
    }
}

static void skip_ws(P *ps) {
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            ps->p++;
        else
            break;
    }
}

static JVal *new_val(JType t) {
    JVal *v = xmalloc(sizeof *v);
    memset(v, 0, sizeof *v);
    v->type = t;
    return v;
}

static void push_item(JVal *v, char *key, JVal *item) {
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->items = xrealloc(v->items, v->cap * sizeof *v->items);
        if (v->type == J_OBJ) v->keys = xrealloc(v->keys, v->cap * sizeof *v->keys);
    }
    v->items[v->n] = item;
    if (v->type == J_OBJ) v->keys[v->n] = key;
    v->n++;
}

static int hex4(P *ps) {
    if (ps->end - ps->p < 4) {
        fail(ps, "truncated \\u escape");
        return -1;
    }
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = *ps->p++;
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= c - '0';
        else if (c >= 'a' && c <= 'f')
            v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            v |= c - 'A' + 10;
        else {
            fail(ps, "bad hex digit in \\u escape");
            return -1;
        }
    }
    return v;
}

static void put_utf8(StrBuf *sb, unsigned cp) {
    char b[4];
    if (cp < 0x80) {
        b[0] = (char)cp;
        sb_add(sb, b, 1);
    } else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
        sb_add(sb, b, 2);
    } else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        sb_add(sb, b, 3);
    } else {
        b[0] = (char)(0xF0 | (cp >> 18));
        b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[3] = (char)(0x80 | (cp & 0x3F));
        sb_add(sb, b, 4);
    }
}

static JVal *p_string(P *ps) {
    /* ps->p points at the opening quote */
    ps->p++;
    StrBuf sb;
    sb_init(&sb);
    while (ps->p < ps->end) {
        unsigned char c = (unsigned char)*ps->p++;
        if (c == '"') {
            JVal *v = new_val(J_STR);
            v->str = sb_take(&sb);
            return v;
        }
        if (c < 0x20) {
            fail(ps, "raw control character in string");
            break;
        }
        if (c != '\\') {
            sb_add(&sb, (const char *)&c, 1);
            continue;
        }
        if (ps->p >= ps->end) {
            fail(ps, "truncated escape");
            break;
        }
        char e = *ps->p++;
        switch (e) {
            case '"': sb_addc(&sb, '"'); break;
            case '\\': sb_addc(&sb, '\\'); break;
            case '/': sb_addc(&sb, '/'); break;
            case 'b': sb_addc(&sb, '\b'); break;
            case 'f': sb_addc(&sb, '\f'); break;
            case 'n': sb_addc(&sb, '\n'); break;
            case 'r': sb_addc(&sb, '\r'); break;
            case 't': sb_addc(&sb, '\t'); break;
            case 'u': {
                int cp = hex4(ps);
                if (cp < 0) break;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (ps->end - ps->p < 6 || ps->p[0] != '\\' || ps->p[1] != 'u') {
                        fail(ps, "lone high surrogate");
                        break;
                    }
                    ps->p += 2;
                    int lo = hex4(ps);
                    if (lo < 0) break;
                    if (lo < 0xDC00 || lo > 0xDFFF) {
                        fail(ps, "invalid low surrogate");
                        break;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    fail(ps, "lone low surrogate");
                    break;
                }
                put_utf8(&sb, (unsigned)cp);
                break;
            }
            default:
                fail(ps, "unknown escape");
                break;
        }
        if (ps->failed) break;
    }
    sb_free(&sb);
    fail(ps, "unterminated string");
    return NULL;
}

static JVal *p_number(P *ps) {
    const char *start = ps->p;
    if (ps->p < ps->end && *ps->p == '-') ps->p++;
    if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9') {
        fail(ps, "bad number");
        return NULL;
    }
    if (*ps->p == '0' && ps->p + 1 < ps->end && ps->p[1] >= '0' && ps->p[1] <= '9') {
        fail(ps, "leading zero in number");
        return NULL;
    }
    while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    if (ps->p < ps->end && *ps->p == '.') {
        ps->p++;
        if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9') {
            fail(ps, "bad number fraction");
            return NULL;
        }
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    }
    if (ps->p < ps->end && (*ps->p == 'e' || *ps->p == 'E')) {
        ps->p++;
        if (ps->p < ps->end && (*ps->p == '+' || *ps->p == '-')) ps->p++;
        if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9') {
            fail(ps, "bad number exponent");
            return NULL;
        }
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    }
    size_t n = (size_t)(ps->p - start);
    if (n >= 64) {
        fail(ps, "number too long");
        return NULL;
    }
    char buf[64];
    memcpy(buf, start, n);
    buf[n] = '\0';
    char *endp = NULL;
    double d = strtod(buf, &endp);
    if (!endp || *endp != '\0' || !isfinite(d)) {
        fail(ps, "bad number");
        return NULL;
    }
    JVal *v = new_val(J_NUM);
    v->num = d;
    return v;
}

static JVal *p_value(P *ps) {
    skip_ws(ps);
    if (ps->p >= ps->end) {
        fail(ps, "unexpected end of input");
        return NULL;
    }
    char c = *ps->p;
    switch (c) {
        case 'n':
            if (ps->end - ps->p >= 4 && memcmp(ps->p, "null", 4) == 0) {
                ps->p += 4;
                return new_val(J_NULL);
            }
            break;
        case 't':
            if (ps->end - ps->p >= 4 && memcmp(ps->p, "true", 4) == 0) {
                ps->p += 4;
                JVal *v = new_val(J_BOOL);
                v->b = true;
                return v;
            }
            break;
        case 'f':
            if (ps->end - ps->p >= 5 && memcmp(ps->p, "false", 5) == 0) {
                ps->p += 5;
                JVal *v = new_val(J_BOOL);
                v->b = false;
                return v;
            }
            break;
        case '"':
            return p_string(ps);
        case '[': {
            if (ps->depth >= JSON_MAX_DEPTH) {
                fail(ps, "nesting too deep");
                return NULL;
            }
            ps->p++;
            ps->depth++;
            JVal *v = new_val(J_ARR);
            skip_ws(ps);
            if (ps->p < ps->end && *ps->p == ']') {
                ps->p++;
                ps->depth--;
                return v;
            }
            for (;;) {
                JVal *item = p_value(ps);
                if (!item) {
                    json_free(v);
                    return NULL;
                }
                push_item(v, NULL, item);
                skip_ws(ps);
                if (ps->p < ps->end && *ps->p == ',') {
                    ps->p++;
                    continue;
                }
                if (ps->p < ps->end && *ps->p == ']') {
                    ps->p++;
                    ps->depth--;
                    return v;
                }
                json_free(v);
                fail(ps, "expected ',' or ']'");
                return NULL;
            }
        }
        case '{': {
            if (ps->depth >= JSON_MAX_DEPTH) {
                fail(ps, "nesting too deep");
                return NULL;
            }
            ps->p++;
            ps->depth++;
            JVal *v = new_val(J_OBJ);
            skip_ws(ps);
            if (ps->p < ps->end && *ps->p == '}') {
                ps->p++;
                ps->depth--;
                return v;
            }
            for (;;) {
                skip_ws(ps);
                if (ps->p >= ps->end || *ps->p != '"') {
                    json_free(v);
                    fail(ps, "expected object key string");
                    return NULL;
                }
                JVal *k = p_string(ps);
                if (!k) {
                    json_free(v);
                    return NULL;
                }
                skip_ws(ps);
                if (ps->p >= ps->end || *ps->p != ':') {
                    json_free(k);
                    json_free(v);
                    fail(ps, "expected ':'");
                    return NULL;
                }
                ps->p++;
                JVal *item = p_value(ps);
                if (!item) {
                    json_free(k);
                    json_free(v);
                    return NULL;
                }
                /* Duplicate keys: last one wins. */
                for (size_t i = 0; i < v->n; i++) {
                    if (strcmp(v->keys[i], k->str) == 0) {
                        json_free(v->items[i]);
                        free(v->keys[i]);
                        v->items[i] = item;
                        v->keys[i] = k->str;
                        free(k);
                        goto next_member;
                    }
                }
                push_item(v, k->str, item);
                free(k);
            next_member:
                skip_ws(ps);
                if (ps->p < ps->end && *ps->p == ',') {
                    ps->p++;
                    continue;
                }
                if (ps->p < ps->end && *ps->p == '}') {
                    ps->p++;
                    ps->depth--;
                    return v;
                }
                json_free(v);
                fail(ps, "expected ',' or '}'");
                return NULL;
            }
        }
        default:
            return p_number(ps);
    }
    fail(ps, "invalid literal");
    return NULL;
}

JVal *json_parse(const char *text, size_t len, char *err, size_t errsz) {
    P ps = {text, text + len, err, errsz, 0, false};
    if (err && errsz) err[0] = '\0';
    JVal *v = p_value(&ps);
    if (!v) return NULL;
    skip_ws(&ps);
    if (ps.p != ps.end) {
        json_free(v);
        fail(&ps, "trailing garbage after JSON value");
        return NULL;
    }
    return v;
}

void json_free(JVal *v) {
    if (!v) return;
    switch (v->type) {
        case J_STR:
            free(v->str);
            break;
        case J_ARR:
        case J_OBJ:
            for (size_t i = 0; i < v->n; i++) {
                json_free(v->items[i]);
                if (v->type == J_OBJ) free(v->keys[i]);
            }
            free(v->items);
            free(v->keys);
            break;
        default:
            break;
    }
    free(v);
}

const JVal *json_get(const JVal *obj, const char *key) {
    if (!obj || obj->type != J_OBJ) return NULL;
    for (size_t i = 0; i < obj->n; i++)
        if (strcmp(obj->keys[i], key) == 0) return obj->items[i];
    return NULL;
}

const char *json_str(const JVal *v) {
    return (v && v->type == J_STR) ? v->str : NULL;
}

bool json_int(const JVal *v, long long *out) {
    if (!v || v->type != J_NUM) return false;
    double d = v->num;
    if (!isfinite(d) || d != (double)(long long)d) return false;
    if (d < -9007199254740992.0 || d > 9007199254740992.0) return false;
    *out = (long long)d;
    return true;
}

/* ---- writer -------------------------------------------------------------- */

void jw_init(JsonW *w, StrBuf *sb) {
    w->sb = sb;
    w->depth = 0;
    w->first[0] = true;
    w->after_key = false;
}

static void sep(JsonW *w) {
    if (w->after_key) {
        w->after_key = false;
        return;
    }
    if (w->depth < 0) return;
    if (w->first[w->depth])
        w->first[w->depth] = false;
    else
        sb_addc(w->sb, ',');
}

void jw_obj(JsonW *w) {
    sep(w);
    sb_addc(w->sb, '{');
    if (w->depth < JSON_MAX_DEPTH) {
        w->depth++;
        w->first[w->depth] = true;
    }
}

void jw_obj_end(JsonW *w) {
    sb_addc(w->sb, '}');
    if (w->depth > 0) w->depth--;
}

void jw_arr(JsonW *w) {
    sep(w);
    sb_addc(w->sb, '[');
    if (w->depth < JSON_MAX_DEPTH) {
        w->depth++;
        w->first[w->depth] = true;
    }
}

void jw_arr_end(JsonW *w) {
    sb_addc(w->sb, ']');
    if (w->depth > 0) w->depth--;
}

static void write_escaped(StrBuf *sb, const char *s) {
    sb_addc(sb, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"': sb_adds(sb, "\\\""); break;
            case '\\': sb_adds(sb, "\\\\"); break;
            case '\n': sb_adds(sb, "\\n"); break;
            case '\r': sb_adds(sb, "\\r"); break;
            case '\t': sb_adds(sb, "\\t"); break;
            case '\b': sb_adds(sb, "\\b"); break;
            case '\f': sb_adds(sb, "\\f"); break;
            default:
                if (*p < 0x20)
                    sb_addf(sb, "\\u%04x", *p);
                else
                    sb_addc(sb, (char)*p);
        }
    }
    sb_addc(sb, '"');
}

void jw_key(JsonW *w, const char *key) {
    sep(w);
    write_escaped(w->sb, key);
    sb_addc(w->sb, ':');
    w->after_key = true;
}

void jw_str(JsonW *w, const char *s) {
    sep(w);
    if (s)
        write_escaped(w->sb, s);
    else
        sb_adds(w->sb, "null");
}

void jw_int(JsonW *w, long long v) {
    sep(w);
    sb_addf(w->sb, "%lld", v);
}

void jw_bool(JsonW *w, bool v) {
    sep(w);
    sb_adds(w->sb, v ? "true" : "false");
}

void jw_null(JsonW *w) {
    sep(w);
    sb_adds(w->sb, "null");
}

void jw_raw(JsonW *w, const char *json) {
    sep(w);
    sb_adds(w->sb, json ? json : "null");
}
