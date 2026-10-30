#include "tests.h"

#include "core/schedule.h"
#include "fixture.h"

void test_schedule(void) {
    DB db;
    db_init(&db);

    Habit *daily = test_add_habit(&db, "daily", SCHED_DAILY, 0, 0, "2026-10-01");
    CHECK(sched_is_planned(daily, test_day("2026-10-01")));
    CHECK(sched_is_planned(daily, test_day("2027-03-15")));
    CHECK(!sched_is_planned(daily, test_day("2026-09-30")));

    Habit *wd = test_add_habit(&db, "wd", SCHED_WEEKDAYS, 0x15 /* Mon, Wed, Fri */, 0,
                               "2026-10-01");
    CHECK(sched_is_planned(wd, test_day("2026-10-26")));  /* Mon */
    CHECK(!sched_is_planned(wd, test_day("2026-10-27"))); /* Tue */
    CHECK(sched_is_planned(wd, test_day("2026-10-28")));  /* Wed */
    CHECK(sched_is_planned(wd, test_day("2026-10-30")));  /* Fri */
    CHECK(!sched_is_planned(wd, test_day("2026-10-31"))); /* Sat */

    Habit *wc = test_add_habit(&db, "wc", SCHED_WEEKLY_COUNT, 0, 3, "2026-10-01");
    CHECK(sched_is_planned(wc, test_day("2026-10-27")));

    int32_t week = date_week_start(test_day("2026-10-30"));
    CHECK(sched_week_quota(&db, wc, week) == 3);
    test_add_entry(&db, wc->id, "2026-10-27", ENTRY_SKIPPED);
    CHECK(sched_week_quota(&db, wc, week) == 2);
    test_add_entry(&db, wc->id, "2026-10-28", ENTRY_SKIPPED);
    CHECK(sched_week_quota(&db, wc, week) == 1);
    CHECK(sched_week_quota(&db, wc, week + 7) == 3);

    int done = 0, skipped = 0;
    sched_week_tally(&db, wc->id, week, &done, &skipped);
    CHECK(done == 0 && skipped == 2);

    /* Archived habits disappear after the archive date. */
    Habit *arch = test_add_habit(&db, "arch", SCHED_DAILY, 0, 0, "2026-10-01");
    arch->archived = true;
    arch->archived_day = test_day("2026-10-20");
    date_format(arch->archived_day, arch->archived_date);
    CHECK(sched_is_planned(arch, test_day("2026-10-20")));
    CHECK(!sched_is_planned(arch, test_day("2026-10-21")));
    CHECK(!sched_show_in_today(arch, test_day("2026-10-25")));

    db_free(&db);
}
