#include "core/stats.h"

#include "core/schedule.h"
#include "infra/util.h"

static int32_t reference_day(const Habit *h, int32_t today) {
    if (h->archived && h->archived_day < today) return h->archived_day;
    return today;
}

static int week_done(const DB *db, const Habit *h, int32_t week_start, int *quota_out) {
    int done, skipped;
    sched_week_tally(db, h->id, week_start, &done, &skipped);
    int quota = h->times - skipped;
    if (quota < 0) quota = 0;
    if (quota_out) *quota_out = quota;
    return done;
}

static bool week_closed(const DB *db, const Habit *h, int32_t week_start) {
    int quota = 0;
    int done = week_done(db, h, week_start, &quota);
    return done >= quota;
}

static Streak streak_days(const DB *db, const Habit *h, int32_t ref, bool grace) {
    Streak s = {0, 0, STATS_UNIT_DAYS};
    const Entry *e = db_entry_find_c(db, h->id, ref);
    bool ref_placeholder = grace && e == NULL && sched_is_planned(h, ref);

    int32_t d = ref;
    if (ref_placeholder) d = ref - 1;

    while (d >= h->start_day) {
        if (!sched_is_planned(h, d)) {
            d--;
            continue;
        }
        const Entry *entry = db_entry_find_c(db, h->id, d);
        if (!entry) break;
        if (entry->status == ENTRY_DONE) s.current++;
        d--;
    }

    int run = 0;
    for (int32_t x = h->start_day; x <= ref; x++) {
        if (!sched_is_planned(h, x)) continue;
        const Entry *entry = db_entry_find_c(db, h->id, x);
        if (entry && entry->status == ENTRY_DONE) {
            run++;
            if (run > s.best) s.best = run;
        } else if (entry && entry->status == ENTRY_SKIPPED) {
            /* neutral: neither breaks nor extends the run */
        } else if (x == ref && grace) {
            /* grace for an unmarked today */
        } else {
            run = 0;
        }
    }
    if (s.current > s.best) s.best = s.current;
    return s;
}

static Streak streak_weeks(const DB *db, const Habit *h, int32_t ref, bool grace) {
    Streak s = {0, 0, STATS_UNIT_WEEKS};
    int32_t first_week = date_week_start(h->start_day);
    int32_t ws = date_week_start(ref);

    bool first = true;
    for (int32_t w = ws; w >= first_week; w -= 7) {
        if (week_closed(db, h, w)) {
            s.current++;
        } else if (first && grace) {
            /* current week still in progress */
        } else {
            break;
        }
        first = false;
        if (w < first_week + 7) break;
    }

    int run = 0;
    for (int32_t w = first_week; w <= ws; w += 7) {
        if (week_closed(db, h, w)) {
            run++;
            if (run > s.best) s.best = run;
        } else if (w == ws && grace) {
            /* grace for the current week */
        } else {
            run = 0;
        }
    }
    if (s.current > s.best) s.best = s.current;
    return s;
}

Streak stats_streak(const DB *db, const Habit *h, int32_t today) {
    int32_t ref = reference_day(h, today);
    bool grace = !h->archived;
    if (h->kind == SCHED_WEEKLY_COUNT) return streak_weeks(db, h, ref, grace);
    return streak_days(db, h, ref, grace);
}

Rate stats_rate(const DB *db, const Habit *h, int32_t from, int32_t to, int32_t today) {
    Rate r = {0, 0, 0, 0, false};
    int32_t end = reference_day(h, today);
    if (to > end) to = end;
    if (from < h->start_day) from = h->start_day;
    if (to < from) return r;

    if (h->kind != SCHED_WEEKLY_COUNT) {
        for (int32_t d = from; d <= to; d++) {
            if (!sched_is_planned(h, d)) continue;
            r.planned++;
            const Entry *e = db_entry_find_c(db, h->id, d);
            if (!e)
                continue;
            if (e->status == ENTRY_DONE)
                r.done++;
            else
                r.skipped++;
        }
        int denom = r.planned - r.skipped;
        if (denom > 0) {
            r.percent = (int)((100.0 * r.done) / denom + 0.5);
            r.has = true;
        }
        return r;
    }

    int32_t first_week = date_week_start(from);
    int32_t last_week = date_week_start(to);
    double credit = 0.0;
    int weeks = 0;
    for (int32_t w = first_week; w <= last_week; w += 7) {
        int quota = 0;
        int done = week_done(db, h, w, &quota);
        int done_in_range = 0, skipped_in_range = 0;
        size_t i = db_entry_lower_bound(db, h->id, w);
        size_t e = db_entry_lower_bound(db, h->id, w + 7);
        for (; i < e; i++) {
            int32_t d = db->entries[i].day;
            if (d < from || d > to) continue;
            if (db->entries[i].status == ENTRY_DONE)
                done_in_range++;
            else
                skipped_in_range++;
        }
        r.done += done_in_range;
        r.skipped += skipped_in_range;
        if (quota <= 0) continue;
        if (w == date_week_start(today) && to >= today) continue; /* week in progress */
        double c = (double)done / quota;
        if (c > 1.0) c = 1.0;
        credit += c;
        weeks++;
        r.planned += quota;
    }
    if (weeks > 0) {
        r.percent = (int)((100.0 * credit) / weeks + 0.5);
        r.has = true;
    }
    return r;
}

DayState stats_day_state(const DB *db, const Habit *h, int32_t day, int32_t today) {
    if (day > today) return DAY_FUTURE;
    if (day < h->start_day) return DAY_OFF;
    if (h->archived && day > h->archived_day) return DAY_OFF;
    const Entry *e = db_entry_find_c(db, h->id, day);
    if (e) return e->status == ENTRY_DONE ? DAY_DONE : DAY_SKIPPED;
    return sched_is_planned(h, day) ? DAY_MISS : DAY_OFF;
}
