#ifndef HABITS_CORE_SCHEDULE_H
#define HABITS_CORE_SCHEDULE_H

#include <stdbool.h>
#include <stdint.h>

#include "core/model.h"

/* Is this date a planned day for the habit (ignoring weekly quotas)? */
bool sched_is_planned(const Habit *h, int32_t day);

/* Done / skipped entry counts of a habit inside the ISO week. */
void sched_week_tally(const DB *db, int64_t habit_id, int32_t week_start, int *done,
                      int *skipped);

/* Effective quota for weekly_count: times - skipped, never below zero. */
int sched_week_quota(const DB *db, const Habit *h, int32_t week_start);

/* Does the habit belong on the "Today" screen for this date? */
bool sched_show_in_today(const Habit *h, int32_t day);

#endif
