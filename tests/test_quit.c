#include "tests.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "api/api.h"
#include "core/store.h"
#include "infra/json.h"
#include "infra/util.h"

static char *tmpdir(void) {
    char tmpl[] = "/tmp/habits-quit-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) abort();
    return xstrdup(dir);
}

static void cleanup(const char *dir) {
    const char *files[] = {"habits.jsonl", "habits.jsonl.bak"};
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
                HttpResponse *res) {
    HttpRequest req;
    memset(&req, 0, sizeof req);
    snprintf(req.method, sizeof req.method, "%s", method);
    snprintf(req.path, sizeof req.path, "%s", path);
    char *q = strchr(req.path, '?');
    if (q) {
        *q = '\0';
        snprintf(req.query, sizeof req.query, "%s", q + 1);
    }
    snprintf(req.host, sizeof req.host, "127.0.0.1:8787");
    snprintf(req.requested_with, sizeof req.requested_with, "habits");
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
    return json_parse(res->body.data ? res->body.data : "", res->body.len, err, sizeof err);
}

static void test_datetime(void) {
    int32_t day = 0;
    CHECK(date_parse("2026-10-30", &day));
    int64_t e = 0;
    CHECK(datetime_parse_epoch("2026-10-30T19:45:00+02:00", &e));
    CHECK(e == (int64_t)day * 86400 + 19 * 3600 + 45 * 60 - 2 * 3600);

    CHECK(datetime_parse_epoch("2026-10-30T17:45:00Z", &e));
    CHECK(e == (int64_t)day * 86400 + 17 * 3600 + 45 * 60);

    CHECK(datetime_parse_epoch("2026-10-30T19:45:00.123+02:00", &e));
    CHECK(e == (int64_t)day * 86400 + 19 * 3600 + 45 * 60 - 2 * 3600);
    CHECK(datetime_parse_epoch("2026-10-30T17:45:00.000Z", &e));
    CHECK(e == (int64_t)day * 86400 + 17 * 3600 + 45 * 60);

    CHECK(!datetime_parse_epoch("2026-10-30T25:00:00Z", &e));
    CHECK(!datetime_parse_epoch("30.10.2026 12:00", &e));
    CHECK(!datetime_parse_epoch("", &e));

    char iso[32];
    iso_from_epoch((int64_t)day * 86400, iso);
    int64_t back = 0;
    CHECK(datetime_parse_epoch(iso, &back));
    CHECK(back == (int64_t)day * 86400);
}

static void test_store_roundtrip(void) {
    char *dir = tmpdir();
    char err[256] = "";
    DB db;
    CHECK(store_load(&db, dir, err, sizeof err));

    Quit *q = db_quit_add(&db);
    q->name = xstrdup("Не курю");
    q->description = xstrdup("с 1 октября");
    q->color = xstrdup("#ef4444");
    q->icon = xstrdup("no-smoke");
    q->started_at = now_epoch() - 3600;
    iso_from_epoch(q->started_at, q->started_iso);
    now_iso8601(q->created_at);
    q->best_seconds = 7200;
    q->attempts = 3;
    int64_t id = q->id;
    int64_t started = q->started_at;

    CHECK(store_save(&db, dir, err, sizeof err));
    db_free(&db);
    CHECK(store_load(&db, dir, err, sizeof err));
    CHECK(db.nquits == 1);
    const Quit *lq = db_quit_by_id_c(&db, id);
    CHECK(lq != NULL);
    if (lq) {
        CHECK_STR(lq->name, "Не курю");
        CHECK_STR(lq->description, "с 1 октября");
        CHECK_STR(lq->icon, "no-smoke");
        CHECK(lq->best_seconds == 7200);
        CHECK(lq->attempts == 3);
        CHECK(lq->started_at == started);
    }
    db_free(&db);
    cleanup(dir);
    free(dir);
}

static void test_api_quits(void) {
    char *dir = tmpdir();
    char err[256] = "";
    DB db;
    CHECK(store_load(&db, dir, err, sizeof err));
    Server s = {&db, dir, NULL, "1.0.0", false};
    HttpResponse res;

    int64_t base = now_epoch() - 1000;
    char base_iso[32];
    iso_from_epoch(base, base_iso);
    char body[512];
    snprintf(body, sizeof body,
             "{\"name\":\"Не курю\",\"icon\":\"no-smoke\",\"started_at\":\"%s\"}", base_iso);
    CHECK(call(&s, "POST", "/api/v1/quits", body, &res) == 201);
    JVal *v = parse_res(&res);
    long long id = 0, attempts = 0, elapsed = 0, best = 0;
    CHECK(v && json_int(json_get(v, "id"), &id));
    CHECK(v && json_int(json_get(v, "attempts"), &attempts) && attempts == 1);
    CHECK(v && json_int(json_get(v, "elapsed_seconds"), &elapsed) && elapsed >= 995);
    CHECK(v && strcmp(json_str(json_get(v, "started_at")), base_iso) == 0);
    sb_free(&res.body);
    json_free(v);

    int64_t now = now_epoch();
    char at_iso[32];
    iso_from_epoch(now - 500, at_iso);
    char path[128];
    snprintf(path, sizeof path, "/api/v1/quits/%lld/reset", id);
    snprintf(body, sizeof body, "{\"at\":\"%s\"}", at_iso);
    CHECK(call(&s, "POST", path, body, &res) == 200);
    v = parse_res(&res);
    CHECK(v && json_int(json_get(v, "attempts"), &attempts) && attempts == 2);
    CHECK(v && json_int(json_get(v, "best_seconds"), &best) && best >= 480 && best <= 520);
    CHECK(v && json_int(json_get(v, "elapsed_seconds"), &elapsed) && elapsed >= 480);
    sb_free(&res.body);
    json_free(v);

    now = now_epoch();
    iso_from_epoch(now - 100, at_iso);
    snprintf(body, sizeof body, "{\"at\":\"%s\"}", at_iso);
    CHECK(call(&s, "POST", path, body, &res) == 200);
    v = parse_res(&res);
    CHECK(v && json_int(json_get(v, "attempts"), &attempts) && attempts == 3);
    CHECK(v && json_int(json_get(v, "best_seconds"), &best) && best >= 480 && best <= 520);
    CHECK(v && json_int(json_get(v, "elapsed_seconds"), &elapsed) && elapsed <= 120);
    sb_free(&res.body);
    json_free(v);

    CHECK(call(&s, "POST", path, "{\"at\":\"2020-01-01T00:00:00Z\"}", &res) == 400);
    sb_free(&res.body);

    CHECK(call(&s, "GET", "/api/v1/quits", NULL, &res) == 200);
    v = parse_res(&res);
    const JVal *arr = json_get(v, "quits");
    CHECK(arr && arr->n == 1);
    sb_free(&res.body);
    json_free(v);

    snprintf(path, sizeof path, "/api/v1/quits/%lld", id);
    CHECK(call(&s, "PATCH", path, "{\"name\":\"Без сигарет\"}", &res) == 200);
    v = parse_res(&res);
    CHECK(v && strcmp(json_str(json_get(v, "name")), "Без сигарет") == 0);
    sb_free(&res.body);
    json_free(v);

    CHECK(call(&s, "PATCH", path, "{\"color\":\"nope\"}", &res) == 400);
    sb_free(&res.body);

    snprintf(path, sizeof path, "/api/v1/quits/%lld/archive", id);
    CHECK(call(&s, "POST", path, "{}", &res) == 200);
    sb_free(&res.body);
    CHECK(call(&s, "GET", "/api/v1/quits", NULL, &res) == 200);
    v = parse_res(&res);
    arr = json_get(v, "quits");
    CHECK(arr && arr->n == 0);
    sb_free(&res.body);
    json_free(v);

    CHECK(call(&s, "GET", "/api/v1/quits?archived=1", NULL, &res) == 200);
    v = parse_res(&res);
    arr = json_get(v, "quits");
    CHECK(arr && arr->n == 1);
    sb_free(&res.body);
    json_free(v);

    snprintf(path, sizeof path, "/api/v1/quits/%lld", id);
    CHECK(call(&s, "DELETE", path, NULL, &res) == 204);
    sb_free(&res.body);
    CHECK(call(&s, "GET", path, NULL, &res) == 404);
    sb_free(&res.body);

    db_free(&db);
    cleanup(dir);
    free(dir);
}

void test_quit(void) {
    test_datetime();
    test_store_roundtrip();
    test_api_quits();
}
