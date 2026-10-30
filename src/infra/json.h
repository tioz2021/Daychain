#ifndef HABITS_INFRA_JSON_H
#define HABITS_INFRA_JSON_H

#include <stdbool.h>
#include <stddef.h>

#include "infra/util.h"

#define JSON_MAX_DEPTH 32

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal JVal;
struct JVal {
    JType type;
    bool b;
    double num;
    char *str;
    JVal **items;
    char **keys;
    size_t n;
    size_t cap;
};

/* Parse a complete JSON document. Returns NULL and fills err on failure. */
JVal *json_parse(const char *text, size_t len, char *err, size_t errsz);
void json_free(JVal *v);

/* Object helpers. */
const JVal *json_get(const JVal *obj, const char *key);
const char *json_str(const JVal *v); /* NULL when not a string */
bool json_int(const JVal *v, long long *out);

/* ---- writer -------------------------------------------------------------- */

typedef struct {
    StrBuf *sb;
    int depth;
    bool first[JSON_MAX_DEPTH + 1];
    bool after_key;
} JsonW;

void jw_init(JsonW *w, StrBuf *sb);
void jw_obj(JsonW *w);
void jw_obj_end(JsonW *w);
void jw_arr(JsonW *w);
void jw_arr_end(JsonW *w);
void jw_key(JsonW *w, const char *key);
void jw_str(JsonW *w, const char *s);
void jw_int(JsonW *w, long long v);
void jw_bool(JsonW *w, bool v);
void jw_null(JsonW *w);
/* Insert an already serialized value (used for nested prebuilt JSON). */
void jw_raw(JsonW *w, const char *json);

#endif
