#include "tests.h"

#include "core/stats.h"
#include "fixture.h"

static void test_daily_streaks(void) {
    DB db;
    db_init(&db);
    int32_t today = test_day("2026-10-30");

    Habit *h = test_add_habit(&db, "h", SCHED_DAILY, 0, 0, "2026-10-01");
    test_add_entry(&db, h->id, "2026-10-28", ENTRY_DONE);
    test_add_entry(&db, h->id, "2026-10-29", ENTRY_DONE);

    Streak s = stats_streak(&db, h, today);
    CHECK(s.current == 2); /* today unmarked: grace */
    CHECK(s.best == 2);
    CHECK(s.unit == STATS_UNIT_DAYS);

    test_add_entry(&db, h->id, "2026-10-30", ENTRY_DONE);
    s = stats_streak(&db, h, today);
    CHECK(s.current == 3);
    CHECK(s.best == 3);

    /* skipped is neutral */
    test_add_entry(&db, h->id, "2026-10-31", ENTRY_SKIPPED);
    s = stats_streak(&db, h, test_day("2026-10-31"));
    CHECK(s.current == 3);

    /* a missing planned day breaks the streak */
    db_entry_remove(&db, h->id, test_day("2026-10-29"));
    db_entry_remove(&db, h->id, test_day("2026-10-30"));
    s = stats_streak(&db, h, today);
    CHECK(s.current == 0);
    CHECK(s.best == 1);

    db_free(&db);
}

static void test_new_habit(void) {
    DB db;
    db_init(&db);
    Habit *h = test_add_habit(&db, "new", SCHED_DAILY, 0, 0, "2026-10-30");
    Streak s = stats_streak(&db, h, test_day("2026-10-30"));
    CHECK(s.current == 0);
    CHECK(s.best == 0);
    db_free(&db);
}

static void test_weekday_streak(void) {
    DB db;
    db_init(&db);
    Habit *h = test_add_habit(&db, "mwf", SCHED_WEEKDAYS, 0x15, 0, "2026-10-01");
    test_add_entry(&db, h->id, "2026-10-26", ENTRY_DONE); /* Mon */
    test_add_entry(&db, h->id, "2026-10-28", ENTRY_DONE); /* Wed */
    /* Thursday, a non-planned day: the streak still comes from Wednesday. */
    Streak s = stats_streak(&db, h, test_day("2026-10-29"));
    CHECK(s.current == 2);
    /* Friday is planned but unmarked: grace keeps the streak. */
    s = stats_streak(&db, h, test_day("2026-10-30"));
    CHECK(s.current == 2);
    /* Missed Wednesday breaks it. */
    db_entry_remove(&db, h->id, test_day("2026-10-28"));
    s = stats_streak(&db, h, test_day("2026-10-29"));
    CHECK(s.current == 0);
    db_free(&db);
}

static void test_weekly_streak(void) {
    int32_t today = test_day("2026-10-30");
    DB db;
    db_init(&db);

    Habit *h = test_add_habit(&db, "w", SCHED_WEEKLY_COUNT, 0, 3, "2026-09-01");
    test_add_entry(&db, h->id, "2026-10-19", ENTRY_DONE);
    test_add_entry(&db, h->id, "2026-10-20", ENTRY_DONE);
    test_add_entry(&db, h->id, "2026-10-21", ENTRY_DONE);
    test_add_entry(&db, h->id, "2026-10-27", ENTRY_DONE);
    Streak s = stats_streak(&db, h, today);
    CHECK(s.unit == STATS_UNIT_WEEKS);
    CHECK(s.current == 1);

    /* quota reduced by a skip: 2 done + 1 skip closes the week */
    Habit *h2 = test_add_habit(&db, "w2", SCHED_WEEKLY_COUNT, 0, 3, "2026-09-01");
    test_add_entry(&db, h2->id, "2026-10-19", ENTRY_DONE);
    test_add_entry(&db, h2->id, "2026-10-20", ENTRY_DONE);
    test_add_entry(&db, h2->id, "2026-10-21", ENTRY_SKIPPED);
    s = stats_streak(&db, h2, today);
    CHECK(s.current == 1);

    /* 2 of 3 is not enough */
    Habit *h3 = test_add_habit(&db, "w3", SCHED_WEEKLY_COUNT, 0, 3, "2026-09-01");
    test_add_entry(&db, h3->id, "2026-10-19", ENTRY_DONE);
    test_add_entry(&db, h3->id, "2026-10-20", ENTRY_DONE);
    s = stats_streak(&db, h3, today);
    CHECK(s.current == 0);

    db_free(&db);
}

static void test_rate(void) {
    DB db;
    db_init(&db);
    int32_t today = test_day("2026-10-30");
    Habit *h = test_add_habit(&db, "h", SCHED_DAILY, 0, 0, "2026-10-01");

    for (int day = 24; day <= 28; day++) {
        char buf[11];
        snprintf(buf, sizeof buf, "2026-10-%02d", day);
        test_add_entry(&db, h->id, buf, ENTRY_DONE);
    }
    test_add_entry(&db, h->id, "2026-10-29", ENTRY_SKIPPED);

    Rate r = stats_rate(&db, h, test_day("2026-10-24"), today, today);
    CHECK(r.has);
    CHECK(r.planned == 7);
    CHECK(r.done == 5);
    CHECK(r.skipped == 1);
    CHECK(r.percent == 83); /* 5 / (7 - 1) */

    /* No planned days in the range. */
    Rate empty = stats_rate(&db, h, test_day("2026-09-01"), test_day("2026-09-30"), today);
    CHECK(!empty.has);

    db_free(&db);
}

static void test_weekly_rate(void) {
    DB db;
    db_init(&db);
    int32_t today = test_day("2026-11-02");
    Habit *h = test_add_habit(&db, "w", SCHED_WEEKLY_COUNT, 0, 3, "2026-09-28");

    const char *weeks[4][3] = {
        {"2026-09-28", "2026-09-29", "2026-09-30"},
        {"2026-10-05", "2026-10-06", NULL},
        {"2026-10-12", "2026-10-13", "2026-10-14"},
        {"2026-10-19", "2026-10-20", "2026-10-21"},
    };
    for (int w = 0; w < 4; w++)
        for (int i = 0; i < 3; i++)
            if (weeks[w][i]) test_add_entry(&db, h->id, weeks[w][i], ENTRY_DONE);

    Rate r = stats_rate(&db, h, test_day("2026-09-28"), test_day("2026-10-25"), today);
    CHECK(r.has);
    CHECK(r.done == 11);
    CHECK(r.percent == 92); /* (1 + 2/3 + 1 + 1) / 4 */

    db_free(&db);
}

static void test_day_state(void) {
    DB db;
    db_init(&db);
    int32_t today = test_day("2026-10-30");
    Habit *h = test_add_habit(&db, "h", SCHED_DAILY, 0, 0, "2026-10-01");
    test_add_entry(&db, h->id, "2026-10-28", ENTRY_DONE);
    test_add_entry(&db, h->id, "2026-10-29", ENTRY_SKIPPED);

    CHECK(stats_day_state(&db, h, test_day("2026-10-28"), today) == DAY_DONE);
    CHECK(stats_day_state(&db, h, test_day("2026-10-29"), today) == DAY_SKIPPED);
    CHECK(stats_day_state(&db, h, test_day("2026-10-27"), today) == DAY_MISS);
    CHECK(stats_day_state(&db, h, test_day("2026-09-30"), today) == DAY_OFF);
    CHECK(stats_day_state(&db, h, test_day("2026-10-31"), today) == DAY_FUTURE);
    db_free(&db);
}

void test_stats(void) {
    test_daily_streaks();
    test_new_habit();
    test_weekday_streak();
    test_weekly_streak();
    test_rate();
    test_weekly_rate();
    test_day_state();
}
