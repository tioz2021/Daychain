#include "fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Habit *test_add_habit(DB *db, const char *name, SchedKind kind, unsigned mask, int times,
                      const char *start_date) {
    Habit *h = db_habit_add(db);
    h->name = xstrdup(name);
    h->description = xstrdup("");
    h->color = xstrdup("#4f8cff");
    h->icon = xstrdup("dot");
    h->category = NULL;
    h->kind = kind;
    h->days_mask = mask ? mask : 0x7F;
    h->times = times;
    if (!date_parse(start_date, &h->start_day)) {
        fprintf(stderr, "fixture: bad start date %s\n", start_date);
        abort();
    }
    snprintf(h->start_date, sizeof h->start_date, "%s", start_date);
    now_iso8601(h->created_at);
    return h;
}

void test_add_entry(DB *db, int64_t habit_id, const char *date, EntryStatus st) {
    int32_t day = 0;
    if (!date_parse(date, &day)) {
        fprintf(stderr, "fixture: bad date %s\n", date);
        abort();
    }
    db_entry_upsert(db, habit_id, day, st, "", "2026-01-01T00:00:00+00:00");
    const Entry *e = db_entry_find_c(db, habit_id, day);
    if (!e || e->day != day) {
        fprintf(stderr, "fixture: entry not stored\n");
        abort();
    }
}

void test_set_entry_note(DB *db, int64_t habit_id, const char *date, const char *note) {
    int32_t day = 0;
    date_parse(date, &day);
    Entry *e = db_entry_find(db, habit_id, day);
    if (!e) return;
    free(e->note);
    e->note = xstrdup(note);
}

int32_t test_day(const char *s) {
    int32_t d = 0;
    if (!date_parse(s, &d)) abort();
    return d;
}
