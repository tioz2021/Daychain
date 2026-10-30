#include "api/api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/schedule.h"
#include "core/stats.h"
#include "core/store.h"
#include "infra/json.h"
#include "infra/util.h"
#include "infra/web.h"

typedef struct {
    const char *code;
    const char *message;
    const char *field;
} ApiErr;

static int item_cmp(const void *a, const void *b);

static void err_set(ApiErr *e, const char *code, const char *message, const char *field) {
    e->code = code;
    e->message = message;
    e->field = field;
}

static void send_error(HttpResponse *res, int status, const char *code, const char *message,
                       const char *field) {
    StrBuf *out = &res->body;
    sb_reset(out);
    JsonW w;
    jw_init(&w, out);
    jw_obj(&w);
    jw_key(&w, "error");
    jw_obj(&w);
    jw_key(&w, "code");
    jw_str(&w, code);
    jw_key(&w, "message");
    jw_str(&w, message ? message : code);
    if (field) {
        jw_key(&w, "field");
        jw_str(&w, field);
    }
    jw_obj_end(&w);
    jw_obj_end(&w);
    res->status = status;
    snprintf(res->content_type, sizeof res->content_type, "application/json; charset=utf-8");
}

static void send_text(HttpResponse *res, int status, const char *text) {
    sb_reset(&res->body);
    sb_adds(&res->body, text);
    res->status = status;
    snprintf(res->content_type, sizeof res->content_type, "text/plain; charset=utf-8");
}

/* ---- security ------------------------------------------------------------ */

static bool host_ok(const char *h) {
    if (!h || !*h) return false;
    if (strncmp(h, "127.0.0.1", 9) == 0) return h[9] == '\0' || h[9] == ':';
    if (strncmp(h, "localhost", 9) == 0) return h[9] == '\0' || h[9] == ':';
    return false;
}

static bool origin_ok(const char *o) {
    return strncmp(o, "http://127.0.0.1", 16) == 0 || strncmp(o, "http://localhost", 16) == 0;
}

static bool check_security(const HttpRequest *req, HttpResponse *res) {
    if (!host_ok(req->host)) {
        send_error(res, 403, "bad_host", "unexpected Host header", NULL);
        return false;
    }
    const char *m = req->method;
    if (strcmp(m, "GET") != 0 && strcmp(m, "HEAD") != 0) {
        if (strcmp(req->requested_with, "habits") != 0) {
            send_error(res, 403, "forbidden", "missing X-Requested-With: habits", NULL);
            return false;
        }
        if (req->origin[0] && !origin_ok(req->origin)) {
            send_error(res, 403, "forbidden", "cross-origin request rejected", NULL);
            return false;
        }
    }
    return true;
}

/* ---- query / path helpers ------------------------------------------------ */

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool url_decode(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; in[i]; i++) {
        char c = in[i];
        if (c == '%') {
            int hi = hex_val(in[i + 1]);
            int lo = hi < 0 ? -1 : hex_val(in[i + 2]);
            if (hi < 0 || lo < 0) return false;
            c = (char)((hi << 4) | lo);
            i += 2;
        } else if (c == '+') {
            c = ' ';
        }
        if (o + 1 >= cap) return false;
        out[o++] = c;
    }
    out[o] = '\0';
    return true;
}

static bool query_get(const char *query, const char *key, char *out, size_t cap) {
    size_t klen = strlen(key);
    const char *p = query;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t seg = amp ? (size_t)(amp - p) : strlen(p);
        if (seg > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            char raw[512];
            size_t n = seg - klen - 1;
            if (n >= sizeof raw) n = sizeof raw - 1;
            memcpy(raw, p + klen + 1, n);
            raw[n] = '\0';
            return url_decode(raw, out, cap);
        }
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

static bool parse_id(const char *s, int64_t *out) {
    if (!s || !*s) return false;
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (!end || *end != '\0' || v <= 0) return false;
    *out = v;
    return true;
}

/* ---- JSON serializers ---------------------------------------------------- */

static void schedule_to_json(JsonW *w, const Habit *h) {
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

static void habit_to_json(JsonW *w, const Habit *h) {
    jw_obj(w);
    jw_key(w, "type");
    jw_str(w, "habit");
    jw_key(w, "id");
    jw_int(w, h->id);
    jw_key(w, "name");
    jw_str(w, h->name);
    jw_key(w, "description");
    jw_str(w, h->description ? h->description : "");
    jw_key(w, "color");
    jw_str(w, h->color);
    jw_key(w, "icon");
    jw_str(w, h->icon);
    jw_key(w, "category");
    if (h->category)
        jw_str(w, h->category);
    else
        jw_null(w);
    jw_key(w, "schedule");
    schedule_to_json(w, h);
    jw_key(w, "start_date");
    jw_str(w, h->start_date);
    jw_key(w, "created_at");
    jw_str(w, h->created_at);
    jw_key(w, "archived_at");
    if (h->archived)
        jw_str(w, h->archived_date);
    else
        jw_null(w);
    jw_key(w, "sort_order");
    jw_int(w, h->sort_order);
    jw_key(w, "value");
    jw_null(w);
    jw_obj_end(w);
}

static void entry_to_json(JsonW *w, const Entry *e) {
    char date[11];
    date_format(e->day, date);
    jw_obj(w);
    jw_key(w, "type");
    jw_str(w, "entry");
    jw_key(w, "habit_id");
    jw_int(w, e->habit_id);
    jw_key(w, "date");
    jw_str(w, date);
    jw_key(w, "status");
    jw_str(w, e->status == ENTRY_DONE ? "done" : "skipped");
    jw_key(w, "note");
    jw_str(w, e->note ? e->note : "");
    jw_key(w, "value");
    jw_null(w);
    jw_key(w, "updated_at");
    jw_str(w, e->updated_at);
    jw_obj_end(w);
}

static void streak_to_json(JsonW *w, const Streak *s) {
    jw_obj(w);
    jw_key(w, "current");
    jw_int(w, s->current);
    jw_key(w, "best");
    jw_int(w, s->best);
    jw_key(w, "unit");
    jw_str(w, s->unit == STATS_UNIT_WEEKS ? "weeks" : "days");
    jw_obj_end(w);
}

static void rate_to_json(JsonW *w, const Rate *r) {
    jw_obj(w);
    jw_key(w, "done");
    jw_int(w, r->done);
    jw_key(w, "planned");
    jw_int(w, r->planned);
    jw_key(w, "skipped");
    jw_int(w, r->skipped);
    jw_key(w, "has");
    jw_bool(w, r->has);
    jw_key(w, "percent");
    if (r->has)
        jw_int(w, r->percent);
    else
        jw_null(w);
    jw_obj_end(w);
}


static void quit_to_json(JsonW *w, const Quit *q, int64_t now) {
    int64_t elapsed = now > q->started_at ? now - q->started_at : 0;
    int64_t best = q->best_seconds > elapsed ? q->best_seconds : elapsed;
    jw_obj(w);
    jw_key(w, "type");
    jw_str(w, "quit");
    jw_key(w, "id");
    jw_int(w, q->id);
    jw_key(w, "name");
    jw_str(w, q->name);
    jw_key(w, "description");
    jw_str(w, q->description ? q->description : "");
    jw_key(w, "color");
    jw_str(w, q->color);
    jw_key(w, "icon");
    jw_str(w, q->icon);
    jw_key(w, "started_at");
    jw_str(w, q->started_iso);
    jw_key(w, "created_at");
    jw_str(w, q->created_at);
    jw_key(w, "elapsed_seconds");
    jw_int(w, elapsed);
    jw_key(w, "best_seconds");
    jw_int(w, best);
    jw_key(w, "attempts");
    jw_int(w, q->attempts);
    jw_key(w, "archived_at");
    if (q->archived)
        jw_str(w, q->archived_date);
    else
        jw_null(w);
    jw_key(w, "sort_order");
    jw_int(w, q->sort_order);
    jw_obj_end(w);
}

/* ---- validation helpers -------------------------------------------------- */

static const char *field_for_code(const char *code) {
    if (strcmp(code, "invalid_name") == 0) return "name";
    if (strcmp(code, "invalid_description") == 0) return "description";
    if (strcmp(code, "invalid_color") == 0) return "color";
    if (strcmp(code, "invalid_icon") == 0) return "icon";
    if (strcmp(code, "invalid_category") == 0) return "category";
    if (strncmp(code, "invalid_schedule", 16) == 0) return "schedule";
    return NULL;
}

static void trim_copy(char *dst, size_t cap, const char *src) {
    while (*src == ' ' || *src == '\t') src++;
    size_t n = strlen(src);
    while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\t')) n--;
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool parse_schedule_obj(const JVal *o, Habit *h, ApiErr *e) {
    if (!o || o->type != J_OBJ) {
        err_set(e, "invalid_schedule_kind", "schedule must be an object", "schedule");
        return false;
    }
    const char *kind = json_str(json_get(o, "kind"));
    if (!kind || !sched_kind_parse(kind, &h->kind)) {
        err_set(e, "invalid_schedule_kind", "schedule.kind must be daily, weekdays or weekly_count",
                "schedule");
        return false;
    }
    if (h->kind == SCHED_WEEKDAYS) {
        const JVal *days = json_get(o, "days");
        if (!days || days->type != J_ARR || days->n == 0) {
            err_set(e, "invalid_schedule_days", "schedule.days must be a non-empty array", "schedule");
            return false;
        }
        h->days_mask = 0;
        for (size_t i = 0; i < days->n; i++) {
            long long d = 0;
            if (!json_int(days->items[i], &d) || d < 1 || d > 7) {
                err_set(e, "invalid_schedule_days", "schedule.days values must be 1..7", "schedule");
                return false;
            }
            h->days_mask |= 1u << (int)(d - 1);
        }
    } else if (h->kind == SCHED_WEEKLY_COUNT) {
        long long t = 0;
        if (!json_int(json_get(o, "times"), &t) || t < 1 || t > 7) {
            err_set(e, "invalid_schedule_times", "schedule.times must be 1..7", "schedule");
            return false;
        }
        h->times = (int)t;
    }
    return true;
}

static void habit_defaults(Habit *h) {
    h->kind = SCHED_DAILY;
    h->days_mask = 0x7F;
    h->times = 7;
    h->color = xstrdup("#4f8cff");
    h->icon = xstrdup("dot");
    h->description = xstrdup("");
}

static void habit_free_partial(Habit *h) {
    free(h->name);
    free(h->description);
    free(h->color);
    free(h->icon);
    free(h->category);
    memset(h, 0, sizeof *h);
}

static JVal *parse_body(const HttpRequest *req, ApiErr *e) {
    char errbuf[160] = "";
    JVal *v = json_parse(req->body ? req->body : "", req->body ? req->body_len : 0, errbuf,
                         sizeof errbuf);
    if (!v) {
        err_set(e, "invalid_json", errbuf[0] ? errbuf : "invalid JSON body", NULL);
        return NULL;
    }
    if (v->type != J_OBJ) {
        json_free(v);
        err_set(e, "invalid_json", "request body must be a JSON object", NULL);
        return NULL;
    }
    return v;
}

static bool build_new_habit(const JVal *o, int32_t today, Habit *h, ApiErr *e) {
    memset(h, 0, sizeof *h);
    habit_defaults(h);

    const char *name = json_str(json_get(o, "name"));
    if (!name) {
        err_set(e, "invalid_name", "name is required", "name");
        return false;
    }
    char namebuf[512];
    trim_copy(namebuf, sizeof namebuf, name);
    h->name = xstrdup(namebuf);

    const JVal *v;
    if ((v = json_get(o, "description"))) {
        const char *s = json_str(v);
        if (!s) {
            err_set(e, "invalid_description", "description must be a string", "description");
            return false;
        }
        free(h->description);
        h->description = xstrdup(s);
    }
    if ((v = json_get(o, "color"))) {
        const char *s = json_str(v);
        if (!s) {
            err_set(e, "invalid_color", "color must be a string", "color");
            return false;
        }
        free(h->color);
        h->color = xstrdup(s);
    }
    if ((v = json_get(o, "icon"))) {
        const char *s = json_str(v);
        if (!s) {
            err_set(e, "invalid_icon", "icon must be a string", "icon");
            return false;
        }
        free(h->icon);
        h->icon = xstrdup(s);
    }
    if ((v = json_get(o, "category"))) {
        const char *s = json_str(v);
        if (s) h->category = xstrdup(s);
    }
    if ((v = json_get(o, "schedule"))) {
        if (!parse_schedule_obj(v, h, e)) return false;
    }

    const char *start = json_str(json_get(o, "start_date"));
    if (start) {
        if (!date_parse(start, &h->start_day)) {
            err_set(e, "invalid_date", "start_date must be YYYY-MM-DD", "start_date");
            return false;
        }
    } else {
        h->start_day = today;
    }
    if (h->start_day > today) {
        err_set(e, "date_in_future", "start_date cannot be in the future", "start_date");
        return false;
    }
    date_format(h->start_day, h->start_date);
    now_iso8601(h->created_at);

    long long sort = 0;
    h->sort_order = 0;
    if (json_get(o, "sort_order")) {
        if (!json_int(json_get(o, "sort_order"), &sort) || sort < 0) {
            err_set(e, "invalid_sort_order", "sort_order must be a non-negative integer",
                    "sort_order");
            return false;
        }
        h->sort_order = (int)sort;
    }

    const char *code = NULL, *msg = NULL;
    if (!habit_validate(h, &code, &msg)) {
        err_set(e, code, msg, field_for_code(code));
        return false;
    }
    return true;
}

/* ---- saving -------------------------------------------------------------- */

static bool save_or_500(Server *s, HttpResponse *res) {
    char err[256] = "";
    if (store_save(s->db, s->data_dir, err, sizeof err)) return true;
    fprintf(stderr, "habits: error: %s\n", err);
    DB fresh;
    char lerr[256] = "";
    if (store_load(&fresh, s->data_dir, lerr, sizeof lerr)) {
        db_free(s->db);
        *s->db = fresh;
    }
    send_error(res, 500, "internal", "cannot write the data file", NULL);
    return false;
}

/* ---- handlers ------------------------------------------------------------ */

static void handle_health(const Server *s, HttpResponse *res) {
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    jw_obj(&w);
    jw_key(&w, "status");
    jw_str(&w, "ok");
    jw_key(&w, "version");
    jw_str(&w, s->version);
    jw_key(&w, "data_dir");
    jw_str(&w, s->data_dir ? s->data_dir : "");
    jw_obj_end(&w);
    res->status = 200;
}

static void handle_habits_list(const Server *s, const HttpRequest *req, HttpResponse *res) {
    bool include_archived = false;
    char val[16];
    if (query_get(req->query, "archived", val, sizeof val))
        include_archived = strcmp(val, "1") == 0 || strcmp(val, "true") == 0;

    const Habit **items = xmalloc((s->db->nhabits + 1) * sizeof *items);
    size_t n = 0;
    for (size_t i = 0; i < s->db->nhabits; i++) {
        const Habit *h = &s->db->habits[i];
        if (h->archived && !include_archived) continue;
        items[n++] = h;
    }
    qsort(items, n, sizeof *items, item_cmp);

    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    jw_obj(&w);
    jw_key(&w, "habits");
    jw_arr(&w);
    for (size_t i = 0; i < n; i++) habit_to_json(&w, items[i]);
    jw_arr_end(&w);
    jw_obj_end(&w);
    free(items);
    res->status = 200;
}

static void handle_habit_get(const Server *s, int64_t id, HttpResponse *res) {
    const Habit *h = db_habit_by_id_c(s->db, id);
    if (!h) {
        send_error(res, 404, "habit_not_found", "habit not found", NULL);
        return;
    }
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    habit_to_json(&w, h);
    res->status = 200;
}

static void handle_habit_create(Server *s, const HttpRequest *req, HttpResponse *res) {
    ApiErr e = {0};
    JVal *body = parse_body(req, &e);
    if (!body) {
        send_error(res, 400, e.code, e.message, e.field);
        return;
    }
    Habit h;
    int32_t today = date_today();
    if (!build_new_habit(body, today, &h, &e)) {
        json_free(body);
        habit_free_partial(&h);
        send_error(res, 400, e.code, e.message, e.field);
        return;
    }
    json_free(body);
    if (!habit_schedule_valid(&h, &e.code, &e.message)) {
        habit_free_partial(&h);
        send_error(res, 400, e.code, e.message, "schedule");
        return;
    }

    Habit *slot = db_habit_add(s->db);
    int64_t assigned = slot->id;
    h.id = assigned;
    if (h.sort_order == 0) h.sort_order = slot->sort_order;
    s->db->max_sort_order = h.sort_order > s->db->max_sort_order ? h.sort_order
                                                                 : s->db->max_sort_order;
    *slot = h;

    if (!save_or_500(s, res)) {
        db_habit_remove(s->db, assigned);
        return;
    }
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    habit_to_json(&w, db_habit_by_id_c(s->db, assigned));
    res->status = 201;
}

static void handle_habit_patch(Server *s, int64_t id, const HttpRequest *req, HttpResponse *res) {
    Habit *h = db_habit_by_id(s->db, id);
    if (!h) {
        send_error(res, 404, "habit_not_found", "habit not found", NULL);
        return;
    }
    ApiErr e = {0};
    JVal *body = parse_body(req, &e);
    if (!body) {
        send_error(res, 400, e.code, e.message, e.field);
        return;
    }

    Habit t = *h;
    char namebuf[512];
    const char *new_name = NULL, *new_desc = NULL, *new_color = NULL, *new_icon = NULL;
    const char *new_cat = NULL;
    bool cat_set = false, start_set = false;
    int32_t new_start = h->start_day;
    int new_sort = h->sort_order;

    const JVal *v;
    if ((v = json_get(body, "name"))) {
        const char *sval = json_str(v);
        if (!sval) {
            err_set(&e, "invalid_name", "name must be a string", "name");
            goto bad;
        }
        trim_copy(namebuf, sizeof namebuf, sval);
        new_name = namebuf;
        t.name = namebuf;
    }
    if ((v = json_get(body, "description"))) {
        const char *sval = json_str(v);
        if (!sval) {
            err_set(&e, "invalid_description", "description must be a string", "description");
            goto bad;
        }
        new_desc = sval;
        t.description = (char *)sval;
    }
    if ((v = json_get(body, "color"))) {
        const char *sval = json_str(v);
        if (!sval) {
            err_set(&e, "invalid_color", "color must be a string", "color");
            goto bad;
        }
        new_color = sval;
        t.color = (char *)sval;
    }
    if ((v = json_get(body, "icon"))) {
        const char *sval = json_str(v);
        if (!sval) {
            err_set(&e, "invalid_icon", "icon must be a string", "icon");
            goto bad;
        }
        new_icon = sval;
        t.icon = (char *)sval;
    }
    if ((v = json_get(body, "category"))) {
        cat_set = true;
        new_cat = json_str(v);
        t.category = (char *)new_cat;
    }
    if ((v = json_get(body, "schedule"))) {
        if (!parse_schedule_obj(v, &t, &e)) goto bad;
    }
    if ((v = json_get(body, "start_date"))) {
        const char *sval = json_str(v);
        if (!sval || !date_parse(sval, &new_start)) {
            err_set(&e, "invalid_date", "start_date must be YYYY-MM-DD", "start_date");
            goto bad;
        }
        if (new_start > date_today()) {
            err_set(&e, "date_in_future", "start_date cannot be in the future", "start_date");
            goto bad;
        }
        start_set = true;
        t.start_day = new_start;
    }
    if ((v = json_get(body, "sort_order"))) {
        long long sv = 0;
        if (!json_int(v, &sv) || sv < 0) {
            err_set(&e, "invalid_sort_order", "sort_order must be a non-negative integer",
                    "sort_order");
            goto bad;
        }
        new_sort = (int)sv;
    }

    {
        const char *code = NULL, *msg = NULL;
        if (!habit_validate(&t, &code, &msg)) {
            err_set(&e, code, msg, field_for_code(code));
            goto bad;
        }
    }

    if (new_name) {
        free(h->name);
        h->name = xstrdup(new_name);
    }
    if (new_desc) {
        free(h->description);
        h->description = xstrdup(new_desc);
    }
    if (new_color) {
        free(h->color);
        h->color = xstrdup(new_color);
    }
    if (new_icon) {
        free(h->icon);
        h->icon = xstrdup(new_icon);
    }
    if (cat_set) {
        free(h->category);
        h->category = new_cat ? xstrdup(new_cat) : NULL;
    }
    if (start_set) {
        h->start_day = new_start;
        date_format(new_start, h->start_date);
    }
    if (json_get(body, "schedule")) {
        h->kind = t.kind;
        h->days_mask = t.days_mask;
        h->times = t.times;
    }
    h->sort_order = new_sort;
    if (h->sort_order > s->db->max_sort_order) s->db->max_sort_order = h->sort_order;
    json_free(body);

    if (!save_or_500(s, res)) return;
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    habit_to_json(&w, h);
    res->status = 200;
    return;

bad:
    json_free(body);
    send_error(res, 400, e.code, e.message, e.field);
}

static void handle_habit_delete(Server *s, int64_t id, HttpResponse *res) {
    if (!db_habit_by_id(s->db, id)) {
        send_error(res, 404, "habit_not_found", "habit not found", NULL);
        return;
    }
    db_habit_remove(s->db, id);
    if (!save_or_500(s, res)) return;
    res->status = 204;
    sb_reset(&res->body);
}

static void handle_archive(Server *s, int64_t id, bool arch, HttpResponse *res) {
    Habit *h = db_habit_by_id(s->db, id);
    if (!h) {
        send_error(res, 404, "habit_not_found", "habit not found", NULL);
        return;
    }
    if (arch) {
        h->archived = true;
        h->archived_day = date_today();
        date_format(h->archived_day, h->archived_date);
    } else {
        h->archived = false;
        h->archived_date[0] = '\0';
    }
    if (!save_or_500(s, res)) return;
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    habit_to_json(&w, h);
    res->status = 200;
}

static void handle_entries_list(const Server *s, int64_t id, const HttpRequest *req,
                                HttpResponse *res) {
    const Habit *h = db_habit_by_id_c(s->db, id);
    if (!h) {
        send_error(res, 404, "habit_not_found", "habit not found", NULL);
        return;
    }
    int32_t today = date_today();
    int32_t from = h->start_day, to = today;
    char buf[16];
    if (query_get(req->query, "from", buf, sizeof buf) && !date_parse(buf, &from)) {
        send_error(res, 400, "invalid_date", "from must be YYYY-MM-DD", "from");
        return;
    }
    if (query_get(req->query, "to", buf, sizeof buf) && !date_parse(buf, &to)) {
        send_error(res, 400, "invalid_date", "to must be YYYY-MM-DD", "to");
        return;
    }
    if (to > today) to = today;
    if (from > to) {
        from = to;
    }

    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    jw_obj(&w);
    jw_key(&w, "entries");
    jw_arr(&w);
    size_t i = db_entry_lower_bound(s->db, id, from);
    size_t end = db_entry_lower_bound(s->db, id, to + 1);
    for (; i < end; i++) entry_to_json(&w, &s->db->entries[i]);
    jw_arr_end(&w);
    jw_obj_end(&w);
    res->status = 200;
}

static void handle_entry_put(Server *s, int64_t id, const char *date, const HttpRequest *req,
                             HttpResponse *res) {
    Habit *h = db_habit_by_id(s->db, id);
    if (!h) {
        send_error(res, 404, "habit_not_found", "habit not found", NULL);
        return;
    }
    int32_t day = 0;
    if (!date_parse(date, &day)) {
        send_error(res, 400, "invalid_date", "date must be YYYY-MM-DD", "date");
        return;
    }
    if (h->archived && day > h->archived_day) {
        send_error(res, 400, "date_after_archive", "habit is archived", "date");
        return;
    }
    ApiErr e = {0};
    JVal *body = parse_body(req, &e);
    if (!body) {
        send_error(res, 400, e.code, e.message, e.field);
        return;
    }
    const char *status = json_str(json_get(body, "status"));
    int st = -1;
    if (status && strcmp(status, "done") == 0)
        st = ENTRY_DONE;
    else if (status && strcmp(status, "skipped") == 0)
        st = ENTRY_SKIPPED;
    const char *note = json_str(json_get(body, "note"));
    if (json_get(body, "note") && !note) {
        json_free(body);
        send_error(res, 400, "invalid_note", "note must be a string", "note");
        return;
    }
    const char *code = NULL, *msg = NULL;
    int32_t today = date_today();
    if (st < 0) {
        code = "invalid_status";
        msg = "status must be done or skipped";
    } else if (!entry_validate(day, st, note, h->start_day, today, &code, &msg)) {
        /* code/msg set by validator */
    }
    if (code) {
        json_free(body);
        send_error(res, 400, code, msg, "status");
        return;
    }
    char nowbuf[32];
    now_iso8601(nowbuf);
    Entry *entry = db_entry_upsert(s->db, id, day, (EntryStatus)st, note, nowbuf);
    json_free(body);
    if (!save_or_500(s, res)) return;
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    entry_to_json(&w, entry);
    res->status = 200;
}

static void handle_entry_delete(Server *s, int64_t id, const char *date, HttpResponse *res) {
    int32_t day = 0;
    if (!date_parse(date, &day)) {
        send_error(res, 400, "invalid_date", "date must be YYYY-MM-DD", "date");
        return;
    }
    if (!db_entry_remove(s->db, id, day)) {
        send_error(res, 404, "entry_not_found", "entry not found", NULL);
        return;
    }
    if (!save_or_500(s, res)) return;
    res->status = 204;
    sb_reset(&res->body);
}


static int quit_cmp(const void *a, const void *b) {
    const Quit *qa = *(const Quit *const *)a;
    const Quit *qb = *(const Quit *const *)b;
    if (qa->sort_order != qb->sort_order) return qa->sort_order < qb->sort_order ? -1 : 1;
    return qa->id < qb->id ? -1 : (qa->id > qb->id ? 1 : 0);
}

static void handle_quits_list(const Server *s, const HttpRequest *req, HttpResponse *res) {
    bool include_archived = false;
    char val[16];
    if (query_get(req->query, "archived", val, sizeof val))
        include_archived = strcmp(val, "1") == 0 || strcmp(val, "true") == 0;

    const Quit **items = xmalloc((s->db->nquits + 1) * sizeof *items);
    size_t n = 0;
    for (size_t i = 0; i < s->db->nquits; i++) {
        const Quit *q = &s->db->quits[i];
        if (q->archived && !include_archived) continue;
        items[n++] = q;
    }
    qsort(items, n, sizeof *items, quit_cmp);

    int64_t now = now_epoch();
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    jw_obj(&w);
    jw_key(&w, "quits");
    jw_arr(&w);
    for (size_t i = 0; i < n; i++) quit_to_json(&w, items[i], now);
    jw_arr_end(&w);
    jw_obj_end(&w);
    free(items);
    res->status = 200;
}

static bool parse_started_at(const JVal *o, int64_t now, int64_t *out, char iso[32], ApiErr *e) {
    const JVal *v = json_get(o, "started_at");
    if (!v) {
        *out = now;
        iso_from_epoch(now, iso);
        return true;
    }
    const char *s = json_str(v);
    if (!s || !datetime_parse_epoch(s, out)) {
        err_set(e, "invalid_started_at", "started_at must be an ISO 8601 date-time", "started_at");
        return false;
    }
    if (*out > now) {
        err_set(e, "invalid_started_at", "started_at cannot be in the future", "started_at");
        return false;
    }
    snprintf(iso, 32, "%s", s);
    return true;
}

static void handle_quit_create(Server *s, const HttpRequest *req, HttpResponse *res) {
    ApiErr e = {0};
    JVal *body = parse_body(req, &e);
    if (!body) {
        send_error(res, 400, e.code, e.message, e.field);
        return;
    }
    int64_t now = now_epoch();
    Quit q;
    memset(&q, 0, sizeof q);
    q.attempts = 1;
    q.description = xstrdup("");
    q.color = xstrdup("#ef4444");
    q.icon = xstrdup("shield");

    const char *name = json_str(json_get(body, "name"));
    if (!name) {
        err_set(&e, "invalid_name", "name is required", "name");
        json_free(body);
        quit_free(&q);
        send_error(res, 400, e.code, e.message, e.field);
        return;
    }
    char namebuf[512];
    trim_copy(namebuf, sizeof namebuf, name);
    q.name = xstrdup(namebuf);

    const JVal *v;
    if ((v = json_get(body, "description"))) {
        const char *sv = json_str(v);
        if (!sv) {
            err_set(&e, "invalid_description", "description must be a string", "description");
            json_free(body);
            quit_free(&q);
            send_error(res, 400, e.code, e.message, e.field);
            return;
        }
        free(q.description);
        q.description = xstrdup(sv);
    }
    if ((v = json_get(body, "color"))) {
        const char *sv = json_str(v);
        if (!sv) {
            err_set(&e, "invalid_color", "color must be a string", "color");
            json_free(body);
            quit_free(&q);
            send_error(res, 400, e.code, e.message, e.field);
            return;
        }
        free(q.color);
        q.color = xstrdup(sv);
    }
    if ((v = json_get(body, "icon"))) {
        const char *sv = json_str(v);
        if (!sv) {
            err_set(&e, "invalid_icon", "icon must be a string", "icon");
            json_free(body);
            quit_free(&q);
            send_error(res, 400, e.code, e.message, e.field);
            return;
        }
        free(q.icon);
        q.icon = xstrdup(sv);
    }
    if (!parse_started_at(body, now, &q.started_at, q.started_iso, &e)) {
        json_free(body);
        quit_free(&q);
        send_error(res, 400, e.code, e.message, e.field);
        return;
    }
    /* Normalise the timestamp so the stored value is always the same shape. */
    iso_from_epoch(q.started_at, q.started_iso);
    now_iso8601(q.created_at);

    const char *code = NULL, *msg = NULL;
    if (!quit_validate(&q, now, &code, &msg)) {
        json_free(body);
        quit_free(&q);
        send_error(res, 400, code, msg, field_for_code(code));
        return;
    }
    json_free(body);

    Quit *slot = db_quit_add(s->db);
    int64_t assigned = slot->id;
    int sort_order = slot->sort_order;
    q.id = assigned;
    q.sort_order = sort_order;
    *slot = q;

    if (!save_or_500(s, res)) {
        db_quit_remove(s->db, assigned);
        return;
    }
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    quit_to_json(&w, db_quit_by_id_c(s->db, assigned), now_epoch());
    res->status = 201;
}

static void handle_quit_get(const Server *s, int64_t id, HttpResponse *res) {
    const Quit *q = db_quit_by_id_c(s->db, id);
    if (!q) {
        send_error(res, 404, "quit_not_found", "bad habit not found", NULL);
        return;
    }
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    quit_to_json(&w, q, now_epoch());
    res->status = 200;
}

static void handle_quit_patch(Server *s, int64_t id, const HttpRequest *req, HttpResponse *res) {
    Quit *q = db_quit_by_id(s->db, id);
    if (!q) {
        send_error(res, 404, "quit_not_found", "bad habit not found", NULL);
        return;
    }
    ApiErr e = {0};
    JVal *body = parse_body(req, &e);
    if (!body) {
        send_error(res, 400, e.code, e.message, e.field);
        return;
    }
    int64_t now = now_epoch();
    Quit t = *q;
    char namebuf[512];
    const char *new_name = NULL, *new_desc = NULL, *new_color = NULL, *new_icon = NULL;
    int64_t new_started = q->started_at;
    char new_started_iso[32];
    snprintf(new_started_iso, sizeof new_started_iso, "%s", q->started_iso);
    bool started_set = false;

    const JVal *v;
    if ((v = json_get(body, "name"))) {
        const char *sv = json_str(v);
        if (!sv) {
            err_set(&e, "invalid_name", "name must be a string", "name");
            goto bad;
        }
        trim_copy(namebuf, sizeof namebuf, sv);
        new_name = namebuf;
        t.name = (char *)namebuf;
    }
    if ((v = json_get(body, "description"))) {
        const char *sv = json_str(v);
        if (!sv) {
            err_set(&e, "invalid_description", "description must be a string", "description");
            goto bad;
        }
        new_desc = sv;
        t.description = (char *)sv;
    }
    if ((v = json_get(body, "color"))) {
        const char *sv = json_str(v);
        if (!sv) {
            err_set(&e, "invalid_color", "color must be a string", "color");
            goto bad;
        }
        new_color = sv;
        t.color = (char *)sv;
    }
    if ((v = json_get(body, "icon"))) {
        const char *sv = json_str(v);
        if (!sv) {
            err_set(&e, "invalid_icon", "icon must be a string", "icon");
            goto bad;
        }
        new_icon = sv;
        t.icon = (char *)sv;
    }
    if (json_get(body, "started_at")) {
        if (!parse_started_at(body, now, &new_started, new_started_iso, &e)) goto bad;
        iso_from_epoch(new_started, new_started_iso);
        t.started_at = new_started;
        started_set = true;
    }

    {
        const char *code = NULL, *msg = NULL;
        if (!quit_validate(&t, now, &code, &msg)) {
            err_set(&e, code, msg, field_for_code(code));
            goto bad;
        }
    }

    if (new_name) {
        free(q->name);
        q->name = xstrdup(new_name);
    }
    if (new_desc) {
        free(q->description);
        q->description = xstrdup(new_desc);
    }
    if (new_color) {
        free(q->color);
        q->color = xstrdup(new_color);
    }
    if (new_icon) {
        free(q->icon);
        q->icon = xstrdup(new_icon);
    }
    if (started_set) {
        q->started_at = new_started;
        snprintf(q->started_iso, sizeof q->started_iso, "%s", new_started_iso);
    }
    json_free(body);

    if (!save_or_500(s, res)) return;
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    quit_to_json(&w, q, now_epoch());
    res->status = 200;
    return;

bad:
    json_free(body);
    send_error(res, 400, e.code, e.message, e.field);
}

static void handle_quit_delete(Server *s, int64_t id, HttpResponse *res) {
    if (!db_quit_by_id(s->db, id)) {
        send_error(res, 404, "quit_not_found", "bad habit not found", NULL);
        return;
    }
    db_quit_remove(s->db, id);
    if (!save_or_500(s, res)) return;
    res->status = 204;
    sb_reset(&res->body);
}

static void handle_quit_archive(Server *s, int64_t id, bool arch, HttpResponse *res) {
    Quit *q = db_quit_by_id(s->db, id);
    if (!q) {
        send_error(res, 404, "quit_not_found", "bad habit not found", NULL);
        return;
    }
    if (arch) {
        q->archived = true;
        q->archived_day = date_today();
        date_format(q->archived_day, q->archived_date);
    } else {
        q->archived = false;
        q->archived_date[0] = '\0';
    }
    if (!save_or_500(s, res)) return;
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    quit_to_json(&w, q, now_epoch());
    res->status = 200;
}

static void handle_quit_reset(Server *s, int64_t id, const HttpRequest *req, HttpResponse *res) {
    Quit *q = db_quit_by_id(s->db, id);
    if (!q) {
        send_error(res, 404, "quit_not_found", "bad habit not found", NULL);
        return;
    }
    int64_t now = now_epoch();
    int64_t at = now;
    if (req->body && req->body_len) {
        ApiErr e = {0};
        JVal *body = parse_body(req, &e);
        if (!body) {
            send_error(res, 400, e.code, e.message, e.field);
            return;
        }
        const JVal *v = json_get(body, "at");
        if (v) {
            const char *sv = json_str(v);
            if (!sv || !datetime_parse_epoch(sv, &at)) {
                json_free(body);
                send_error(res, 400, "invalid_started_at", "at must be an ISO 8601 date-time",
                           "at");
                return;
            }
        }
        json_free(body);
    }
    if (at > now) {
        send_error(res, 400, "invalid_started_at", "reset time cannot be in the future", "at");
        return;
    }
    if (at <= q->started_at) {
        send_error(res, 400, "invalid_started_at", "reset time must be after the attempt start",
                   "at");
        return;
    }
    int64_t elapsed = at - q->started_at;
    if (elapsed > q->best_seconds) q->best_seconds = elapsed;
    q->attempts++;
    q->started_at = at;
    iso_from_epoch(at, q->started_iso);

    if (!save_or_500(s, res)) return;
    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    quit_to_json(&w, q, now_epoch());
    res->status = 200;
}

static int item_cmp(const void *a, const void *b) {
    const Habit *ha = *(const Habit *const *)a;
    const Habit *hb = *(const Habit *const *)b;
    if (ha->sort_order != hb->sort_order) return ha->sort_order < hb->sort_order ? -1 : 1;
    return ha->id < hb->id ? -1 : (ha->id > hb->id ? 1 : 0);
}

static int planned_days_in_week(const Habit *h, int32_t week_start, int32_t today) {
    int n = 0;
    for (int i = 0; i < 7; i++) {
        int32_t d = week_start + i;
        if (d > today) break;
        if (sched_is_planned(h, d)) n++;
    }
    return n;
}

static void handle_today(Server *s, const HttpRequest *req, HttpResponse *res) {
    int32_t today = date_today();
    int32_t day = today;
    char buf[16];
    if (query_get(req->query, "date", buf, sizeof buf)) {
        if (!date_parse(buf, &day)) {
            send_error(res, 400, "invalid_date", "date must be YYYY-MM-DD", "date");
            return;
        }
        if (day > today) {
            send_error(res, 400, "date_in_future", "date is in the future", "date");
            return;
        }
    }

    const Habit **items = xmalloc((s->db->nhabits + 1) * sizeof *items);
    size_t n = 0;
    for (size_t i = 0; i < s->db->nhabits; i++) {
        const Habit *h = &s->db->habits[i];
        if (h->archived) continue;
        if (!sched_show_in_today(h, day)) continue;
        items[n++] = h;
    }
    qsort(items, n, sizeof *items, item_cmp);

    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    jw_obj(&w);
    char datebuf[11];
    date_format(day, datebuf);
    jw_key(&w, "date");
    jw_str(&w, datebuf);
    jw_key(&w, "items");
    jw_arr(&w);
    int32_t week = date_week_start(day);
    for (size_t i = 0; i < n; i++) {
        const Habit *h = items[i];
        jw_obj(&w);
        jw_key(&w, "habit");
        habit_to_json(&w, h);
        jw_key(&w, "entry");
        const Entry *e = db_entry_find_c(s->db, h->id, day);
        if (e)
            entry_to_json(&w, e);
        else
            jw_null(&w);
        jw_key(&w, "streak");
        Streak st = stats_streak(s->db, h, today);
        streak_to_json(&w, &st);
        jw_key(&w, "week");
        jw_obj(&w);
        int done = 0, skipped = 0;
        sched_week_tally(s->db, h->id, week, &done, &skipped);
        int quota = h->kind == SCHED_WEEKLY_COUNT ? (h->times - skipped)
                                                  : planned_days_in_week(h, week, today);
        if (quota < 0) quota = 0;
        jw_key(&w, "done");
        jw_int(&w, done);
        jw_key(&w, "quota");
        jw_int(&w, quota);
        jw_obj_end(&w);
        jw_obj_end(&w);
    }
    jw_arr_end(&w);
    jw_obj_end(&w);
    free(items);
    res->status = 200;
}

static const char *state_name(DayState st) {
    switch (st) {
        case DAY_DONE: return "done";
        case DAY_SKIPPED: return "skipped";
        case DAY_MISS: return "miss";
        case DAY_OFF: return "off";
        case DAY_FUTURE: return "future";
    }
    return "off";
}

static void handle_stats(Server *s, const HttpRequest *req, HttpResponse *res) {
    char buf[32];
    int64_t id = 0;
    if (!query_get(req->query, "habit_id", buf, sizeof buf) || !parse_id(buf, &id)) {
        send_error(res, 400, "habit_not_found", "habit_id is required", "habit_id");
        return;
    }
    const Habit *h = db_habit_by_id_c(s->db, id);
    if (!h) {
        send_error(res, 404, "habit_not_found", "habit not found", NULL);
        return;
    }
    int32_t today = date_today();
    int32_t from = today - 29;
    int32_t to = today;
    if (query_get(req->query, "from", buf, sizeof buf) && !date_parse(buf, &from)) {
        send_error(res, 400, "invalid_date", "from must be YYYY-MM-DD", "from");
        return;
    }
    if (query_get(req->query, "to", buf, sizeof buf) && !date_parse(buf, &to)) {
        send_error(res, 400, "invalid_date", "to must be YYYY-MM-DD", "to");
        return;
    }
    if (to > today) to = today;
    if (from > to) from = to;

    /* Heatmap: 12 ISO weeks ending with the current one. */
    int32_t heat_start = date_week_start(today) - 11 * 7;

    sb_reset(&res->body);
    JsonW w;
    jw_init(&w, &res->body);
    jw_obj(&w);
    jw_key(&w, "habit_id");
    jw_int(&w, id);
    char dbuf[11];
    date_format(from, dbuf);
    jw_key(&w, "from");
    jw_str(&w, dbuf);
    date_format(to, dbuf);
    jw_key(&w, "to");
    jw_str(&w, dbuf);
    jw_key(&w, "streak");
    Streak st = stats_streak(s->db, h, today);
    streak_to_json(&w, &st);
    jw_key(&w, "rate");
    Rate rate = stats_rate(s->db, h, from, to, today);
    rate_to_json(&w, &rate);
    jw_key(&w, "heatmap");
    jw_obj(&w);
    jw_key(&w, "weeks");
    jw_int(&w, 12);
    jw_key(&w, "cells");
    jw_arr(&w);
    for (int i = 0; i < 12 * 7; i++) {
        int32_t d = heat_start + i;
        DayState ds = stats_day_state(s->db, h, d, today);
        if (h->kind == SCHED_WEEKLY_COUNT && ds == DAY_MISS) ds = DAY_OFF;
        char cbuf[11];
        date_format(d, cbuf);
        jw_obj(&w);
        jw_key(&w, "date");
        jw_str(&w, cbuf);
        jw_key(&w, "state");
        jw_str(&w, state_name(ds));
        jw_obj_end(&w);
    }
    jw_arr_end(&w);
    jw_obj_end(&w);
    jw_obj_end(&w);
    res->status = 200;
}

static void handle_export(Server *s, HttpResponse *res) {
    sb_reset(&res->body);
    store_serialize(s->db, &res->body);
    char date[11];
    date_format(date_today(), date);
    snprintf(res->filename, sizeof res->filename, "habits-%s.jsonl", date);
    snprintf(res->content_type, sizeof res->content_type, "application/x-ndjson; charset=utf-8");
    res->status = 200;
}

/* ---- static files -------------------------------------------------------- */

static void serve_static(Server *s, const HttpRequest *req, HttpResponse *res) {
    char decoded[1024];
    if (!url_decode(req->path, decoded, sizeof decoded)) {
        send_text(res, 400, "bad path\n");
        return;
    }
    if (strstr(decoded, "..")) {
        send_text(res, 404, "not found\n");
        return;
    }
    const char *rel = decoded[0] == '/' ? decoded + 1 : decoded;
    const char *asset_path = decoded;
    if (*rel == '\0') {
        rel = "index.html";
        asset_path = "/index.html";
    }

    if (s->web_dir) {
        char *full = path_join(s->web_dir, rel);
        char *data = NULL;
        size_t len = 0;
        bool ok = file_read_all(full, &data, &len);
        free(full);
        if (!ok) {
            send_text(res, 404, "not found\n");
            return;
        }
        sb_reset(&res->body);
        sb_add(&res->body, data, len);
        free(data);
        snprintf(res->content_type, sizeof res->content_type, "%s", web_mime_for(rel));
        res->status = 200;
        return;
    }

    const WebAsset *a = web_asset_find(asset_path);
    if (!a) {
        if (strcmp(asset_path, "/index.html") == 0) {
            send_text(res, 500,
                      "habits: the embedded UI bundle is missing index.html.\n"
                      "Rebuild the project (make) or run with --web-dir web.\n");
            return;
        }
        send_text(res, 404, "not found\n");
        return;
    }
    sb_reset(&res->body);
    sb_add(&res->body, (const char *)a->data, a->len);
    snprintf(res->content_type, sizeof res->content_type, "%s", a->mime);
    res->status = 200;
}

/* ---- routing ------------------------------------------------------------- */

static void api_dispatch(Server *s, const HttpRequest *req, HttpResponse *res) {
    const char *p = req->path + strlen("/api/v1");
    bool get = strcmp(req->method, "GET") == 0;

    if (strcmp(p, "/health") == 0) {
        if (!get) {
            send_error(res, 405, "method_not_allowed", "GET only", NULL);
            return;
        }
        handle_health(s, res);
        return;
    }
    if (strcmp(p, "/export") == 0) {
        if (!get) {
            send_error(res, 405, "method_not_allowed", "GET only", NULL);
            return;
        }
        handle_export(s, res);
        return;
    }
    if (strcmp(p, "/today") == 0) {
        if (!get) {
            send_error(res, 405, "method_not_allowed", "GET only", NULL);
            return;
        }
        handle_today(s, req, res);
        return;
    }
    if (strcmp(p, "/stats") == 0) {
        if (!get) {
            send_error(res, 405, "method_not_allowed", "GET only", NULL);
            return;
        }
        handle_stats(s, req, res);
        return;
    }
    if (strcmp(p, "/habits") == 0) {
        if (get)
            handle_habits_list(s, req, res);
        else if (strcmp(req->method, "POST") == 0)
            handle_habit_create(s, req, res);
        else
            send_error(res, 405, "method_not_allowed", "GET or POST", NULL);
        return;
    }


    if (strcmp(p, "/quits") == 0) {
        if (get)
            handle_quits_list(s, req, res);
        else if (strcmp(req->method, "POST") == 0)
            handle_quit_create(s, req, res);
        else
            send_error(res, 405, "method_not_allowed", "GET or POST", NULL);
        return;
    }
    if (strncmp(p, "/quits/", 7) == 0) {
        char qidbuf[32];
        const char *qrest = p + 7;
        const char *qslash = strchr(qrest, '/');
        size_t qidlen = qslash ? (size_t)(qslash - qrest) : strlen(qrest);
        if (qidlen == 0 || qidlen >= sizeof qidbuf) {
            send_error(res, 404, "quit_not_found", "bad habit not found", NULL);
            return;
        }
        memcpy(qidbuf, qrest, qidlen);
        qidbuf[qidlen] = '\0';
        int64_t qid = 0;
        if (!parse_id(qidbuf, &qid)) {
            send_error(res, 404, "quit_not_found", "bad habit not found", NULL);
            return;
        }
        const char *qsub = qslash ? qslash + 1 : "";
        if (*qsub == '\0') {
            if (get)
                handle_quit_get(s, qid, res);
            else if (strcmp(req->method, "PATCH") == 0)
                handle_quit_patch(s, qid, req, res);
            else if (strcmp(req->method, "DELETE") == 0)
                handle_quit_delete(s, qid, res);
            else
                send_error(res, 405, "method_not_allowed", "GET, PATCH or DELETE", NULL);
            return;
        }
        if (strcmp(qsub, "reset") == 0) {
            if (strcmp(req->method, "POST") != 0)
                send_error(res, 405, "method_not_allowed", "POST only", NULL);
            else
                handle_quit_reset(s, qid, req, res);
            return;
        }
        if (strcmp(qsub, "archive") == 0) {
            if (strcmp(req->method, "POST") != 0)
                send_error(res, 405, "method_not_allowed", "POST only", NULL);
            else
                handle_quit_archive(s, qid, true, res);
            return;
        }
        if (strcmp(qsub, "unarchive") == 0) {
            if (strcmp(req->method, "POST") != 0)
                send_error(res, 405, "method_not_allowed", "POST only", NULL);
            else
                handle_quit_archive(s, qid, false, res);
            return;
        }
        send_error(res, 404, "not_found", "no such endpoint", NULL);
        return;
    }

    if (strncmp(p, "/habits/", 8) != 0) {
        send_error(res, 404, "not_found", "no such endpoint", NULL);
        return;
    }

    char idbuf[32];
    const char *rest = p + 8;
    const char *slash = strchr(rest, '/');
    size_t idlen = slash ? (size_t)(slash - rest) : strlen(rest);
    if (idlen == 0 || idlen >= sizeof idbuf) {
        send_error(res, 404, "habit_not_found", "habit not found", NULL);
        return;
    }
    memcpy(idbuf, rest, idlen);
    idbuf[idlen] = '\0';
    int64_t id = 0;
    if (!parse_id(idbuf, &id)) {
        send_error(res, 404, "habit_not_found", "habit not found", NULL);
        return;
    }
    const char *sub = slash ? slash + 1 : "";

    if (*sub == '\0') {
        if (get)
            handle_habit_get(s, id, res);
        else if (strcmp(req->method, "PATCH") == 0)
            handle_habit_patch(s, id, req, res);
        else if (strcmp(req->method, "DELETE") == 0)
            handle_habit_delete(s, id, res);
        else
            send_error(res, 405, "method_not_allowed", "GET, PATCH or DELETE", NULL);
        return;
    }
    if (strcmp(sub, "archive") == 0) {
        if (strcmp(req->method, "POST") != 0)
            send_error(res, 405, "method_not_allowed", "POST only", NULL);
        else
            handle_archive(s, id, true, res);
        return;
    }
    if (strcmp(sub, "unarchive") == 0) {
        if (strcmp(req->method, "POST") != 0)
            send_error(res, 405, "method_not_allowed", "POST only", NULL);
        else
            handle_archive(s, id, false, res);
        return;
    }
    if (strcmp(sub, "entries") == 0) {
        if (!get) {
            send_error(res, 405, "method_not_allowed", "GET only", NULL);
            return;
        }
        handle_entries_list(s, id, req, res);
        return;
    }
    if (strncmp(sub, "entries/", 8) == 0) {
        const char *date = sub + 8;
        if (strcmp(req->method, "PUT") == 0)
            handle_entry_put(s, id, date, req, res);
        else if (strcmp(req->method, "DELETE") == 0)
            handle_entry_delete(s, id, date, res);
        else
            send_error(res, 405, "method_not_allowed", "PUT or DELETE", NULL);
        return;
    }
    send_error(res, 404, "not_found", "no such endpoint", NULL);
}

void api_handle(const HttpRequest *req, HttpResponse *res, void *ud) {
    Server *s = ud;
    if (!check_security(req, res)) {
        if (s->verbose) fprintf(stderr, "%s %s -> %d\n", req->method, req->path, res->status);
        return;
    }

    if (strncmp(req->path, "/api/", 5) == 0) {
        if (strncmp(req->path, "/api/v1", 7) != 0) {
            send_error(res, 404, "not_found", "unknown API version", NULL);
        } else {
            api_dispatch(s, req, res);
        }
    } else if (strcmp(req->method, "GET") != 0) {
        send_error(res, 405, "method_not_allowed", "GET only", NULL);
    } else {
        serve_static(s, req, res);
    }
    if (s->verbose) fprintf(stderr, "%s %s -> %d\n", req->method, req->path, res->status);
}
