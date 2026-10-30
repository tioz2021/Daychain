#include "core/model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "infra/util.h"

void habit_free(Habit *h) {
    if (!h) return;
    free(h->name);
    free(h->description);
    free(h->color);
    free(h->icon);
    free(h->category);
    memset(h, 0, sizeof *h);
}

void db_init(DB *db) {
    memset(db, 0, sizeof *db);
    db->next_habit_id = 1;
    db->next_quit_id = 1;
}

void db_free(DB *db) {
    if (!db) return;
    for (size_t i = 0; i < db->nhabits; i++) habit_free(&db->habits[i]);
    for (size_t i = 0; i < db->nentries; i++) free(db->entries[i].note);
    for (size_t i = 0; i < db->nquits; i++) quit_free(&db->quits[i]);
    free(db->habits);
    free(db->entries);
    free(db->quits);
    memset(db, 0, sizeof *db);
    db->next_habit_id = 1;
    db->next_quit_id = 1;
}

void quit_free(Quit *q) {
    if (!q) return;
    free(q->name);
    free(q->description);
    free(q->color);
    free(q->icon);
    memset(q, 0, sizeof *q);
}

Quit *db_quit_by_id(DB *db, int64_t id) {
    for (size_t i = 0; i < db->nquits; i++)
        if (db->quits[i].id == id) return &db->quits[i];
    return NULL;
}

const Quit *db_quit_by_id_c(const DB *db, int64_t id) {
    for (size_t i = 0; i < db->nquits; i++)
        if (db->quits[i].id == id) return &db->quits[i];
    return NULL;
}

Quit *db_quit_add(DB *db) {
    if (db->nquits == db->cquits) {
        db->cquits = db->cquits ? db->cquits * 2 : 8;
        db->quits = xrealloc(db->quits, db->cquits * sizeof *db->quits);
    }
    Quit *q = &db->quits[db->nquits++];
    memset(q, 0, sizeof *q);
    q->id = db->next_quit_id++;
    q->attempts = 1;
    q->sort_order = db->max_quit_sort + 10;
    db->max_quit_sort = q->sort_order;
    return q;
}

void db_quit_remove(DB *db, int64_t id) {
    size_t i;
    for (i = 0; i < db->nquits; i++)
        if (db->quits[i].id == id) break;
    if (i == db->nquits) return;
    quit_free(&db->quits[i]);
    memmove(&db->quits[i], &db->quits[i + 1], (db->nquits - i - 1) * sizeof *db->quits);
    db->nquits--;
}

Habit *db_habit_by_id(DB *db, int64_t id) {
    for (size_t i = 0; i < db->nhabits; i++)
        if (db->habits[i].id == id) return &db->habits[i];
    return NULL;
}

const Habit *db_habit_by_id_c(const DB *db, int64_t id) {
    for (size_t i = 0; i < db->nhabits; i++)
        if (db->habits[i].id == id) return &db->habits[i];
    return NULL;
}

static int entry_cmp(int64_t hid_a, int32_t day_a, int64_t hid_b, int32_t day_b) {
    if (hid_a != hid_b) return hid_a < hid_b ? -1 : 1;
    if (day_a != day_b) return day_a < day_b ? -1 : 1;
    return 0;
}

size_t db_entry_lower_bound(const DB *db, int64_t habit_id, int32_t day) {
    size_t lo = 0, hi = db->nentries;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (entry_cmp(db->entries[mid].habit_id, db->entries[mid].day, habit_id, day) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

Entry *db_entry_find(DB *db, int64_t habit_id, int32_t day) {
    size_t i = db_entry_lower_bound(db, habit_id, day);
    if (i < db->nentries && db->entries[i].habit_id == habit_id && db->entries[i].day == day)
        return &db->entries[i];
    return NULL;
}

const Entry *db_entry_find_c(const DB *db, int64_t habit_id, int32_t day) {
    size_t i = db_entry_lower_bound(db, habit_id, day);
    if (i < db->nentries && db->entries[i].habit_id == habit_id && db->entries[i].day == day)
        return &db->entries[i];
    return NULL;
}

static void entries_reserve(DB *db, size_t need) {
    if (need <= db->centries) return;
    size_t cap = db->centries ? db->centries : 16;
    while (cap < need) cap *= 2;
    db->entries = xrealloc(db->entries, cap * sizeof *db->entries);
    db->centries = cap;
}

Entry *db_entry_upsert(DB *db, int64_t habit_id, int32_t day, EntryStatus status,
                       const char *note, const char *updated_at) {
    size_t i = db_entry_lower_bound(db, habit_id, day);
    if (i < db->nentries && db->entries[i].habit_id == habit_id && db->entries[i].day == day) {
        Entry *e = &db->entries[i];
        e->status = status;
        free(e->note);
        e->note = xstrdup(note ? note : "");
        snprintf(e->updated_at, sizeof e->updated_at, "%s", updated_at);
        return e;
    }
    entries_reserve(db, db->nentries + 1);
    memmove(&db->entries[i + 1], &db->entries[i], (db->nentries - i) * sizeof *db->entries);
    Entry *e = &db->entries[i];
    memset(e, 0, sizeof *e);
    e->habit_id = habit_id;
    e->day = day;
    e->status = status;
    e->note = xstrdup(note ? note : "");
    snprintf(e->updated_at, sizeof e->updated_at, "%s", updated_at);
    db->nentries++;
    return e;
}

bool db_entry_remove(DB *db, int64_t habit_id, int32_t day) {
    size_t i = db_entry_lower_bound(db, habit_id, day);
    if (i >= db->nentries || db->entries[i].habit_id != habit_id || db->entries[i].day != day)
        return false;
    free(db->entries[i].note);
    memmove(&db->entries[i], &db->entries[i + 1],
            (db->nentries - i - 1) * sizeof *db->entries);
    db->nentries--;
    return true;
}

size_t db_entry_count_range(const DB *db, int64_t habit_id, int32_t from, int32_t to) {
    size_t lo = db_entry_lower_bound(db, habit_id, from);
    size_t hi = db_entry_lower_bound(db, habit_id, to + 1);
    return hi > lo ? hi - lo : 0;
}

Habit *db_habit_add(DB *db) {
    if (db->nhabits == db->chabits) {
        db->chabits = db->chabits ? db->chabits * 2 : 8;
        db->habits = xrealloc(db->habits, db->chabits * sizeof *db->habits);
    }
    Habit *h = &db->habits[db->nhabits++];
    memset(h, 0, sizeof *h);
    h->id = db->next_habit_id++;
    h->kind = SCHED_DAILY;
    h->times = 7;
    h->days_mask = 0x7F;
    h->sort_order = db->max_sort_order + 10;
    db->max_sort_order = h->sort_order;
    return h;
}

static int active_habit_cmp(const void *a, const void *b) {
    const Habit *ha = *(const Habit *const *)a;
    const Habit *hb = *(const Habit *const *)b;
    if (ha->sort_order != hb->sort_order) return ha->sort_order < hb->sort_order ? -1 : 1;
    return ha->id < hb->id ? -1 : (ha->id > hb->id ? 1 : 0);
}

bool db_habit_move(DB *db, int64_t id, int delta) {
    if (delta != -1 && delta != 1) return false;
    Habit **order = xmalloc((db->nhabits + 1) * sizeof *order);
    size_t n = 0;
    for (size_t i = 0; i < db->nhabits; i++)
        if (!db->habits[i].archived) order[n++] = &db->habits[i];
    qsort(order, n, sizeof *order, active_habit_cmp);

    size_t pos = n;
    for (size_t i = 0; i < n; i++)
        if (order[i]->id == id) {
            pos = i;
            break;
        }
    if (pos == n) {
        free(order);
        return false;
    }
    long target = (long)pos + delta;
    if (target < 0 || target >= (long)n) {
        free(order);
        return false;
    }
    /* renumber first so equal sort_order values cannot keep the items stuck */
    for (size_t i = 0; i < n; i++) order[i]->sort_order = (int)(i + 1) * 10;
    int tmp = order[pos]->sort_order;
    order[pos]->sort_order = order[target]->sort_order;
    order[target]->sort_order = tmp;
    free(order);
    return true;
}

size_t db_entries_remove_habit(DB *db, int64_t habit_id) {
    size_t removed = 0;
    size_t w = 0;
    for (size_t r = 0; r < db->nentries; r++) {
        if (db->entries[r].habit_id == habit_id) {
            free(db->entries[r].note);
            removed++;
            continue;
        }
        if (w != r) db->entries[w] = db->entries[r];
        w++;
    }
    db->nentries = w;
    return removed;
}

void db_habit_remove(DB *db, int64_t id) {
    size_t i;
    for (i = 0; i < db->nhabits; i++)
        if (db->habits[i].id == id) break;
    if (i == db->nhabits) return;
    habit_free(&db->habits[i]);
    memmove(&db->habits[i], &db->habits[i + 1], (db->nhabits - i - 1) * sizeof *db->habits);
    db->nhabits--;

    size_t w = 0;
    for (size_t r = 0; r < db->nentries; r++) {
        if (db->entries[r].habit_id == id) {
            free(db->entries[r].note);
            continue;
        }
        if (w != r) db->entries[w] = db->entries[r];
        w++;
    }
    db->nentries = w;
}

/* ---- validation ---------------------------------------------------------- */

const char *sched_kind_name(SchedKind k) {
    switch (k) {
        case SCHED_DAILY: return "daily";
        case SCHED_WEEKDAYS: return "weekdays";
        case SCHED_WEEKLY_COUNT: return "weekly_count";
    }
    return "daily";
}

bool sched_kind_parse(const char *s, SchedKind *out) {
    if (!s) return false;
    if (strcmp(s, "daily") == 0)
        *out = SCHED_DAILY;
    else if (strcmp(s, "weekdays") == 0)
        *out = SCHED_WEEKDAYS;
    else if (strcmp(s, "weekly_count") == 0)
        *out = SCHED_WEEKLY_COUNT;
    else
        return false;
    return true;
}

static bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool color_valid(const char *c) {
    if (!c || strlen(c) != 7 || c[0] != '#') return false;
    for (int i = 1; i < 7; i++)
        if (!is_hex(c[i])) return false;
    return true;
}

static bool icon_valid(const char *s) {
    size_t n = strlen(s);
    if (n < 1 || n > 24) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    }
    return true;
}

bool habit_schedule_valid(const Habit *h, const char **code, const char **message) {
    if (h->kind == SCHED_WEEKDAYS) {
        if (h->days_mask == 0 || (h->days_mask & ~0x7Fu) != 0) {
            *code = "invalid_schedule_days";
            *message = "weekdays must be a non-empty subset of 1..7";
            return false;
        }
    } else if (h->kind == SCHED_WEEKLY_COUNT) {
        if (h->times < 1 || h->times > 7) {
            *code = "invalid_schedule_times";
            *message = "weekly_count times must be between 1 and 7";
            return false;
        }
    }
    return true;
}

bool habit_validate(Habit *h, const char **code, const char **message) {
    if (!h->name || !utf8_valid(h->name, strlen(h->name))) {
        *code = "invalid_name";
        *message = "name must be valid UTF-8";
        return false;
    }
    size_t name_len = utf8_chars(h->name);
    if (name_len < 1 || name_len > HABIT_NAME_MAX) {
        *code = "invalid_name";
        *message = "name must be 1..80 characters";
        return false;
    }
    if (h->description && utf8_chars(h->description) > HABIT_DESC_MAX) {
        *code = "invalid_description";
        *message = "description is too long (max 2000)";
        return false;
    }
    if (h->category && utf8_chars(h->category) > HABIT_CATEGORY_MAX) {
        *code = "invalid_category";
        *message = "category is too long (max 40)";
        return false;
    }
    if (!color_valid(h->color)) {
        *code = "invalid_color";
        *message = "color must be #rrggbb";
        return false;
    }
    if (!icon_valid(h->icon)) {
        *code = "invalid_icon";
        *message = "icon must be 1..24 chars of [a-z0-9_-]";
        return false;
    }
    return habit_schedule_valid(h, code, message);
}

bool quit_validate(const Quit *q, int64_t now, const char **code, const char **message) {
    if (!q->name || !utf8_valid(q->name, strlen(q->name))) {
        *code = "invalid_name";
        *message = "name must be valid UTF-8";
        return false;
    }
    size_t n = utf8_chars(q->name);
    if (n < 1 || n > HABIT_NAME_MAX) {
        *code = "invalid_name";
        *message = "name must be 1..80 characters";
        return false;
    }
    if (q->description && utf8_chars(q->description) > HABIT_DESC_MAX) {
        *code = "invalid_description";
        *message = "description is too long (max 2000)";
        return false;
    }
    if (!color_valid(q->color)) {
        *code = "invalid_color";
        *message = "color must be #rrggbb";
        return false;
    }
    if (!icon_valid(q->icon)) {
        *code = "invalid_icon";
        *message = "icon must be 1..24 chars of [a-z0-9_-]";
        return false;
    }
    if (q->started_at <= 0 || q->started_at > now + 60) {
        *code = "invalid_started_at";
        *message = "started_at must be a moment in the past";
        return false;
    }
    if (q->best_seconds < 0) {
        *code = "invalid_started_at";
        *message = "best_seconds cannot be negative";
        return false;
    }
    if (q->attempts < 1) {
        *code = "invalid_started_at";
        *message = "attempts must be at least 1";
        return false;
    }
    return true;
}

bool quit_reset(Quit *q, int64_t at) {
    if (!q || at <= q->started_at) return false;
    int64_t elapsed = at - q->started_at;
    if (elapsed > q->best_seconds) q->best_seconds = elapsed;
    q->attempts++;
    q->started_at = at;
    iso_from_epoch(at, q->started_iso);
    return true;
}

bool entry_validate(int32_t day, int status, const char *note, int32_t habit_start,
                    int32_t today, const char **code, const char **message) {
    if (day > today) {
        *code = "date_in_future";
        *message = "cannot record an entry in the future";
        return false;
    }
    if (day < habit_start) {
        *code = "date_before_start";
        *message = "date is before the habit start date";
        return false;
    }
    if (status != ENTRY_DONE && status != ENTRY_SKIPPED) {
        *code = "invalid_status";
        *message = "status must be done or skipped";
        return false;
    }
    if (note && utf8_chars(note) > ENTRY_NOTE_MAX) {
        *code = "invalid_note";
        *message = "note is too long (max 500)";
        return false;
    }
    return true;
}
