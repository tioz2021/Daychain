#include "core/schedule.h"

#include "infra/util.h"

bool sched_is_planned(const Habit *h, int32_t day) {
    if (h->archived && day > h->archived_day) return false;
    if (day < h->start_day) return false;
    switch (h->kind) {
        case SCHED_DAILY:
            return true;
        case SCHED_WEEKDAYS:
            return (h->days_mask >> (date_iso_weekday(day) - 1)) & 1u;
        case SCHED_WEEKLY_COUNT:
            return true;
    }
    return false;
}

void sched_week_tally(const DB *db, int64_t habit_id, int32_t week_start, int *done,
                      int *skipped) {
    *done = 0;
    *skipped = 0;
    size_t i = db_entry_lower_bound(db, habit_id, week_start);
    size_t end = db_entry_lower_bound(db, habit_id, week_start + 7);
    for (; i < end; i++) {
        if (db->entries[i].status == ENTRY_DONE)
            (*done)++;
        else
            (*skipped)++;
    }
}

int sched_week_quota(const DB *db, const Habit *h, int32_t week_start) {
    int done, skipped;
    sched_week_tally(db, h->id, week_start, &done, &skipped);
    int q = h->times - skipped;
    return q < 0 ? 0 : q;
}

bool sched_show_in_today(const Habit *h, int32_t day) {
    if (h->archived || day < h->start_day) return false;
    return sched_is_planned(h, day);
}
