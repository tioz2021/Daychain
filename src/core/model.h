#ifndef HABITS_CORE_MODEL_H
#define HABITS_CORE_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HABIT_NAME_MAX 80
#define HABIT_DESC_MAX 2000
#define HABIT_CATEGORY_MAX 40
#define ENTRY_NOTE_MAX 500

typedef enum {
    SCHED_DAILY = 0,
    SCHED_WEEKDAYS = 1,
    SCHED_WEEKLY_COUNT = 2
} SchedKind;

typedef enum { ENTRY_DONE = 0, ENTRY_SKIPPED = 1 } EntryStatus;

typedef struct {
    int64_t id;
    char *name;
    char *description;
    char *color;
    char *icon;
    char *category; /* NULL when absent */
    SchedKind kind;
    unsigned days_mask; /* bit 0 = Monday ... bit 6 = Sunday */
    int times;          /* weekly_count quota */
    int32_t start_day;
    char start_date[11];
    char created_at[32];
    bool archived;
    int32_t archived_day;
    char archived_date[11];
    int sort_order;
} Habit;

typedef struct {
    int64_t habit_id;
    int32_t day;
    EntryStatus status;
    char *note; /* never NULL, may be empty */
    char updated_at[32];
} Entry;

/* A bad habit being quit: counts time since started_at. */
typedef struct {
    int64_t id;
    char *name;
    char *description;
    char *color;
    char *icon;
    int64_t started_at; /* Unix seconds of the current attempt */
    char started_iso[32];
    char created_at[32];
    int64_t best_seconds; /* longest finished attempt */
    int attempts;         /* including the current one */
    bool archived;
    int32_t archived_day;
    char archived_date[11];
    int sort_order;
} Quit;

typedef struct {
    Habit *habits;
    size_t nhabits, chabits;
    Entry *entries;
    size_t nentries, centries;
    Quit *quits;
    size_t nquits, cquits;
    int64_t next_habit_id;
    int64_t next_quit_id;
    int max_sort_order;
    int max_quit_sort;
    char created_at[32]; /* from the file meta line */
} DB;

void habit_free(Habit *h);
void db_init(DB *db);
void db_free(DB *db);

Habit *db_habit_by_id(DB *db, int64_t id);
const Habit *db_habit_by_id_c(const DB *db, int64_t id);

/* Entries are kept sorted by (habit_id, day). */
size_t db_entry_lower_bound(const DB *db, int64_t habit_id, int32_t day);
Entry *db_entry_find(DB *db, int64_t habit_id, int32_t day);
const Entry *db_entry_find_c(const DB *db, int64_t habit_id, int32_t day);
/* Insert or replace an entry; returns the stored entry. */
Entry *db_entry_upsert(DB *db, int64_t habit_id, int32_t day, EntryStatus status,
                       const char *note, const char *updated_at);
bool db_entry_remove(DB *db, int64_t habit_id, int32_t day);
/* Number of entries of a habit with from <= day <= to. */
size_t db_entry_count_range(const DB *db, int64_t habit_id, int32_t from, int32_t to);

/* Append a habit, assigning id and sort_order when they are zero. */
Habit *db_habit_add(DB *db);
void db_habit_remove(DB *db, int64_t id);
/* Delete every entry of a habit, keeping the habit itself. Returns the count. */
size_t db_entries_remove_habit(DB *db, int64_t habit_id);
/* Move an active habit one place up (delta -1) or down (+1) by renumbering
   sort_order. Returns false when it is already at the end. */
bool db_habit_move(DB *db, int64_t id, int delta);
/* Swap the places of two active habits (used when moving inside a filtered list). */
bool db_habit_swap(DB *db, int64_t a, int64_t b);
bool db_quit_move(DB *db, int64_t id, int delta);
bool db_quit_swap(DB *db, int64_t a, int64_t b);

/* Field validation; returns false and sets *code / *message. */
bool habit_validate(Habit *h, const char **code, const char **message);
bool habit_schedule_valid(const Habit *h, const char **code, const char **message);
bool entry_validate(int32_t day, int status, const char *note, int32_t habit_start,
                    int32_t today, const char **code, const char **message);

void quit_free(Quit *q);
Quit *db_quit_by_id(DB *db, int64_t id);
const Quit *db_quit_by_id_c(const DB *db, int64_t id);
Quit *db_quit_add(DB *db);
void db_quit_remove(DB *db, int64_t id);
bool quit_validate(const Quit *q, int64_t now, const char **code, const char **message);
/* Start a new attempt at `at`, counting the elapsed time into the best result. */
bool quit_reset(Quit *q, int64_t at);

const char *sched_kind_name(SchedKind k);
bool sched_kind_parse(const char *s, SchedKind *out);

#endif
