#include "tests.h"

#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

#include "core/store.h"
#include "fixture.h"
#include "infra/util.h"

static char *make_tmpdir(void) {
    char tmpl[] = "/tmp/habits-store-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) {
        perror("mkdtemp");
        abort();
    }
    return xstrdup(dir);
}

static void remove_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char *p = path_join(dir, ent->d_name);
            unlink(p);
            free(p);
        }
        closedir(d);
    }
    rmdir(dir);
}

static void test_roundtrip(void) {
    char *dir = make_tmpdir();
    char err[256] = "";

    DB db;
    CHECK(store_load(&db, dir, err, sizeof err));
    CHECK(db.nhabits == 0);

    Habit *h = test_add_habit(&db, "Чтение", SCHED_WEEKDAYS, 0x15, 0, "2026-10-01");
    free(h->description);
    h->description = xstrdup("30 минут");
    free(h->icon);
    h->icon = xstrdup("book");
    free(h->category);
    h->category = xstrdup("развитие");
    test_add_entry(&db, h->id, "2026-10-28", ENTRY_DONE);
    test_set_entry_note(&db, h->id, "2026-10-28", "глава 7");
    test_add_entry(&db, h->id, "2026-10-29", ENTRY_SKIPPED);

    CHECK(store_save(&db, dir, err, sizeof err));
    int64_t id = h->id;
    db_free(&db);

    CHECK(store_load(&db, dir, err, sizeof err));
    CHECK(db.nhabits == 1);
    Habit *loaded = db_habit_by_id(&db, id);
    CHECK(loaded != NULL);
    if (loaded) {
        CHECK_STR(loaded->name, "Чтение");
        CHECK_STR(loaded->description, "30 минут");
        CHECK_STR(loaded->icon, "book");
        CHECK_STR(loaded->category, "развитие");
        CHECK(loaded->kind == SCHED_WEEKDAYS);
        CHECK(loaded->days_mask == 0x15);
        CHECK_STR(loaded->start_date, "2026-10-01");
        CHECK(db.nentries == 2);
        const Entry *done = db_entry_find_c(&db, id, test_day("2026-10-28"));
        CHECK(done && done->status == ENTRY_DONE);
        CHECK(done && strcmp(done->note, "глава 7") == 0);
        const Entry *skip = db_entry_find_c(&db, id, test_day("2026-10-29"));
        CHECK(skip && skip->status == ENTRY_SKIPPED);
    }
    db_free(&db);

    /* Second save keeps the previous version as .bak. */
    CHECK(store_load(&db, dir, err, sizeof err));
    db.habits[0].sort_order = 55;
    CHECK(store_save(&db, dir, err, sizeof err));
    char *bak = path_join(dir, "habits.jsonl.bak");
    char *baktext = NULL;
    size_t baklen = 0;
    CHECK(file_read_all(bak, &baktext, &baklen));
    CHECK(baktext && strstr(baktext, "\"sort_order\":10") != NULL);
    free(baktext);
    free(bak);
    db_free(&db);

    remove_dir(dir);
    free(dir);
}

static void test_tolerates_corruption(void) {
    char *dir = make_tmpdir();
    char err[256] = "";
    DB db;
    CHECK(store_load(&db, dir, err, sizeof err));
    test_add_habit(&db, "a", SCHED_DAILY, 0, 0, "2026-10-01");
    CHECK(store_save(&db, dir, err, sizeof err));
    db_free(&db);

    char *path = path_join(dir, "habits.jsonl");
    FILE *f = fopen(path, "ab");
    fputs("{\"type\":\"entry\",\"habit_id\":1,\"dat\n", f); /* truncated line */
    fclose(f);

    CHECK(store_load(&db, dir, err, sizeof err));
    CHECK(db.nhabits == 1);
    db_free(&db);

    /* The corrupt source is preserved for the user. */
    int found = 0;
    DIR *d = opendir(dir);
    struct dirent *ent;
    while (d && (ent = readdir(d)) != NULL)
        if (strstr(ent->d_name, ".corrupt-")) found = 1;
    if (d) closedir(d);
    CHECK(found);

    free(path);
    remove_dir(dir);
    free(dir);
}

static void test_drops_orphan_entries(void) {
    char *dir = make_tmpdir();
    char err[256] = "";
    DB db;
    CHECK(store_load(&db, dir, err, sizeof err));
    test_add_habit(&db, "a", SCHED_DAILY, 0, 0, "2026-10-01");
    CHECK(store_save(&db, dir, err, sizeof err));
    db_free(&db);

    char *path = path_join(dir, "habits.jsonl");
    FILE *f = fopen(path, "ab");
    fputs("{\"type\":\"entry\",\"habit_id\":999,\"date\":\"2026-10-05\",\"status\":\"done\","
          "\"note\":\"\",\"updated_at\":\"2026-10-05T10:00:00+00:00\"}\n",
          f);
    fclose(f);

    CHECK(store_load(&db, dir, err, sizeof err));
    CHECK(db.nentries == 0);
    db_free(&db);
    free(path);
    remove_dir(dir);
    free(dir);
}

static void test_refuses_newer_schema(void) {
    char *dir = make_tmpdir();
    char *path = path_join(dir, "habits.jsonl");
    FILE *f = fopen(path, "wb");
    fputs("{\"type\":\"meta\",\"schema_version\":99,\"app\":\"habits\"}\n", f);
    fclose(f);

    DB db;
    char err[256] = "";
    CHECK(!store_load(&db, dir, err, sizeof err));
    CHECK(strstr(err, "schema_version") != NULL);
    free(path);
    remove_dir(dir);
    free(dir);
}

static void test_clear_entries(void) {
    DB db;
    db_init(&db);
    Habit *a = test_add_habit(&db, "a", SCHED_DAILY, 0, 0, "2026-10-01");
    Habit *b = test_add_habit(&db, "b", SCHED_DAILY, 0, 0, "2026-10-01");
    test_add_entry(&db, a->id, "2026-10-05", ENTRY_DONE);
    test_add_entry(&db, a->id, "2026-10-06", ENTRY_SKIPPED);
    test_add_entry(&db, b->id, "2026-10-05", ENTRY_DONE);

    CHECK(db_entries_remove_habit(&db, a->id) == 2);
    CHECK(db.nentries == 1);
    CHECK(db_habit_by_id(&db, a->id) != NULL);
    CHECK(db_entry_find_c(&db, a->id, test_day("2026-10-05")) == NULL);
    CHECK(db_entry_find_c(&db, b->id, test_day("2026-10-05")) != NULL);

    /* clearing again removes nothing */
    CHECK(db_entries_remove_habit(&db, a->id) == 0);
    db_free(&db);
}

static void test_habit_move(void) {
    DB db;
    db_init(&db);
    Habit *a = test_add_habit(&db, "a", SCHED_DAILY, 0, 0, "2026-10-01");
    Habit *b = test_add_habit(&db, "b", SCHED_DAILY, 0, 0, "2026-10-01");
    Habit *c = test_add_habit(&db, "c", SCHED_DAILY, 0, 0, "2026-10-01");
    CHECK(a->sort_order < b->sort_order && b->sort_order < c->sort_order);

    CHECK(db_habit_move(&db, b->id, -1));
    CHECK(b->sort_order < a->sort_order);
    CHECK(!db_habit_move(&db, b->id, -1));

    CHECK(db_habit_move(&db, b->id, 1));
    CHECK(a->sort_order < b->sort_order);

    CHECK(db_habit_move(&db, c->id, -1));
    CHECK(c->sort_order < b->sort_order);
    CHECK(db_habit_move(&db, c->id, 1));
    CHECK(c->sort_order > b->sort_order);
    CHECK(!db_habit_move(&db, c->id, 1));

    CHECK(!db_habit_move(&db, 999, 1));
    db_free(&db);
}

void test_store(void) {
    test_roundtrip();
    test_tolerates_corruption();
    test_drops_orphan_entries();
    test_refuses_newer_schema();
    test_clear_entries();
    test_habit_move();
}
