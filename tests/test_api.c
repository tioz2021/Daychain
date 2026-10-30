#include "tests.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "api/api.h"
#include "core/store.h"
#include "infra/json.h"
#include "infra/util.h"

static char *tmpdir(void) {
    char tmpl[] = "/tmp/habits-api-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) abort();
    return xstrdup(dir);
}

static void cleanup(const char *dir) {
    char *files[] = {"habits.jsonl", "habits.jsonl.bak"};
    for (size_t i = 0; i < 2; i++) {
        char *p = path_join(dir, files[i]);
        unlink(p);
        free(p);
    }
    char *locks = path_join(dir, "locks");
    char *lock = path_join(locks, "habits.lock");
    unlink(lock);
    free(lock);
    rmdir(locks);
    free(locks);
    rmdir(dir);
}

static int call(Server *s, const char *method, const char *path, const char *body,
                HttpResponse *res, const char *host, const char *xhr) {
    HttpRequest req;
    memset(&req, 0, sizeof req);
    snprintf(req.method, sizeof req.method, "%s", method);
    snprintf(req.path, sizeof req.path, "%s", path);
    char *q = strchr(req.path, '?');
    if (q) {
        *q = '\0';
        snprintf(req.query, sizeof req.query, "%s", q + 1);
    }
    snprintf(req.host, sizeof req.host, "%s", host ? host : "127.0.0.1:8787");
    if (xhr) snprintf(req.requested_with, sizeof req.requested_with, "%s", xhr);
    if (body) {
        req.body = xstrdup(body);
        req.body_len = strlen(body);
    }
    memset(res, 0, sizeof *res);
    sb_init(&res->body);
    api_handle(&req, res, s);
    free(req.body);
    return res->status;
}

static JVal *parse_res(HttpResponse *res) {
    char err[128];
    JVal *v = json_parse(res->body.data ? res->body.data : "", res->body.len, err, sizeof err);
    if (!v) fprintf(stderr, "    response is not JSON: %s\n", err);
    return v;
}

static void free_res(HttpResponse *res) {
    sb_free(&res->body);
}

void test_api(void) {
    char *dir = tmpdir();
    char err[256] = "";
    DB db;
    CHECK(store_load(&db, dir, err, sizeof err));
    Server s = {&db, dir, NULL, "1.0.0", false};
    HttpResponse res;

    /* health */
    CHECK(call(&s, "GET", "/api/v1/health", NULL, &res, NULL, NULL) == 200);
    JVal *v = parse_res(&res);
    CHECK(v && json_get(v, "status") && strcmp(json_str(json_get(v, "status")), "ok") == 0);
    CHECK(v && json_get(v, "data_dir"));
    json_free(v);
    free_res(&res);

    /* security */
    CHECK(call(&s, "GET", "/api/v1/health", NULL, &res, "evil.example", NULL) == 403);
    free_res(&res);
    CHECK(call(&s, "POST", "/api/v1/habits", "{\"name\":\"x\"}", &res, NULL, NULL) == 403);
    free_res(&res);
    CHECK(call(&s, "POST", "/api/v1/habits", "{\"name\":\"x\"}", &res, NULL, "habits") == 201);
    long long id_ll = 0;
    v = parse_res(&res);
    CHECK(v && json_int(json_get(v, "id"), &id_ll));
    int64_t id = id_ll;
    char startbuf[11];
    date_format(date_today(), startbuf);
    CHECK(v && strcmp(json_str(json_get(v, "start_date")), startbuf) == 0);
    json_free(v);
    free_res(&res);

    /* validation */
    CHECK(call(&s, "POST", "/api/v1/habits", "{\"name\":\"\"}", &res, NULL, "habits") == 400);
    v = parse_res(&res);
    CHECK(v && strcmp(json_str(json_get(json_get(v, "error"), "code")), "invalid_name") == 0);
    json_free(v);
    free_res(&res);

    CHECK(call(&s, "POST", "/api/v1/habits",
               "{\"name\":\"Run\",\"schedule\":{\"kind\":\"weekdays\",\"days\":[8]}}", &res,
               NULL, "habits") == 400);
    free_res(&res);

    /* entry upsert on today */
    char today[11];
    date_format(date_today(), today);
    char path[256];
    snprintf(path, sizeof path, "/api/v1/habits/%lld/entries/%s", (long long)id, today);
    CHECK(call(&s, "PUT", path, "{\"status\":\"done\",\"note\":\"ok\"}", &res, NULL, "habits") == 200);
    v = parse_res(&res);
    CHECK(v && strcmp(json_str(json_get(v, "status")), "done") == 0);
    json_free(v);
    free_res(&res);

    /* future dates are refused */
    char tomorrow[11];
    date_format(date_today() + 1, tomorrow);
    snprintf(path, sizeof path, "/api/v1/habits/%lld/entries/%s", (long long)id, tomorrow);
    CHECK(call(&s, "PUT", path, "{\"status\":\"done\"}", &res, NULL, "habits") == 400);
    free_res(&res);

    /* today + stats */
    CHECK(call(&s, "GET", "/api/v1/today", NULL, &res, NULL, NULL) == 200);
    v = parse_res(&res);
    CHECK(v && json_get(v, "items") && json_get(v, "items")->n == 1);
    CHECK(v && strcmp(json_str(json_get(json_get(json_get(v, "items")->items[0], "entry"),
                                            "status")),
                      "done") == 0);
    json_free(v);
    free_res(&res);

    char stats[256];
    snprintf(stats, sizeof stats, "/api/v1/stats?habit_id=%lld", (long long)id);
    CHECK(call(&s, "GET", stats, NULL, &res, NULL, NULL) == 200);
    v = parse_res(&res);
    CHECK(v && json_get(v, "heatmap") && json_get(json_get(v, "heatmap"), "cells")->n == 84);
    json_free(v);
    free_res(&res);

    /* patch */
    snprintf(path, sizeof path, "/api/v1/habits/%lld", (long long)id);
    CHECK(call(&s, "PATCH", path, "{\"name\":\"Reading\",\"color\":\"#22c55e\"}", &res, NULL,
               "habits") == 200);
    v = parse_res(&res);
    CHECK(v && strcmp(json_str(json_get(v, "name")), "Reading") == 0);
    CHECK(v && strcmp(json_str(json_get(v, "color")), "#22c55e") == 0);
    json_free(v);
    free_res(&res);

    CHECK(call(&s, "PATCH", path, "{\"color\":\"oops\"}", &res, NULL, "habits") == 400);
    free_res(&res);

    /* archive removes the habit from today */
    snprintf(path, sizeof path, "/api/v1/habits/%lld/archive", (long long)id);
    CHECK(call(&s, "POST", path, "{}", &res, NULL, "habits") == 200);
    free_res(&res);
    CHECK(call(&s, "GET", "/api/v1/today", NULL, &res, NULL, NULL) == 200);
    v = parse_res(&res);
    CHECK(v && json_get(v, "items")->n == 0);
    json_free(v);
    free_res(&res);

    /* export */
    CHECK(call(&s, "GET", "/api/v1/export", NULL, &res, NULL, NULL) == 200);
    CHECK(res.body.data && strstr(res.body.data, "\"type\":\"meta\"") != NULL);
    CHECK(res.body.data && strstr(res.body.data, "\"type\":\"habit\"") != NULL);
    CHECK(res.filename[0] != '\0');
    free_res(&res);

    /* static */
    CHECK(call(&s, "GET", "/", NULL, &res, NULL, NULL) == 200);
    CHECK(res.body.data && strstr(res.body.data, "<!doctype html>") != NULL);
    free_res(&res);
    CHECK(call(&s, "GET", "/app.js", NULL, &res, NULL, NULL) == 200);
    free_res(&res);
    CHECK(call(&s, "GET", "/nope.js", NULL, &res, NULL, NULL) == 404);
    free_res(&res);

    /* delete habit */
    snprintf(path, sizeof path, "/api/v1/habits/%lld", (long long)id);
    CHECK(call(&s, "DELETE", path, NULL, &res, NULL, "habits") == 204);
    free_res(&res);
    CHECK(call(&s, "GET", path, NULL, &res, NULL, NULL) == 404);
    free_res(&res);

    db_free(&db);
    cleanup(dir);
    free(dir);
}
