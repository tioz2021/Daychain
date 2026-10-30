#ifndef HABITS_CORE_STATS_H
#define HABITS_CORE_STATS_H

#include <stdbool.h>
#include <stdint.h>

#include "core/model.h"

typedef enum {
    STATS_UNIT_DAYS = 0,
    STATS_UNIT_WEEKS = 1
} StatsUnit;

typedef enum {
    DAY_DONE = 0,
    DAY_SKIPPED = 1,
    DAY_MISS = 2,
    DAY_OFF = 3,
    DAY_FUTURE = 4
} DayState;

typedef struct {
    int current;
    int best;
    StatsUnit unit;
} Streak;

typedef struct {
    int done;
    int planned;
    int skipped;
    int percent; /* valid only when has == true */
    bool has;
} Rate;

Streak stats_streak(const DB *db, const Habit *h, int32_t today);
Rate stats_rate(const DB *db, const Habit *h, int32_t from, int32_t to, int32_t today);
DayState stats_day_state(const DB *db, const Habit *h, int32_t day, int32_t today);

#endif
