#ifndef HABITS_CORE_STORE_H
#define HABITS_CORE_STORE_H

#include <stdbool.h>
#include <stddef.h>

#include "core/model.h"
#include "infra/util.h"

#define STORE_SCHEMA_VERSION 2

/* Load state from <dir>/habits.jsonl (falling back to .bak).
   A missing file yields an empty database. */
bool store_load(DB *db, const char *data_dir, char *err, size_t errsz);

/* Atomically rewrite <dir>/habits.jsonl, keeping the previous version as .bak. */
bool store_save(const DB *db, const char *data_dir, char *err, size_t errsz);

/* Serialize the whole database to JSONL text (no file I/O). */
void store_serialize(const DB *db, StrBuf *out);

#endif
