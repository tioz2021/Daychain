#include "tests.h"

#include <stdlib.h>

#include "infra/json.h"

static JVal *parse(const char *s) {
    char err[128];
    JVal *v = json_parse(s, strlen(s), err, sizeof err);
    if (!v) fprintf(stderr, "    parse error: %s\n", err);
    return v;
}

static int parse_fails(const char *s) {
    char err[128];
    JVal *v = json_parse(s, strlen(s), err, sizeof err);
    json_free(v);
    return v == NULL;
}

static void test_parse(void) {
    JVal *v = parse("{\"a\":1,\"b\":\"x\",\"c\":[true,false,null],\"d\":{}}");
    CHECK(v != NULL);
    if (!v) return;
    long long n = 0;
    CHECK(json_int(json_get(v, "a"), &n) && n == 1);
    CHECK_STR(json_str(json_get(v, "b")), "x");
    const JVal *arr = json_get(v, "c");
    CHECK(arr && arr->type == J_ARR && arr->n == 3);
    CHECK(arr && arr->items[0]->type == J_BOOL && arr->items[0]->b);
    CHECK(arr && arr->items[1]->type == J_BOOL && !arr->items[1]->b);
    CHECK(arr && arr->items[2]->type == J_NULL);
    CHECK(json_get(json_get(v, "d"), "zzz") == NULL);
    CHECK(json_get(v, "missing") == NULL);
    json_free(v);

    v = parse("  \" hi \\u00e9 \\ud83d\\ude00 \"  ");
    CHECK(v && v->type == J_STR);
    if (v) {
        CHECK_STR(v->str, " hi \xc3\xa9 \xf0\x9f\x98\x80 ");
        json_free(v);
    }

    v = parse("{\"k\":1,\"k\":2}");
    CHECK(v != NULL);
    if (v) {
        CHECK(v->n == 1);
        CHECK(json_int(json_get(v, "k"), &n) && n == 2);
        json_free(v);
    }

    v = parse("-12.5e2");
    CHECK(v && v->type == J_NUM && v->num == -1250.0);
    json_free(v);

    v = parse("[]");
    CHECK(v && v->type == J_ARR && v->n == 0);
    json_free(v);

    v = parse("{}");
    CHECK(v && v->type == J_OBJ && v->n == 0);
    json_free(v);
}

static void test_errors(void) {
    const char *bad[] = {
        "",
        "{",
        "{\"a\":}",
        "{\"a\" 1}",
        "{\"a\":1,}",
        "[1,]",
        "\"unterminated",
        "\"bad \\q escape\"",
        "01",
        "1.",
        ".5",
        "nul",
        "truex",
        "{} extra",
        "\"raw\ncontrol\"",
        "\"\\ud800\"",
        "\"\\udc00\"",
        "\"\\ud800\\u0041\"",
        "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int failed = parse_fails(bad[i]);
        if (!failed) fprintf(stderr, "    case %zu should fail: %s\n", i, bad[i]);
        CHECK(failed);
    }
}

static void test_writer(void) {
    StrBuf sb;
    sb_init(&sb);
    JsonW w;
    jw_init(&w, &sb);
    jw_obj(&w);
    jw_key(&w, "name");
    jw_str(&w, "привет \"x\"\n\t");
    jw_key(&w, "n");
    jw_int(&w, -7);
    jw_key(&w, "arr");
    jw_arr(&w);
    jw_bool(&w, true);
    jw_null(&w);
    jw_obj(&w);
    jw_key(&w, "inner");
    jw_str(&w, NULL);
    jw_obj_end(&w);
    jw_arr_end(&w);
    jw_obj_end(&w);

    JVal *v = parse(sb.data);
    CHECK(v != NULL);
    if (v) {
        CHECK_STR(json_str(json_get(v, "name")), "привет \"x\"\n\t");
        long long n = 0;
        CHECK(json_int(json_get(v, "n"), &n) && n == -7);
        const JVal *arr = json_get(v, "arr");
        CHECK(arr && arr->n == 3);
        CHECK(arr && arr->items[2]->type == J_OBJ);
        CHECK(arr && json_get(arr->items[2], "inner")->type == J_NULL);
        json_free(v);
    }

    /* Escaped control characters survive a round trip. */
    sb_reset(&sb);
    jw_init(&w, &sb);
    jw_str(&w, "\x01\x1f");
    v = parse(sb.data);
    CHECK(v != NULL);
    if (v) {
        CHECK_STR(v->str, "\x01\x1f");
        json_free(v);
    }
    sb_free(&sb);
}

void test_json(void) {
    test_parse();
    test_errors();
    test_writer();
}
