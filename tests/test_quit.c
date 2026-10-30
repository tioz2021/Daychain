#include "tests.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/model.h"
#include "core/store.h"
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

    CHECK(!datetime_parse_epoch("2026-10-30T25:00:00Z", &e));
    CHECK(!datetime_parse_epoch("30.10.2026 12:00", &e));
    CHECK(!datetime_parse_epoch("", &e));

    char iso[32];
    iso_from_epoch((int64_t)day * 86400, iso);
    int64_t back = 0;
    CHECK(datetime_parse_epoch(iso, &back));
    CHECK(back == (int64_t)day * 86400);
}

static void test_reset(void) {
    Quit q;
    memset(&q, 0, sizeof q);
    q.name = xstrdup("Not smoking");
    q.description = xstrdup("");
    q.color = xstrdup("#ef4444");
    q.icon = xstrdup("no-smoke");
    q.started_at = 1000;
    iso_from_epoch(q.started_at, q.started_iso);
    q.attempts = 1;

    CHECK(quit_reset(&q, 1500));
    CHECK(q.best_seconds == 500);
    CHECK(q.attempts == 2);
    CHECK(q.started_at == 1500);

    /* the same moment or earlier cannot start a new attempt */
    CHECK(!quit_reset(&q, 1500));
    CHECK(!quit_reset(&q, 1200));

    /* a shorter attempt does not lower the record */
    CHECK(quit_reset(&q, 2000));
    CHECK(q.best_seconds == 500);
    CHECK(q.attempts == 3);
    CHECK(q.started_at == 2000);

    quit_free(&q);
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

static void test_validation(void) {
    Quit q;
    memset(&q, 0, sizeof q);
    q.name = xstrdup("ok");
    q.description = xstrdup("");
    q.color = xstrdup("#ef4444");
    q.icon = xstrdup("shield");
    q.started_at = now_epoch();
    q.attempts = 1;

    const char *code = NULL, *msg = NULL;
    CHECK(quit_validate(&q, now_epoch(), &code, &msg));

    q.started_at = now_epoch() + 3600;
    CHECK(!quit_validate(&q, now_epoch(), &code, &msg));

    q.started_at = now_epoch();
    free(q.color);
    q.color = xstrdup("red");
    CHECK(!quit_validate(&q, now_epoch(), &code, &msg));

    quit_free(&q);
}

void test_quit(void) {
    test_datetime();
    test_reset();
    test_store_roundtrip();
    test_validation();
}
