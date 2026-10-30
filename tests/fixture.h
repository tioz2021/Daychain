#ifndef HABITS_TESTS_FIXTURE_H
#define HABITS_TESTS_FIXTURE_H

#include <stdint.h>

#include "core/model.h"
#include "infra/util.h"

Habit *test_add_habit(DB *db, const char *name, SchedKind kind, unsigned mask, int times,
                      const char *start_date);
void test_add_entry(DB *db, int64_t habit_id, const char *date, EntryStatus st);
void test_set_entry_note(DB *db, int64_t habit_id, const char *date, const char *note);
int32_t test_day(const char *s);

#endif
