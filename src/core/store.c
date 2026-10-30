#include "core/store.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "infra/json.h"
#include "infra/util.h"

static void warnf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("habits: warning: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* ---- serialization ------------------------------------------------------- */

static void write_schedule(JsonW *w, const Habit *h) {
    jw_obj(w);
    jw_key(w, "kind");
    jw_str(w, sched_kind_name(h->kind));
    if (h->kind == SCHED_WEEKDAYS) {
        jw_key(w, "days");
        jw_arr(w);
        for (int i = 0; i < 7; i++)
            if ((h->days_mask >> i) & 1u) jw_int(w, i + 1);
        jw_arr_end(w);
    } else if (h->kind == SCHED_WEEKLY_COUNT) {
        jw_key(w, "times");
        jw_int(w, h->times);
    }
    jw_obj_end(w);
}

void store_serialize(const DB *db, StrBuf *out) {
    char nowbuf[32];
    now_iso8601(nowbuf);
    const char *created = db->created_at[0] ? db->created_at : nowbuf;

    JsonW w;

    jw_init(&w, out);
    jw_obj(&w);
    jw_key(&w, "type");
    jw_str(&w, "meta");
    jw_key(&w, "schema_version");
    jw_int(&w, STORE_SCHEMA_VERSION);
    jw_key(&w, "app");
    jw_str(&w, "habits");
    jw_key(&w, "created_at");
    jw_str(&w, created);
    jw_obj_end(&w);
    sb_addc(out, '\n');

    for (size_t i = 0; i < db->nhabits; i++) {
        const Habit *h = &db->habits[i];
        jw_init(&w, out);
        jw_obj(&w);
        jw_key(&w, "type");
        jw_str(&w, "habit");
        jw_key(&w, "id");
        jw_int(&w, h->id);
        jw_key(&w, "name");
        jw_str(&w, h->name ? h->name : "");
        jw_key(&w, "description");
        jw_str(&w, h->description ? h->description : "");
        jw_key(&w, "color");
        jw_str(&w, h->color ? h->color : "#4f8cff");
        jw_key(&w, "icon");
        jw_str(&w, h->icon ? h->icon : "dot");
        jw_key(&w, "category");
        if (h->category && h->category[0])
            jw_str(&w, h->category);
        else
            jw_null(&w);
        jw_key(&w, "schedule");
        write_schedule(&w, h);
        jw_key(&w, "start_date");
        jw_str(&w, h->start_date);
        jw_key(&w, "created_at");
        jw_str(&w, h->created_at);
        jw_key(&w, "archived_at");
        if (h->archived)
            jw_str(&w, h->archived_date);
        else
            jw_null(&w);
        jw_key(&w, "sort_order");
        jw_int(&w, h->sort_order);
        jw_key(&w, "value");
        jw_null(&w);
        jw_obj_end(&w);
        sb_addc(out, '\n');
    }

    for (size_t i = 0; i < db->nquits; i++) {
        const Quit *q = &db->quits[i];
        jw_init(&w, out);
        jw_obj(&w);
        jw_key(&w, "type");
        jw_str(&w, "quit");
        jw_key(&w, "id");
        jw_int(&w, q->id);
        jw_key(&w, "name");
        jw_str(&w, q->name ? q->name : "");
        jw_key(&w, "description");
        jw_str(&w, q->description ? q->description : "");
        jw_key(&w, "color");
        jw_str(&w, q->color ? q->color : "#ef4444");
        jw_key(&w, "icon");
        jw_str(&w, q->icon ? q->icon : "shield");
        jw_key(&w, "started_at");
        jw_str(&w, q->started_iso);
        jw_key(&w, "created_at");
        jw_str(&w, q->created_at);
        jw_key(&w, "best_seconds");
        jw_int(&w, q->best_seconds);
        jw_key(&w, "attempts");
        jw_int(&w, q->attempts);
        jw_key(&w, "archived_at");
        if (q->archived)
            jw_str(&w, q->archived_date);
        else
            jw_null(&w);
        jw_key(&w, "sort_order");
        jw_int(&w, q->sort_order);
        jw_obj_end(&w);
        sb_addc(out, '\n');
    }

    for (size_t i = 0; i < db->nentries; i++) {
        const Entry *e = &db->entries[i];
        char date[11];
        date_format(e->day, date);
        jw_init(&w, out);
        jw_obj(&w);
        jw_key(&w, "type");
        jw_str(&w, "entry");
        jw_key(&w, "habit_id");
        jw_int(&w, e->habit_id);
        jw_key(&w, "date");
        jw_str(&w, date);
        jw_key(&w, "status");
        jw_str(&w, e->status == ENTRY_DONE ? "done" : "skipped");
        jw_key(&w, "note");
        jw_str(&w, e->note ? e->note : "");
        jw_key(&w, "value");
        jw_null(&w);
        jw_key(&w, "updated_at");
        jw_str(&w, e->updated_at);
        jw_obj_end(&w);
        sb_addc(out, '\n');
    }
}

/* ---- parsing helpers ----------------------------------------------------- */

static const char *jstr(const JVal *o, const char *key) {
    return json_str(json_get(o, key));
}

static bool jint(const JVal *o, const char *key, long long *out) {
    return json_int(json_get(o, key), out);
}

static bool parse_schedule(const JVal *sch, Habit *h) {
    const char *kind = jstr(sch, "kind");
    if (!kind || !sched_kind_parse(kind, &h->kind)) return false;
    if (h->kind == SCHED_WEEKDAYS) {
        const JVal *days = json_get(sch, "days");
        if (!days || days->type != J_ARR || days->n == 0) return false;
        h->days_mask = 0;
        for (size_t i = 0; i < days->n; i++) {
            long long d = 0;
            if (!json_int(days->items[i], &d) || d < 1 || d > 7) return false;
            h->days_mask |= 1u << (int)(d - 1);
        }
    } else if (h->kind == SCHED_WEEKLY_COUNT) {
        long long t = 0;
        if (!jint(sch, "times", &t) || t < 1 || t > 7) return false;
        h->times = (int)t;
        h->days_mask = 0x7F;
    } else {
        h->days_mask = 0x7F;
    }
    return true;
}

static void set_schedule_defaults(Habit *h) {
    h->kind = SCHED_DAILY;
    h->days_mask = 0x7F;
    h->times = 7;
}

static char *dup_or(const char *s, const char *fallback) {
    return xstrdup(s && s[0] ? s : fallback);
}

static bool habit_from_json(const JVal *o, DB *db, Habit *h) {
    memset(h, 0, sizeof *h);
    set_schedule_defaults(h);
    long long id = 0;
    bool has_id = jint(o, "id", &id) && id > 0;

    const char *name = jstr(o, "name");
    if (!name) return false;
    h->name = xstrdup(name);
    h->description = dup_or(jstr(o, "description"), "");
    h->color = xstrdup(jstr(o, "color") && *jstr(o, "color") ? jstr(o, "color") : "#4f8cff");
    h->icon = xstrdup(jstr(o, "icon") && *jstr(o, "icon") ? jstr(o, "icon") : "dot");
    const char *cat = jstr(o, "category");
    h->category = (cat && cat[0]) ? xstrdup(cat) : NULL;

    const JVal *sch = json_get(o, "schedule");
    if (sch && sch->type == J_OBJ) {
        if (!parse_schedule(sch, h)) {
            habit_free(h);
            return false;
        }
    }

    const char *start = jstr(o, "start_date");
    int32_t start_day = 0;
    if (!start || !date_parse(start, &start_day)) {
        habit_free(h);
        return false;
    }
    h->start_day = start_day;
    snprintf(h->start_date, sizeof h->start_date, "%s", start);

    const char *created = jstr(o, "created_at");
    if (created && datetime_valid(created))
        snprintf(h->created_at, sizeof h->created_at, "%s", created);
    else
        now_iso8601(h->created_at);

    const char *arch = jstr(o, "archived_at");
    int32_t arch_day = 0;
    if (arch && date_parse(arch, &arch_day)) {
        h->archived = true;
        h->archived_day = arch_day;
        snprintf(h->archived_date, sizeof h->archived_date, "%s", arch);
    }

    long long sort = 0;
    h->sort_order = jint(o, "sort_order", &sort) ? (int)sort : 0;

    const char *code = NULL, *msg = NULL;
    if (!habit_validate(h, &code, &msg)) {
        warnf("skipping habit \"%s\": %s", h->name, msg ? msg : "invalid");
        habit_free(h);
        return false;
    }

    if (has_id && db_habit_by_id(db, (int64_t)id)) {
        warnf("duplicate habit id %lld, assigning a new one", id);
        has_id = false;
    }
    if (has_id)
        h->id = (int64_t)id;
    else
        h->id = db->next_habit_id;
    if (h->id >= db->next_habit_id) db->next_habit_id = h->id + 1;
    return true;
}

typedef struct {
    Entry e;
} PendingEntry;

static bool quit_from_json(const JVal *o, DB *db, Quit *q) {
    memset(q, 0, sizeof *q);
    q->attempts = 1;

    const char *name = jstr(o, "name");
    if (!name || !*name) return false;
    q->name = xstrdup(name);
    q->description = dup_or(jstr(o, "description"), "");
    const char *color = jstr(o, "color");
    q->color = xstrdup(color && *color ? color : "#ef4444");
    const char *icon = jstr(o, "icon");
    q->icon = xstrdup(icon && *icon ? icon : "shield");

    long long id = 0;
    bool has_id = jint(o, "id", &id) && id > 0;

    const char *started = jstr(o, "started_at");
    int64_t started_epoch = 0;
    if (started && datetime_parse_epoch(started, &started_epoch)) {
        q->started_at = started_epoch;
        iso_from_epoch(started_epoch, q->started_iso);
    } else {
        q->started_at = now_epoch();
        now_iso8601(q->started_iso);
    }

    const char *created = jstr(o, "created_at");
    if (created && datetime_valid(created))
        snprintf(q->created_at, sizeof q->created_at, "%s", created);
    else
        now_iso8601(q->created_at);

    long long best = 0;
    if (jint(o, "best_seconds", &best) && best > 0) q->best_seconds = best;
    long long attempts = 1;
    if (jint(o, "attempts", &attempts) && attempts >= 1) q->attempts = (int)attempts;

    const char *arch = jstr(o, "archived_at");
    int32_t arch_day = 0;
    if (arch && date_parse(arch, &arch_day)) {
        q->archived = true;
        q->archived_day = arch_day;
        snprintf(q->archived_date, sizeof q->archived_date, "%s", arch);
    }

    long long sort = 0;
    q->sort_order = jint(o, "sort_order", &sort) ? (int)sort : 0;

    const char *code = NULL, *msg = NULL;
    if (!quit_validate(q, now_epoch(), &code, &msg)) {
        warnf("skipping bad habit \"%s\": %s", q->name, msg ? msg : "invalid");
        quit_free(q);
        return false;
    }

    if (has_id && db_quit_by_id(db, (int64_t)id)) {
        warnf("duplicate quit id %lld, assigning a new one", id);
        has_id = false;
    }
    if (has_id)
        q->id = (int64_t)id;
    else
        q->id = db->next_quit_id;
    if (q->id >= db->next_quit_id) db->next_quit_id = q->id + 1;
    return true;
}

/* ---- load ---------------------------------------------------------------- */

static void backup_corrupt(const char *path) {
    char stamp[32];
    now_iso8601(stamp);
    for (char *p = stamp; *p; p++)
        if (*p == ':' || *p == '+' || *p == 'T') *p = '-';
    char dst[4200];
    snprintf(dst, sizeof dst, "%s.corrupt-%s", path, stamp);
    if (file_copy(path, dst))
        warnf("corrupted source preserved as %s", dst);
}

static int parse_lines(DB *db, PendingEntry **pending, size_t *npending, const char *text,
                       size_t len, bool *unsupported_schema) {
    size_t pos = 0;
    int anomalies = 0;
    int lineno = 0;
    bool seen_meta = false;
    *pending = NULL;
    *npending = 0;
    size_t cap = 0;

    while (pos < len) {
        const char *line = text + pos;
        const char *nl = memchr(line, '\n', len - pos);
        size_t line_len = nl ? (size_t)(nl - line) : len - pos;
        pos += line_len + (nl ? 1 : 0);
        lineno++;
        if (line_len && line[line_len - 1] == '\r') line_len--;
        if (line_len == 0) continue;

        char err[128] = "";
        JVal *v = json_parse(line, line_len, err, sizeof err);
        if (!v) {
            warnf("line %d: %s", lineno, err[0] ? err : "invalid JSON");
            anomalies++;
            continue;
        }
        const char *type = jstr(v, "type");
        if (!type) {
            warnf("line %d: missing type", lineno);
            anomalies++;
            json_free(v);
            continue;
        }
        if (strcmp(type, "meta") == 0) {
            long long ver = STORE_SCHEMA_VERSION;
            if (jint(v, "schema_version", &ver) && ver > STORE_SCHEMA_VERSION) {
                *unsupported_schema = true;
                json_free(v);
                return -1;
            }
            const char *created = jstr(v, "created_at");
            if (created && datetime_valid(created))
                snprintf(db->created_at, sizeof db->created_at, "%s", created);
            seen_meta = true;
        } else if (strcmp(type, "habit") == 0) {
            Habit h;
            if (!habit_from_json(v, db, &h)) {
                warnf("line %d: skipped invalid habit", lineno);
                anomalies++;
                json_free(v);
                continue;
            }
            if (db->nhabits == db->chabits) {
                db->chabits = db->chabits ? db->chabits * 2 : 8;
                db->habits = xrealloc(db->habits, db->chabits * sizeof *db->habits);
            }
            db->habits[db->nhabits++] = h;
            if (h.sort_order > db->max_sort_order) db->max_sort_order = h.sort_order;
        } else if (strcmp(type, "quit") == 0) {
            Quit q;
            if (!quit_from_json(v, db, &q)) {
                warnf("line %d: skipped invalid bad habit", lineno);
                anomalies++;
                json_free(v);
                continue;
            }
            if (db->nquits == db->cquits) {
                db->cquits = db->cquits ? db->cquits * 2 : 8;
                db->quits = xrealloc(db->quits, db->cquits * sizeof *db->quits);
            }
            db->quits[db->nquits++] = q;
            if (q.sort_order > db->max_quit_sort) db->max_quit_sort = q.sort_order;
        } else if (strcmp(type, "entry") == 0) {
            long long hid = 0;
            const char *date = jstr(v, "date");
            const char *status = jstr(v, "status");
            int32_t day = 0;
            if (!jint(v, "habit_id", &hid) || hid <= 0 || !date || !date_parse(date, &day) ||
                !status) {
                warnf("line %d: malformed entry", lineno);
                anomalies++;
                json_free(v);
                continue;
            }
            EntryStatus st;
            if (strcmp(status, "done") == 0)
                st = ENTRY_DONE;
            else if (strcmp(status, "skipped") == 0)
                st = ENTRY_SKIPPED;
            else {
                warnf("line %d: bad entry status \"%s\"", lineno, status);
                anomalies++;
                json_free(v);
                continue;
            }
            if (*npending == cap) {
                cap = cap ? cap * 2 : 16;
                *pending = xrealloc(*pending, cap * sizeof **pending);
            }
            Entry *e = &(*pending)[(*npending)++].e;
            memset(e, 0, sizeof *e);
            e->habit_id = hid;
            e->day = day;
            e->status = st;
            e->note = xstrdup(jstr(v, "note") ? jstr(v, "note") : "");
            const char *upd = jstr(v, "updated_at");
            if (upd && datetime_valid(upd))
                snprintf(e->updated_at, sizeof e->updated_at, "%s", upd);
            else
                now_iso8601(e->updated_at);
        } else {
            warnf("line %d: unknown record type \"%s\" (kept as is)", lineno, type);
        }
        json_free(v);
    }

    if (!seen_meta) {
        now_iso8601(db->created_at);
    }
    return anomalies;
}

bool store_load(DB *db, const char *data_dir, char *err, size_t errsz) {
    db_init(db);
    char *path = path_join(data_dir, "habits.jsonl");
    char *bak = path_join(data_dir, "habits.jsonl.bak");
    char *text = NULL;
    size_t len = 0;
    bool from_bak = false;

    if (!file_read_all(path, &text, &len)) {
        if (file_read_all(bak, &text, &len)) {
            from_bak = true;
            warnf("main data file missing, restored from %s", bak);
        } else {
            now_iso8601(db->created_at);
            free(path);
            free(bak);
            return true; /* first run */
        }
    }

    PendingEntry *pending = NULL;
    size_t npending = 0;
    bool unsupported = false;
    int anomalies = parse_lines(db, &pending, &npending, text, len, &unsupported);

    if (unsupported) {
        snprintf(err, errsz,
                 "data file schema_version is newer than supported (%d); refusing to start",
                 STORE_SCHEMA_VERSION);
        for (size_t i = 0; i < npending; i++) free(pending[i].e.note);
        free(pending);
        free(text);
        free(path);
        free(bak);
        db_free(db);
        return false;
    }

    size_t dropped = 0;
    for (size_t i = 0; i < npending; i++) {
        Entry *src = &pending[i].e;
        if (!db_habit_by_id(db, src->habit_id)) {
            dropped++;
            free(src->note);
            continue;
        }
        Entry *dst = db_entry_find(db, src->habit_id, src->day);
        if (dst) {
            char dbuf[11];
            date_format(src->day, dbuf);
            warnf("duplicate entry for habit %lld on %s, keeping the last one",
                  (long long)src->habit_id, dbuf);
        }
        db_entry_upsert(db, src->habit_id, src->day, src->status, src->note, src->updated_at);
        free(src->note);
    }
    free(pending);
    if (dropped)
        warnf("%zu entries referenced unknown habits and were dropped", dropped);

    if (anomalies > 0) {
        warnf("%d malformed lines skipped; the file will be rewritten on the next save",
              anomalies);
        backup_corrupt(path);
    }
    if (from_bak) warnf("working from the backup copy; it will be rewritten on the next save");

    free(text);
    free(path);
    free(bak);
    return true;
}

/* ---- save ---------------------------------------------------------------- */

bool store_save(const DB *db, const char *data_dir, char *err, size_t errsz) {
    if (mkdir_p(data_dir) != 0) {
        snprintf(err, errsz, "cannot create data directory %s: %s", data_dir, strerror(errno));
        return false;
    }
    char *path = path_join(data_dir, "habits.jsonl");
    char *bak = path_join(data_dir, "habits.jsonl.bak");

    StrBuf sb;
    sb_init(&sb);
    store_serialize(db, &sb);
    if (sb.len == 0 || sb.data[sb.len - 1] != '\n') sb_addc(&sb, '\n');

    struct stat st;
    if (stat(path, &st) == 0) file_copy(path, bak);

    bool ok = file_write_atomic(path, sb.data, sb.len);
    if (!ok) snprintf(err, errsz, "cannot write %s: %s", path, strerror(errno));
    sb_free(&sb);
    free(path);
    free(bak);
    return ok;
}
