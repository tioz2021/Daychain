/*
 * Daychain — native client built on Raven.
 *
 * Reuses the same core and JSONL storage as the web build: no HTTP server,
 * no browser. Run with --data <dir> (default: <exe dir>/data).
 */

#include <Raven.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>

#include "core/model.h"
#include "core/schedule.h"
#include "core/stats.h"
#include "core/store.h"
#include "infra/util.h"

#define APP_VERSION "2.1.0"

typedef enum {
    SCREEN_TODAY = 0,
    SCREEN_HABITS,
    SCREEN_QUITTING,
    SCREEN_STATS,
    SCREEN_SETTINGS,
    SCREEN_COUNT
} Screen;

typedef struct {
    DB db;
    char data_dir[1024];

    Rid window;
    Rid content;
    Rid nav[SCREEN_COUNT];
    Rid nav_bg[SCREEN_COUNT];

    Screen screen;
    int32_t date;

    int64_t stats_habit;
    int stats_days;
    int light;      /* 0 = dark theme, 1 = light theme */
    int animations; /* 0 = Raven transitions off */
    Rid root;       /* the HBox that holds the sidebar and the content */

    /* Live counters of the quitting screen. */
    Rid quit_timer;
    Rid quit_labels[64];
    int64_t quit_starts[64];
    int quit_count;
} App;

static App g_app;

static void save_db(void);
static void rebuild(void);
static void show_message(const char *title, const char *text);
static Rid ui_vbox(float spacing);
static Rid ui_hbox(float spacing);
static Rid ui_label(const char *text, float size, int bold);
static Rid ui_muted(const char *text, float size);
static Rid ui_button(const char *text, void (*cb)(Rid, void *), void *ud);
static Rid ui_primary_button(const char *text, void (*cb)(Rid, void *), void *ud);
static Rid ui_danger_button(const char *text, void (*cb)(Rid, void *), void *ud);
static Rid ui_flat_button(const char *text, void (*cb)(Rid, void *), void *ud);
static Rid ui_spacer(void);
static void on_new_habit(Rid button, void *ud);
static void on_edit_habit(Rid button, void *ud);
static void on_archive_habit(Rid button, void *ud);
static void on_delete_habit(Rid button, void *ud);
static void on_new_quit(Rid button, void *ud);
static void on_edit_quit(Rid button, void *ud);
static void on_delete_quit(Rid button, void *ud);
static void on_toggle_theme(Rid button, void *ud);
static void on_toggle_animations(Rid button, void *ud);
static void rebuild_all(void);

/* ---------- small helpers ---------- */

static void hex_to_rgb(const char *hex, float *r, float *g, float *b) {
    unsigned v = 0x4f8cff;
    if (hex && hex[0] == '#') sscanf(hex + 1, "%06x", &v);
    *r = ((v >> 16) & 0xFF) / 255.0f;
    *g = ((v >> 8) & 0xFF) / 255.0f;
    *b = (v & 0xFF) / 255.0f;
}

static const char *icon_glyph(const char *key) {
    static const struct {
        const char *key;
        const char *glyph;
    } table[] = {
        {"dot", "●"},      {"book", "▤"},     {"water", "◉"},
        {"run", "➜"},      {"gym", "▲"},      {"sleep", "☾"},
        {"meditate", "☀"}, {"code", "⚙"},     {"music", "♪"},
        {"food", "♥"},     {"study", "◆"},    {"walk", "➜"},
        {"journal", "✎"},  {"star", "★"},     {"no-smoke", "⊘"},
        {"shield", "⚑"},   {"heart", "♥"},
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (key && strcmp(key, table[i].key) == 0) return table[i].glyph;
    return "●";
}

static void format_today_long(int32_t day, char *out, size_t cap) {
    static const char *names[] = {"Monday", "Tuesday", "Wednesday", "Thursday",
                                  "Friday", "Saturday", "Sunday"};
    static const char *months[] = {"January", "February", "March",     "April",
                                   "May",     "June",     "July",      "August",
                                   "September", "October", "November", "December"};
    int y;
    unsigned m, d;
    int32_t base = date_week_start(day);
    (void)base;
    /* date_format gives us the civil date; recover the parts from it. */
    char buf[11];
    date_format(day, buf);
    sscanf(buf, "%d-%u-%u", &y, &m, &d);
    snprintf(out, cap, "%s, %u %s %d", names[date_iso_weekday(day) - 1], d, months[m - 1], y);
}

static void format_duration(int64_t seconds, char *out, size_t cap) {
    if (seconds < 0) seconds = 0;
    int64_t days = seconds / 86400;
    int64_t rem = seconds % 86400;
    int hh = (int)(rem / 3600);
    int mm = (int)((rem % 3600) / 60);
    int ss = (int)(rem % 60);
    if (days > 0)
        snprintf(out, cap, "%lld %s %02d:%02d:%02d", (long long)days,
                 days == 1 ? "day" : "days", hh, mm, ss);
    else
        snprintf(out, cap, "%02d:%02d:%02d", hh, mm, ss);
}

/* ---------- ui helpers ---------- */

static Rid ui_vbox(float spacing) {
    Rid box = Rwidget_create(R_WIDGET_VBOX);
    Rpanel_set_spacing(box, spacing);
    return box;
}

static Rid ui_hbox(float spacing) {
    Rid box = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(box, spacing);
    return box;
}

static Rid ui_label(const char *text, float size, int bold) {
    Rid label = Rlabel_create(text);
    Rlabel_set_text_size(label, size);
    (void)bold; /* the bundled font has no bold variant to switch to yet */
    return label;
}

static Rid ui_muted(const char *text, float size) {
    Rid label = ui_label(text, size, 0);
    float r, g, b, a;
    Rtheme_get_color(R_COLOR_TEXT_DISABLED, &r, &g, &b, &a);
    Rwidget_set_color(label, R_COLOR_TEXT, r, g, b, 1.0f);
    return label;
}

static Rid base_button(const char *text, void (*cb)(Rid, void *), void *ud) {
    Rid button = Rbutton_create(text);
    Rbutton_set_style(button, R_BUTTON_DEFAULT);
    Rwidget_set_color(button, R_COLOR_CONTROL_BORDER, 0.0f, 0.0f, 0.0f, 0.0f);
    if (cb) Rbutton_set_clicked_callback(button, cb, ud);
    return button;
}

/* Neutral soft button used everywhere by default. */
static Rid ui_button(const char *text, void (*cb)(Rid, void *), void *ud) {
    Rid button = base_button(text, cb, ud);
    float r, g, b, a;
    Rtheme_get_color(R_COLOR_TEXT, &r, &g, &b, &a);
    Rwidget_set_color(button, R_COLOR_CONTROL, r, g, b, 0.07f);
    Rwidget_set_color(button, R_COLOR_CONTROL_HOVERED, r, g, b, 0.13f);
    Rwidget_set_color(button, R_COLOR_CONTROL_PRESSED, r, g, b, 0.20f);
    return button;
}

/* Filled with the accent: the main action of a screen or a dialog. */
static Rid ui_primary_button(const char *text, void (*cb)(Rid, void *), void *ud) {
    Rid button = base_button(text, cb, ud);
    float r, g, b, a;
    Rtheme_get_color(R_COLOR_ACCENT, &r, &g, &b, &a);
    Rwidget_set_color(button, R_COLOR_CONTROL, r, g, b, 1.0f);
    Rwidget_set_color(button, R_COLOR_CONTROL_HOVERED, r * 0.90f, g * 0.90f, b * 0.90f, 1.0f);
    Rwidget_set_color(button, R_COLOR_CONTROL_PRESSED, r * 0.80f, g * 0.80f, b * 0.80f, 1.0f);
    Rtheme_get_color(R_COLOR_ACCENT_TEXT, &r, &g, &b, &a);
    Rwidget_set_color(button, R_COLOR_TEXT, r, g, b, 1.0f);
    return button;
}

/* Soft red: destructive actions (delete, clear). */
static Rid ui_danger_button(const char *text, void (*cb)(Rid, void *), void *ud) {
    Rid button = base_button(text, cb, ud);
    const float r = 0.92f, g = 0.32f, b = 0.32f;
    Rwidget_set_color(button, R_COLOR_CONTROL, r, g, b, 0.12f);
    Rwidget_set_color(button, R_COLOR_CONTROL_HOVERED, r, g, b, 0.20f);
    Rwidget_set_color(button, R_COLOR_CONTROL_PRESSED, r, g, b, 0.28f);
    if (g_app.light)
        Rwidget_set_color(button, R_COLOR_TEXT, 0.62f, 0.10f, 0.10f, 1.0f);
    else
        Rwidget_set_color(button, R_COLOR_TEXT, 1.0f, 0.58f, 0.58f, 1.0f);
    return button;
}

static Rid ui_flat_button(const char *text, void (*cb)(Rid, void *), void *ud) {
    Rid button = Rbutton_create(text);
    Rbutton_set_style(button, R_BUTTON_FLAT);
    if (cb) Rbutton_set_clicked_callback(button, cb, ud);
    return button;
}

static float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* A rounded button tinted with a semantic colour: soft by default, solid when
   it is the active state (done / skipped). */
static Rid pill_button(const char *text, float r, float g, float b, int solid,
                       void (*cb)(Rid, void *), void *ud) {
    Rid button = Rbutton_create(text);
    Rbutton_set_style(button, R_BUTTON_DEFAULT);
    if (solid) {
        Rwidget_set_color(button, R_COLOR_CONTROL, r, g, b, 1.0f);
        Rwidget_set_color(button, R_COLOR_CONTROL_HOVERED, r * 0.92f, g * 0.92f, b * 0.92f, 1.0f);
        Rwidget_set_color(button, R_COLOR_CONTROL_PRESSED, r * 0.84f, g * 0.84f, b * 0.84f, 1.0f);
        float luma = 0.299f * r + 0.587f * g + 0.114f * b;
        if (luma > 0.62f)
            Rwidget_set_color(button, R_COLOR_TEXT, 0.10f, 0.09f, 0.06f, 1.0f);
        else
            Rwidget_set_color(button, R_COLOR_TEXT, 1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        Rwidget_set_color(button, R_COLOR_CONTROL, r, g, b, 0.14f);
        Rwidget_set_color(button, R_COLOR_CONTROL_HOVERED, r, g, b, 0.24f);
        Rwidget_set_color(button, R_COLOR_CONTROL_PRESSED, r, g, b, 0.34f);
        if (g_app.light)
            Rwidget_set_color(button, R_COLOR_TEXT, r * 0.68f, g * 0.68f, b * 0.68f, 1.0f);
        else
            Rwidget_set_color(button, R_COLOR_TEXT, clamp01(r * 1.15f + 0.20f),
                              clamp01(g * 1.15f + 0.20f), clamp01(b * 1.15f + 0.20f), 1.0f);
    }
    Rwidget_set_color(button, R_COLOR_CONTROL_BORDER, 0.0f, 0.0f, 0.0f, 0.0f);
    if (cb) Rbutton_set_clicked_callback(button, cb, ud);
    return button;
}

static Rid ui_spacer(void) {
    Rid spacer = Rwidget_create(R_WIDGET_SPACER);
    Rslot_set_fill(spacer, 1.0f);
    return spacer;
}

/* HBox children are stretched vertically by default; cards want them centered. */
static void add_center(Rid parent, Rid child) {
    Rslot_set_valign(child, R_VALIGN_CENTER);
    Rwidget_add_child(parent, child);
}

/* A panel surface with a one-pixel outline, so cards read as separate blocks. */
static void card_surface(Rid border) {
    float r, g, b, a;
    Rtheme_get_color(R_COLOR_PANEL, &r, &g, &b, &a);
    Rborder_set_background(border, r, g, b, 1.0f);
    Rborder_set_corner_radius(border, 0.0f, 0.0f, 0.0f, 0.0f);
    Rtheme_get_color(R_COLOR_BORDER, &r, &g, &b, &a);
    Rborder_set_outline(border, 1.0f, r, g, b, 1.0f);
}

static Rid ui_card(Rid *content) {
    Rid border = Rwidget_create(R_WIDGET_BORDER);
    Rborder_set_padding(border, 14.0f, 12.0f, 14.0f, 12.0f);
    card_surface(border);
    Rid box = ui_hbox(12.0f);
    Rwidget_add_child(border, box);
    *content = box;
    return border;
}

/* A card with a coloured strip on the left, in the habit's colour. */
static Rid ui_card_acc(const char *hex, Rid *content) {
    float r, g, b;
    hex_to_rgb(hex, &r, &g, &b);
    float pr, pg, pb, pa;
    Rtheme_get_color(R_COLOR_PANEL, &pr, &pg, &pb, &pa);

    Rid outer = Rwidget_create(R_WIDGET_BORDER);
    card_surface(outer);

    Rid row = ui_hbox(0.0f);
    Rwidget_add_child(outer, row);

    Rid strip = Rwidget_create(R_WIDGET_BORDER);
    Rwidget_set_size(strip, 4.0f, 0.0f);
    Rborder_set_background(strip, r, g, b, 1.0f);
    Rborder_set_corner_radius(strip, 0.0f, 0.0f, 0.0f, 0.0f);
    Rwidget_add_child(row, strip);

    Rid inner = ui_hbox(12.0f);
    Rslot_set_padding(inner, 14.0f, 12.0f, 14.0f, 12.0f);
    Rslot_set_fill(inner, 1.0f);
    Rwidget_add_child(row, inner);
    *content = inner;
    return outer;
}

static Rid ui_icon_badge(const char *glyph, const char *hex) {
    float r, g, b;
    hex_to_rgb(hex, &r, &g, &b);
    float pr, pg, pb, pa;
    Rtheme_get_color(R_COLOR_PANEL, &pr, &pg, &pb, &pa);
    /* Blend the habit colour into the panel: a soft chip in both themes. */
    const float mix = 0.16f;
    float br = pr * (1.0f - mix) + r * mix;
    float bg = pg * (1.0f - mix) + g * mix;
    float bb = pb * (1.0f - mix) + b * mix;
    Rid badge = Rwidget_create(R_WIDGET_BORDER);
    Rwidget_set_size(badge, 40.0f, 40.0f);
    Rborder_set_background(badge, br, bg, bb, 1.0f);
    Rborder_set_corner_radius(badge, 0.0f, 0.0f, 0.0f, 0.0f);
    Rid label = Rlabel_create(glyph);
    Rlabel_set_text_size(label, 19.0f);
    if (g_app.light)
        Rlabel_set_color(label, r * 0.72f, g * 0.72f, b * 0.72f, 1.0f);
    else
        Rlabel_set_color(label, r + 0.15f, g + 0.15f, b + 0.15f, 1.0f);
    Rslot_set_halign(label, R_HALIGN_CENTER);
    Rslot_set_valign(label, R_VALIGN_CENTER);
    Rwidget_add_child(badge, label);
    return badge;
}

/* ---------- persistence ---------- */

static void save_db(void) {
    char err[256] = "";
    if (!store_save(&g_app.db, g_app.data_dir, err, sizeof err))
        show_message("Cannot save", err);
}

static void show_message(const char *title, const char *text) {
    Rmessage_box(g_app.window, title, text, R_MESSAGE_OK);
}

/* ---------- navigation ---------- */

static void on_nav(Rid button, void *ud) {
    (void)button;
    g_app.screen = (Screen)(intptr_t)ud;
    rebuild();
}

static Rid nav_row(const char *text, Screen screen, int index) {
    Rid bg = Rwidget_create(R_WIDGET_BORDER);
    Rborder_set_corner_radius(bg, 0.0f, 0.0f, 0.0f, 0.0f);
    Rborder_set_padding(bg, 0.0f, 0.0f, 0.0f, 0.0f);
    Rborder_set_background(bg, 0.0f, 0.0f, 0.0f, 0.0f);

    Rid button = ui_flat_button(text, on_nav, (void *)(intptr_t)screen);
    /* The hover fill must have exactly the shape of the pill behind it. */
    Rwidget_set_focusable(button, R_FALSE);
    Rwidget_add_child(bg, button);

    g_app.nav_bg[index] = bg;
    g_app.nav[index] = button;
    return bg;
}

/* ---------- today screen ---------- */

static void schedule_text(const Habit *h, char *out, size_t cap) {
    static const char *short_days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    if (h->kind == SCHED_DAILY) {
        snprintf(out, cap, "Every day");
    } else if (h->kind == SCHED_WEEKLY_COUNT) {
        snprintf(out, cap, "%d× per week", h->times);
    } else {
        size_t used = 0;
        out[0] = '\0';
        for (int i = 0; i < 7; i++) {
            if (!((h->days_mask >> i) & 1u)) continue;
            int n = snprintf(out + used, cap - used, "%s%s", used ? ", " : "", short_days[i]);
            if (n < 0 || (size_t)n >= cap - used) break;
            used += (size_t)n;
        }
    }
}

static int quit_cmp(const void *a, const void *b) {
    const Quit *qa = *(const Quit *const *)a;
    const Quit *qb = *(const Quit *const *)b;
    if (qa->sort_order != qb->sort_order) return qa->sort_order < qb->sort_order ? -1 : 1;
    return qa->id < qb->id ? -1 : (qa->id > qb->id ? 1 : 0);
}

static int habit_cmp(const void *a, const void *b) {
    const Habit *ha = *(const Habit *const *)a;
    const Habit *hb = *(const Habit *const *)b;
    if (ha->sort_order != hb->sort_order) return ha->sort_order < hb->sort_order ? -1 : 1;
    return ha->id < hb->id ? -1 : (ha->id > hb->id ? 1 : 0);
}

static void on_toggle_done(Rid button, void *ud);
static void on_note(Rid button, void *ud);
static void on_today_move_up(Rid button, void *ud);
static void on_today_move_down(Rid button, void *ud);
static void on_quit_move_up(Rid button, void *ud);
static void on_quit_move_down(Rid button, void *ud);
static void on_toggle_skip(Rid button, void *ud);

static void on_prev_day(Rid b, void *ud) {
    (void)b;
    (void)ud;
    g_app.date -= 1;
    rebuild();
}

static void on_next_day(Rid b, void *ud) {
    (void)b;
    (void)ud;
    if (g_app.date < date_today()) {
        g_app.date += 1;
        rebuild();
    }
}

static void on_today(Rid b, void *ud) {
    (void)b;
    (void)ud;
    g_app.date = date_today();
    rebuild();
}

static const Habit **today_sorted(size_t *count) {
    const Habit **items = xmalloc((g_app.db.nhabits + 1) * sizeof *items);
    size_t n = 0;
    for (size_t i = 0; i < g_app.db.nhabits; i++) {
        Habit *h = &g_app.db.habits[i];
        if (h->archived) continue;
        if (!sched_show_in_today(h, g_app.date)) continue;
        items[n++] = h;
    }
    qsort(items, n, sizeof *items, habit_cmp);
    *count = n;
    return items;
}

static Rid habit_row(const Habit *h) {
    int32_t day = g_app.date;
    const Entry *entry = db_entry_find_c(&g_app.db, h->id, day);
    char meta[160];
    Streak streak = stats_streak(&g_app.db, h, date_today());
    int32_t week = date_week_start(day);
    int done = 0, skipped = 0;
    sched_week_tally(&g_app.db, h->id, week, &done, &skipped);
    int quota = h->kind == SCHED_WEEKLY_COUNT ? (h->times - skipped) : 7;
    if (quota < 0) quota = 0;

    if (streak.current > 0)
        snprintf(meta, sizeof meta, "%d %s in a row · %d/%d this week", streak.current,
                 streak.unit == STATS_UNIT_WEEKS ? "weeks" : "days", done, quota);
    else
        snprintf(meta, sizeof meta, "%d/%d this week", done, quota);

    Rid col;
    Rid card = ui_card_acc(h->color, &col);
    add_center(col, ui_icon_badge(icon_glyph(h->icon), h->color));

    Rid text = ui_vbox(3.0f);
    Rwidget_add_child(text, ui_label(h->name, 16.0f, 1));
    Rwidget_add_child(text, ui_muted(meta, 12.0f));
    if (quota > 0) {
        Rid bar = Rwidget_create(R_WIDGET_PROGRESSBAR);
        Rwidget_set_size(bar, 130.0f, 4.0f);
        float value = (float)done / (float)quota;
        Rprogress_bar_set_value(bar, value > 1.0f ? 1.0f : value);
        float hr, hg, hb;
        hex_to_rgb(h->color, &hr, &hg, &hb);
        Rwidget_set_color(bar, R_COLOR_ACCENT, hr, hg, hb, 1.0f);
        Rslot_set_valign(bar, R_VALIGN_CENTER);
        Rwidget_add_child(text, bar);
    }
    add_center(col, text);
    Rwidget_add_child(col, ui_spacer());

    int is_done = entry && entry->status == ENTRY_DONE;
    int is_skipped = entry && entry->status == ENTRY_SKIPPED;

    Rid up = ui_button("↑", on_today_move_up, (void *)(intptr_t)h->id);
    Rid down = ui_button("↓", on_today_move_down, (void *)(intptr_t)h->id);
    Rwidget_set_size(up, 26.0f, 26.0f);
    Rwidget_set_size(down, 26.0f, 26.0f);
    Rwidget_set_tooltip(up, "Move up");
    Rwidget_set_tooltip(down, "Move down");
    add_center(col, up);
    add_center(col, down);
    add_center(col, ui_button("Note", on_note, (void *)(intptr_t)h->id));

    add_center(col, pill_button(is_done ? "Done ✓" : "Done", 0.16f, 0.72f, 0.38f, is_done,
                                on_toggle_done, (void *)(intptr_t)h->id));

    add_center(col, pill_button(is_skipped ? "Skipped" : "Skip", 0.95f, 0.60f, 0.05f, is_skipped,
                                on_toggle_skip, (void *)(intptr_t)h->id));
    return card;
}

static void on_toggle_done(Rid button, void *ud) {
    (void)button;
    int64_t id = (int64_t)(intptr_t)ud;
    const Entry *e = db_entry_find_c(&g_app.db, id, g_app.date);
    if (e && e->status == ENTRY_DONE) {
        db_entry_remove(&g_app.db, id, g_app.date);
    } else {
        char now[32];
        now_iso8601(now);
        db_entry_upsert(&g_app.db, id, g_app.date, ENTRY_DONE, e ? e->note : "", now);
    }
    save_db();
    rebuild();
}

static void on_toggle_skip(Rid button, void *ud) {
    (void)button;
    int64_t id = (int64_t)(intptr_t)ud;
    const Entry *e = db_entry_find_c(&g_app.db, id, g_app.date);
    if (e && e->status == ENTRY_SKIPPED) {
        db_entry_remove(&g_app.db, id, g_app.date);
    } else {
        char now[32];
        now_iso8601(now);
        db_entry_upsert(&g_app.db, id, g_app.date, ENTRY_SKIPPED, e ? e->note : "", now);
    }
    save_db();
    rebuild();
}

static void build_today(Rid root) {
    char datebuf[128];
    format_today_long(g_app.date, datebuf, sizeof datebuf);

    Rid head = ui_hbox(10.0f);
    Rid title = ui_label("Today", 26.0f, 1);
    Rwidget_add_child(head, title);
    Rid datelab = ui_muted(datebuf, 14.0f);
    Rslot_set_valign(datelab, R_VALIGN_CENTER);
    Rwidget_add_child(head, datelab);
    Rwidget_add_child(head, ui_spacer());
    Rwidget_add_child(head, ui_flat_button("‹", on_prev_day, NULL));
    if (g_app.date < date_today())
        Rwidget_add_child(head, ui_flat_button("›", on_next_day, NULL));
    if (g_app.date != date_today())
        Rwidget_add_child(head, ui_flat_button("Today", on_today, NULL));
    Rwidget_add_child(root, head);

    size_t n = 0;
    const Habit **items = today_sorted(&n);

    Rid scroll = Rwidget_create(R_WIDGET_SCROLL_BOX);
    Rslot_set_fill(scroll, 1.0f);
    Rid list = ui_vbox(8.0f);
    Rwidget_add_child(scroll, list);

    if (n == 0) {
        Rid empty = ui_vbox(8.0f);
        Rwidget_add_child(empty, ui_muted("Nothing planned for this day.", 14.0f));
        Rwidget_add_child(empty, ui_primary_button("New habit", on_new_habit, NULL));
        Rwidget_add_child(list, empty);
    }
    for (size_t i = 0; i < n; i++) Rwidget_add_child(list, habit_row(items[i]));
    free(items);
    Rwidget_add_child(root, scroll);
}

/* ---------- quitting screen ---------- */

static void on_quit_tick(Rid timer, void *ud) {
    (void)timer;
    (void)ud;
    int64_t now = now_epoch();
    for (int i = 0; i < g_app.quit_count; i++) {
        char buf[64];
        format_duration(now - g_app.quit_starts[i], buf, sizeof buf);
        if (Rwidget_is_valid(g_app.quit_labels[i])) Rlabel_set_text(g_app.quit_labels[i], buf);
    }
}

static void on_quit_reset(Rid button, void *ud) {
    (void)button;
    int64_t id = (int64_t)(intptr_t)ud;
    Quit *q = db_quit_by_id(&g_app.db, id);
    if (!q) return;
    if (Rmessage_box(g_app.window, "Start over?",
                     "The current attempt will be counted in your best result.",
                     R_MESSAGE_OK_CANCEL) != R_MESSAGE_RESULT_OK)
        return;
    quit_reset(q, now_epoch());
    save_db();
    rebuild();
}

static void build_quitting(Rid root) {
    Rid head = ui_hbox(10.0f);
    Rwidget_add_child(head, ui_label("Quitting", 26.0f, 1));
    Rwidget_add_child(head, ui_spacer());
    Rwidget_add_child(head, ui_primary_button("New bad habit", on_new_quit, NULL));
    Rwidget_add_child(root, head);

    g_app.quit_count = 0;
    Rid scroll = Rwidget_create(R_WIDGET_SCROLL_BOX);
    Rslot_set_fill(scroll, 1.0f);
    Rid list = ui_vbox(10.0f);
    Rwidget_add_child(scroll, list);

    int64_t now = now_epoch();
    Quit **order = xmalloc((g_app.db.nquits + 1) * sizeof *order);
    size_t nq = 0;
    for (size_t i = 0; i < g_app.db.nquits; i++)
        if (!g_app.db.quits[i].archived) order[nq++] = &g_app.db.quits[i];
    qsort(order, nq, sizeof *order, quit_cmp);
    for (size_t k = 0; k < nq; k++) {
        Quit *q = order[k];

        Rid content;
        Rid card = ui_card_acc(q->color, &content);
        add_center(content, ui_icon_badge(icon_glyph(q->icon), q->color));

        Rid col = ui_vbox(2.0f);
        Rwidget_add_child(col, ui_label(q->name, 17.0f, 1));
        char timer_text[64];
        format_duration(now - q->started_at, timer_text, sizeof timer_text);
        Rid timer = ui_label(timer_text, 30.0f, 1);
        Rwidget_add_child(col, timer);

        int64_t best = q->best_seconds > now - q->started_at ? q->best_seconds : now - q->started_at;
        char best_text[128];
        char best_dur[64];
        format_duration(best, best_dur, sizeof best_dur);
        snprintf(best_text, sizeof best_text, "Best: %s · %d attempt%s", best_dur, q->attempts,
                 q->attempts == 1 ? "" : "s");
        Rwidget_add_child(col, ui_muted(best_text, 12.0f));
        add_center(content, col);
        Rwidget_add_child(content, ui_spacer());
        Rid up = ui_button("↑", on_quit_move_up, (void *)(intptr_t)q->id);
        Rid down = ui_button("↓", on_quit_move_down, (void *)(intptr_t)q->id);
        Rwidget_set_size(up, 26.0f, 26.0f);
        Rwidget_set_size(down, 26.0f, 26.0f);
        Rwidget_set_tooltip(up, "Move up");
        Rwidget_set_tooltip(down, "Move down");
        add_center(content, up);
        add_center(content, down);
        add_center(content, ui_button("Relapsed", on_quit_reset, (void *)(intptr_t)q->id));
        add_center(content, ui_button("Edit", on_edit_quit, (void *)(intptr_t)q->id));
        add_center(content, ui_danger_button("Delete", on_delete_quit, (void *)(intptr_t)q->id));

        if (g_app.quit_count < 64) {
            g_app.quit_labels[g_app.quit_count] = timer;
            g_app.quit_starts[g_app.quit_count] = q->started_at;
            g_app.quit_count++;
        }
        Rwidget_add_child(list, card);
    }
    free(order);
    Rwidget_add_child(root, scroll);

    if (g_app.quit_timer) Rtimer_destroy(g_app.quit_timer);
    g_app.quit_timer = Rtimer_create(1.0, R_TRUE, on_quit_tick, NULL);
}

/* ---------- placeholders ---------- */


/* ---------- dialogs ---------- */

static void trim_copy(char *dst, size_t cap, const char *src) {
    if (!src) src = "";
    while (*src == ' ' || *src == '\t') src++;
    size_t n = strlen(src);
    while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\t')) n--;
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void on_dialog_cancel(Rid button, void *ud) {
    (void)ud;
    Rdialog_close(button, 0);
}

static void dialog_closed(Rid content, int result, void *ud) {
    (void)content;
    (void)result;
    free(ud);
    rebuild();
}


typedef struct {
    Rid text;
    Rid error;
    int64_t habit_id;
    int32_t day;
} NoteForm;

static void note_save(Rid button, void *ud) {
    NoteForm *f = ud;
    const char *note = Rtext_editor_get_text(f->text);
    if (note && utf8_chars(note) > 500) {
        Rlabel_set_text(f->error, "Note is too long (max 500 characters)");
        return;
    }
    const Entry *e = db_entry_find_c(&g_app.db, f->habit_id, f->day);
    if (!e && (!note || !*note)) {
        Rdialog_close(button, 0);
        return;
    }
    char now[32];
    now_iso8601(now);
    db_entry_upsert(&g_app.db, f->habit_id, f->day, e ? e->status : ENTRY_DONE, note, now);
    save_db();
    Rdialog_close(button, 1);
}

static void open_note_form(int64_t habit_id) {
    const Entry *e = db_entry_find_c(&g_app.db, habit_id, g_app.date);
    NoteForm *f = xmalloc(sizeof *f);
    f->habit_id = habit_id;
    f->day = g_app.date;

    Rid form = ui_vbox(8.0f);
    Rwidget_set_min_size(form, 420.0f, 0.0f);
    Rwidget_add_child(form, ui_label("Note", 18.0f, 1));
    f->text = Rtext_editor_create("");
    Rtext_editor_set_placeholder(f->text, "How did it go?");
    Rtext_editor_set_line_numbers(f->text, R_FALSE);
    if (e) Rtext_editor_set_text(f->text, e->note);
    Rwidget_set_size(f->text, 400.0f, 130.0f);
    Rwidget_add_child(form, f->text);
    f->error = ui_muted("", 12.0f);
    Rlabel_set_color(f->error, 0.95f, 0.35f, 0.35f, 1.0f);
    Rwidget_add_child(form, f->error);

    Rid buttons = ui_hbox(8.0f);
    Rwidget_add_child(buttons, ui_spacer());
    Rwidget_add_child(buttons, ui_button("Cancel", on_dialog_cancel, NULL));
    Rid save = ui_primary_button("Save", note_save, f);
    Rwidget_add_child(buttons, save);
    Rwidget_add_child(form, buttons);

    Rid wrap = Rwidget_create(R_WIDGET_BORDER);
    Rborder_set_padding(wrap, 18.0f, 16.0f, 18.0f, 16.0f);
    Rborder_set_background(wrap, 0.0f, 0.0f, 0.0f, 0.0f);
    Rwidget_add_child(wrap, form);
    Rdialog_open(g_app.window, wrap, dialog_closed, f);
}

static void on_note(Rid button, void *ud) {
    (void)button;
    open_note_form((int64_t)(intptr_t)ud);
}

typedef struct {
    Rid name, desc, kind, color, icon, days_row, times_row, days[7], spin, error;
    int64_t edit_id;
} HabitForm;

static const char *const kColors[] = {"#4f8cff", "#22c55e", "#f59e0b", "#ef4444",
                                      "#a855f7", "#06b6d4", "#ec4899", "#64748b"};
static const char *const kColorNames[] = {"Blue", "Green", "Amber", "Red",
                                          "Purple", "Cyan", "Pink", "Grey"};
static const char *const kIcons[] = {"dot", "book", "water", "run", "gym", "sleep",
                                     "meditate", "code", "music", "food", "study",
                                     "walk", "journal", "star", "no-smoke", "shield"};
#define K_ICON_COUNT ((int)(sizeof kIcons / sizeof kIcons[0]))
#define K_COLOR_COUNT ((int)(sizeof kColors / sizeof kColors[0]))

static void habit_kind_changed(Rid combo, int index, void *ud) {
    (void)combo;
    HabitForm *f = ud;
    Rwidget_set_visibility(f->days_row, index == 1 ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(f->times_row, index == 2 ? R_VISIBLE : R_COLLAPSED);
}

static void habit_save(Rid button, void *ud) {
    HabitForm *f = ud;
    char name[512];
    char desc[2048];
    trim_copy(name, sizeof name, Rtext_input_get_text(f->name));
    trim_copy(desc, sizeof desc, Rtext_input_get_text(f->desc));
    if (!name[0]) {
        Rlabel_set_text(f->error, "Name is required");
        return;
    }

    int kind_index = Rcombo_box_get_selected(f->kind);
    SchedKind kind = SCHED_DAILY;
    if (kind_index == 1) kind = SCHED_WEEKDAYS;
    if (kind_index == 2) kind = SCHED_WEEKLY_COUNT;

    unsigned mask = 0;
    for (int i = 0; i < 7; i++)
        if (Rcheckbox_is_checked(f->days[i])) mask |= 1u << i;
    int times = (int)Rspin_box_get_value(f->spin);

    int color_index = Rcombo_box_get_selected(f->color);
    int icon_index = Rcombo_box_get_selected(f->icon);
    if (color_index < 0 || color_index >= K_COLOR_COUNT) color_index = 0;
    if (icon_index < 0 || icon_index >= K_ICON_COUNT) icon_index = 0;
    const char *color = kColors[color_index];
    const char *icon = kIcons[icon_index];

    Habit *existing = f->edit_id ? db_habit_by_id(&g_app.db, f->edit_id) : NULL;
    Habit probe;
    memset(&probe, 0, sizeof probe);
    probe.name = name;
    probe.description = desc;
    probe.color = (char *)color;
    probe.icon = (char *)icon;
    probe.kind = kind;
    probe.days_mask = mask;
    probe.times = times;
    probe.start_day = existing ? existing->start_day : date_today();

    const char *code = NULL, *msg = NULL;
    if (!habit_validate(&probe, &code, &msg)) {
        Rlabel_set_text(f->error, msg ? msg : "Invalid habit");
        return;
    }

    if (existing) {
        free(existing->name);
        existing->name = xstrdup(name);
        free(existing->description);
        existing->description = xstrdup(desc);
        existing->kind = kind;
        existing->days_mask = mask;
        existing->times = times;
        free(existing->color);
        existing->color = xstrdup(color);
        free(existing->icon);
        existing->icon = xstrdup(icon);
    } else {
        Habit *h = db_habit_add(&g_app.db);
        h->name = xstrdup(name);
        h->description = xstrdup(desc);
        h->color = xstrdup(color);
        h->icon = xstrdup(icon);
        h->category = NULL;
        h->kind = kind;
        h->days_mask = mask;
        h->times = times;
        h->start_day = date_today();
        date_format(h->start_day, h->start_date);
        now_iso8601(h->created_at);
    }
    save_db();
    Rdialog_close(button, 1);
}

static void open_habit_form(const Habit *edit) {
    HabitForm *f = xmalloc(sizeof *f);
    memset(f, 0, sizeof *f);
    f->edit_id = edit ? edit->id : 0;

    Rid form = ui_vbox(8.0f);
    Rwidget_set_min_size(form, 460.0f, 0.0f);
    Rwidget_add_child(form, ui_label(edit ? "Edit habit" : "New habit", 18.0f, 1));

    Rwidget_add_child(form, ui_muted("Name", 12.0f));
    f->name = Rwidget_create(R_WIDGET_TEXT_INPUT);
    Rtext_input_set_placeholder(f->name, "Read 30 minutes");
    if (edit) Rtext_input_set_text(f->name, edit->name);
    Rwidget_add_child(form, f->name);

    Rwidget_add_child(form, ui_muted("Description", 12.0f));
    f->desc = Rwidget_create(R_WIDGET_TEXT_INPUT);
    Rtext_input_set_placeholder(f->desc, "Optional");
    if (edit) Rtext_input_set_text(f->desc, edit->description);
    Rwidget_add_child(form, f->desc);

    Rwidget_add_child(form, ui_muted("Colour", 12.0f));
    f->color = Rwidget_create(R_WIDGET_COMBO_BOX);
    for (int i = 0; i < K_COLOR_COUNT; i++) Rcombo_box_add_item(f->color, kColorNames[i]);
    Rcombo_box_set_selected(f->color, 0);
    if (edit) {
        for (int i = 0; i < K_COLOR_COUNT; i++)
            if (strcasecmp(edit->color, kColors[i]) == 0) Rcombo_box_set_selected(f->color, i);
    }
    Rwidget_add_child(form, f->color);

    Rwidget_add_child(form, ui_muted("Icon", 12.0f));
    f->icon = Rwidget_create(R_WIDGET_COMBO_BOX);
    for (int i = 0; i < K_ICON_COUNT; i++) {
        char item[64];
        snprintf(item, sizeof item, "%s  %s", icon_glyph(kIcons[i]), kIcons[i]);
        Rcombo_box_add_item(f->icon, item);
    }
    Rcombo_box_set_selected(f->icon, 0);
    if (edit) {
        for (int i = 0; i < K_ICON_COUNT; i++)
            if (strcmp(edit->icon, kIcons[i]) == 0) Rcombo_box_set_selected(f->icon, i);
    }
    Rwidget_add_child(form, f->icon);

    Rwidget_add_child(form, ui_muted("Schedule", 12.0f));
    f->kind = Rwidget_create(R_WIDGET_COMBO_BOX);
    Rcombo_box_add_item(f->kind, "Every day");
    Rcombo_box_add_item(f->kind, "Selected weekdays");
    Rcombo_box_add_item(f->kind, "Times per week");
    Rcombo_box_set_selected(f->kind, edit ? (int)edit->kind : 0);
    Rcombo_box_set_changed_callback(f->kind, habit_kind_changed, f);
    Rwidget_add_child(form, f->kind);

    f->days_row = ui_hbox(4.0f);
    static const char *day_names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    for (int i = 0; i < 7; i++) {
        f->days[i] = Rcheckbox_create(day_names[i]);
        if (edit && (edit->days_mask & (1u << i))) Rcheckbox_set_checked(f->days[i], R_TRUE);
        Rwidget_add_child(f->days_row, f->days[i]);
    }
    Rwidget_add_child(form, f->days_row);

    f->times_row = ui_hbox(8.0f);
    Rwidget_add_child(f->times_row, ui_muted("Times per week", 12.0f));
    f->spin = Rwidget_create(R_WIDGET_SPIN_BOX);
    Rspin_box_set_range(f->spin, 1, 7);
    Rspin_box_set_decimals(f->spin, 0);
    Rspin_box_set_value(f->spin, edit && edit->times > 0 ? edit->times : 3);
    Rwidget_add_child(f->times_row, f->spin);
    Rwidget_add_child(form, f->times_row);

    f->error = ui_muted("", 12.0f);
    Rlabel_set_color(f->error, 0.95f, 0.35f, 0.35f, 1.0f);
    Rwidget_add_child(form, f->error);

    Rid buttons = ui_hbox(8.0f);
    Rwidget_add_child(buttons, ui_spacer());
    Rwidget_add_child(buttons, ui_button("Cancel", on_dialog_cancel, NULL));
    Rid save = ui_primary_button("Save", habit_save, f);
    Rwidget_add_child(buttons, save);
    Rwidget_add_child(form, buttons);

    Rcombo_box_set_selected(f->kind, edit ? (int)edit->kind : 0);
    habit_kind_changed(f->kind, edit ? (int)edit->kind : 0, f);
    Rid wrap = Rwidget_create(R_WIDGET_BORDER);
    Rborder_set_padding(wrap, 18.0f, 16.0f, 18.0f, 16.0f);
    Rborder_set_background(wrap, 0.0f, 0.0f, 0.0f, 0.0f);
    Rwidget_add_child(wrap, form);
    Rdialog_open(g_app.window, wrap, dialog_closed, f);
}

static void on_new_habit(Rid button, void *ud) {
    (void)button;
    (void)ud;
    open_habit_form(NULL);
}

static void on_edit_habit(Rid button, void *ud) {
    (void)button;
    open_habit_form(db_habit_by_id(&g_app.db, (int64_t)(intptr_t)ud));
}

static void on_archive_habit(Rid button, void *ud) {
    (void)button;
    Habit *h = db_habit_by_id(&g_app.db, (int64_t)(intptr_t)ud);
    if (!h) return;
    h->archived = R_TRUE;
    h->archived_day = date_today();
    date_format(h->archived_day, h->archived_date);
    save_db();
    rebuild();
}

static void on_restore_habit(Rid button, void *ud) {
    (void)button;
    Habit *h = db_habit_by_id(&g_app.db, (int64_t)(intptr_t)ud);
    if (!h) return;
    h->archived = 0;
    h->archived_date[0] = '\0';
    save_db();
    rebuild();
}

static void on_move_up(Rid button, void *ud) {
    (void)button;
    if (db_habit_move(&g_app.db, (int64_t)(intptr_t)ud, -1)) {
        save_db();
        rebuild();
    }
}

static void on_move_down(Rid button, void *ud) {
    (void)button;
    if (db_habit_move(&g_app.db, (int64_t)(intptr_t)ud, 1)) {
        save_db();
        rebuild();
    }
}

static void today_move(int64_t id, int delta) {
    size_t n = 0;
    const Habit **items = today_sorted(&n);
    size_t pos = n;
    for (size_t i = 0; i < n; i++)
        if (items[i]->id == id) {
            pos = i;
            break;
        }
    long target = (long)pos + delta;
    if (pos != n && target >= 0 && target < (long)n &&
        db_habit_swap(&g_app.db, id, items[target]->id)) {
        save_db();
        rebuild();
    }
    free(items);
}

static void on_today_move_up(Rid button, void *ud) {
    (void)button;
    today_move((int64_t)(intptr_t)ud, -1);
}

static void on_today_move_down(Rid button, void *ud) {
    (void)button;
    today_move((int64_t)(intptr_t)ud, 1);
}

static void on_quit_move_up(Rid button, void *ud) {
    (void)button;
    if (db_quit_move(&g_app.db, (int64_t)(intptr_t)ud, -1)) {
        save_db();
        rebuild();
    }
}

static void on_quit_move_down(Rid button, void *ud) {
    (void)button;
    if (db_quit_move(&g_app.db, (int64_t)(intptr_t)ud, 1)) {
        save_db();
        rebuild();
    }
}

static void on_clear_stats(Rid button, void *ud) {
    (void)button;
    int64_t id = (int64_t)(intptr_t)ud;
    Habit *h = db_habit_by_id(&g_app.db, id);
    if (!h) return;
    char text[320];
    snprintf(text, sizeof text,
             "Delete all marks of \"%s\"? The habit stays, but its history and "
             "statistics are gone.",
             h->name);
    if (Rmessage_box(g_app.window, "Clear statistics", text, R_MESSAGE_YES_NO) !=
        R_MESSAGE_RESULT_YES)
        return;
    db_entries_remove_habit(&g_app.db, id);
    save_db();
    rebuild();
}

static void on_delete_habit(Rid button, void *ud) {
    (void)button;
    int64_t id = (int64_t)(intptr_t)ud;
    Habit *h = db_habit_by_id(&g_app.db, id);
    if (!h) return;
    char text[256];
    snprintf(text, sizeof text, "Delete \"%s\" and all its records?", h->name);
    if (Rmessage_box(g_app.window, "Delete habit", text, R_MESSAGE_YES_NO) != R_MESSAGE_RESULT_YES)
        return;
    db_habit_remove(&g_app.db, id);
    save_db();
    rebuild();
}

typedef struct {
    Rid name, error;
    int64_t edit_id;
} QuitForm;

static void quit_save(Rid button, void *ud) {
    QuitForm *f = ud;
    char name[512];
    trim_copy(name, sizeof name, Rtext_input_get_text(f->name));
    if (!name[0]) {
        Rlabel_set_text(f->error, "Name is required");
        return;
    }
    Quit *existing = f->edit_id ? db_quit_by_id(&g_app.db, f->edit_id) : NULL;
    if (existing) {
        free(existing->name);
        existing->name = xstrdup(name);
    } else {
        Quit *q = db_quit_add(&g_app.db);
        q->name = xstrdup(name);
        q->description = xstrdup("");
        q->color = xstrdup("#ef4444");
        q->icon = xstrdup("no-smoke");
        q->started_at = now_epoch();
        iso_from_epoch(q->started_at, q->started_iso);
        now_iso8601(q->created_at);
    }
    save_db();
    Rdialog_close(button, 1);
}

static void open_quit_form(const Quit *edit) {
    QuitForm *f = xmalloc(sizeof *f);
    memset(f, 0, sizeof *f);
    f->edit_id = edit ? edit->id : 0;

    Rid form = ui_vbox(8.0f);
    Rwidget_set_min_size(form, 420.0f, 0.0f);
    Rwidget_add_child(form, ui_label(edit ? "Edit" : "New bad habit", 18.0f, 1));
    Rwidget_add_child(form, ui_muted("What are you quitting?", 12.0f));
    f->name = Rwidget_create(R_WIDGET_TEXT_INPUT);
    Rtext_input_set_placeholder(f->name, "Not smoking");
    if (edit) Rtext_input_set_text(f->name, edit->name);
    Rwidget_add_child(form, f->name);

    f->error = ui_muted("", 12.0f);
    Rlabel_set_color(f->error, 0.95f, 0.35f, 0.35f, 1.0f);
    Rwidget_add_child(form, f->error);

    Rid buttons = ui_hbox(8.0f);
    Rwidget_add_child(buttons, ui_spacer());
    Rwidget_add_child(buttons, ui_button("Cancel", on_dialog_cancel, NULL));
    Rid save = ui_primary_button("Save", quit_save, f);
    Rwidget_add_child(buttons, save);
    Rwidget_add_child(form, buttons);

    Rid wrap = Rwidget_create(R_WIDGET_BORDER);
    Rborder_set_padding(wrap, 18.0f, 16.0f, 18.0f, 16.0f);
    Rborder_set_background(wrap, 0.0f, 0.0f, 0.0f, 0.0f);
    Rwidget_add_child(wrap, form);
    Rdialog_open(g_app.window, wrap, dialog_closed, f);
}

static void on_new_quit(Rid button, void *ud) {
    (void)button;
    (void)ud;
    open_quit_form(NULL);
}

static void on_edit_quit(Rid button, void *ud) {
    (void)button;
    open_quit_form(db_quit_by_id(&g_app.db, (int64_t)(intptr_t)ud));
}

static void on_delete_quit(Rid button, void *ud) {
    (void)button;
    int64_t id = (int64_t)(intptr_t)ud;
    Quit *q = db_quit_by_id(&g_app.db, id);
    if (!q) return;
    char text[256];
    snprintf(text, sizeof text, "Delete \"%s\"?", q->name);
    if (Rmessage_box(g_app.window, "Delete", text, R_MESSAGE_YES_NO) != R_MESSAGE_RESULT_YES)
        return;
    db_quit_remove(&g_app.db, id);
    save_db();
    rebuild();
}

static void build_habits(Rid root) {
    Rid head = ui_hbox(10.0f);
    Rwidget_add_child(head, ui_label("Habits", 26.0f, 1));
    Rwidget_add_child(head, ui_spacer());
    Rwidget_add_child(head, ui_primary_button("New habit", on_new_habit, NULL));
    Rwidget_add_child(root, head);

    Rid scroll = Rwidget_create(R_WIDGET_SCROLL_BOX);
    Rslot_set_fill(scroll, 1.0f);
    Rid list = ui_vbox(8.0f);
    Rwidget_add_child(scroll, list);

    Habit **order = xmalloc((g_app.db.nhabits + 1) * sizeof *order);
    size_t nh = 0;
    for (size_t i = 0; i < g_app.db.nhabits; i++)
        if (!g_app.db.habits[i].archived) order[nh++] = &g_app.db.habits[i];
    qsort(order, nh, sizeof *order, habit_cmp);
    int any = nh > 0;
    for (size_t k = 0; k < nh; k++) {
        const Habit *h = order[k];
        Rid content;
        Rid card = ui_card_acc(h->color, &content);
        add_center(content, ui_icon_badge(icon_glyph(h->icon), h->color));
        Rid col = ui_vbox(2.0f);
        Rwidget_add_child(col, ui_label(h->name, 15.0f, 1));
        char sched[96];
        schedule_text(h, sched, sizeof sched);
        Rwidget_add_child(col, ui_muted(sched, 12.0f));
        add_center(content, col);
        Rwidget_add_child(content, ui_spacer());
        Rid up = ui_button("↑", on_move_up, (void *)(intptr_t)h->id);
        Rid down = ui_button("↓", on_move_down, (void *)(intptr_t)h->id);
        Rwidget_set_size(up, 26.0f, 26.0f);
        Rwidget_set_size(down, 26.0f, 26.0f);
        Rwidget_set_tooltip(up, "Move up");
        Rwidget_set_tooltip(down, "Move down");
        add_center(content, up);
        add_center(content, down);
        Rid edit = ui_button("Edit", on_edit_habit, (void *)(intptr_t)h->id);
        Rid arch = ui_button("Archive", on_archive_habit, (void *)(intptr_t)h->id);
        Rid del = ui_danger_button("Delete", on_delete_habit, (void *)(intptr_t)h->id);
        Rwidget_set_color(del, R_COLOR_TEXT, 0.94f, 0.38f, 0.38f, 1.0f);
        add_center(content, edit);
        add_center(content, arch);
        add_center(content, del);
        Rwidget_add_child(list, card);
    }
    free(order);
    if (!any) Rwidget_add_child(list, ui_muted("No habits yet. Add the first one.", 14.0f));

    int archived_count = 0;
    for (size_t i = 0; i < g_app.db.nhabits; i++)
        if (g_app.db.habits[i].archived) archived_count++;
    if (archived_count > 0) {
        Rwidget_add_child(list, ui_muted("Archived", 12.0f));
        for (size_t i = 0; i < g_app.db.nhabits; i++) {
            const Habit *h = &g_app.db.habits[i];
            if (!h->archived) continue;
            Rid content;
            Rid card = ui_card_acc(h->color, &content);
            add_center(content, ui_icon_badge(icon_glyph(h->icon), h->color));
            Rid col = ui_vbox(2.0f);
            Rwidget_add_child(col, ui_label(h->name, 15.0f, 1));
            char meta[96];
            snprintf(meta, sizeof meta, "Archived on %s", h->archived_date);
            Rwidget_add_child(col, ui_muted(meta, 12.0f));
            add_center(content, col);
            Rwidget_add_child(content, ui_spacer());
            add_center(content, ui_button("Restore", on_restore_habit, (void *)(intptr_t)h->id));
            add_center(content, ui_danger_button("Delete", on_delete_habit, (void *)(intptr_t)h->id));
            Rwidget_add_child(list, card);
        }
    }
    Rwidget_add_child(root, scroll);
}

static void paint_heatmap(Rid widget, RPainter *painter, float width, float height, void *ud) {
    (void)widget;
    (void)width;
    (void)height;
    App *app = ud;
    const Habit *h = db_habit_by_id_c(&app->db, app->stats_habit);
    if (!h) return;
    int32_t today = date_today();
    int32_t start = date_week_start(today) - 11 * 7;
    const float cell = 16.0f, gap = 3.0f;
    for (int i = 0; i < 12 * 7; i++) {
        DayState st = stats_day_state(&app->db, h, start + i, today);
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 0.04f;
        switch (st) {
            case DAY_DONE: r = 0.31f; g = 0.55f; b = 1.0f; a = 1.0f; break;
            case DAY_SKIPPED: r = 0.96f; g = 0.62f; b = 0.04f; a = 0.55f; break;
            case DAY_MISS: r = 0.90f; g = 0.30f; b = 0.30f; a = 0.22f; break;
            case DAY_OFF: r = 1.0f; g = 1.0f; b = 1.0f; a = 0.05f; break;
            case DAY_FUTURE: r = 1.0f; g = 1.0f; b = 1.0f; a = 0.02f; break;
        }
        float x = (float)(i / 7) * (cell + gap);
        float y = (float)(i % 7) * (cell + gap);
        Rdraw_rect(painter, x, y, cell, cell, 0.0f, r, g, b, a);
    }
}

static void on_stats_habit(Rid combo, int index, void *ud) {
    (void)combo;
    (void)ud;
    if (index >= 0 && index < (int)g_app.db.nhabits)
        g_app.stats_habit = g_app.db.habits[index].id;
    rebuild();
}

static void on_stats_period(Rid combo, int index, void *ud) {
    (void)combo;
    (void)ud;
    static const int days[] = {7, 30, 90, 365, 0};
    if (index >= 0 && index < 5) g_app.stats_days = days[index];
    rebuild();
}

static Rid stat_card(const char *value, const char *label, float vr, float vg, float vb) {
    float pr, pg, pb, pa;
    Rtheme_get_color(R_COLOR_PANEL, &pr, &pg, &pb, &pa);
    Rid border = Rwidget_create(R_WIDGET_BORDER);
    Rborder_set_padding(border, 16.0f, 12.0f, 16.0f, 12.0f);
    card_surface(border);
    Rid col = ui_vbox(2.0f);
    Rwidget_add_child(border, col);
    Rid big = ui_label(value, 26.0f, 1);
    Rlabel_set_color(big, vr, vg, vb, 1.0f);
    Rwidget_add_child(col, big);
    Rwidget_add_child(col, ui_muted(label, 12.0f));
    Rslot_set_fill(border, 1.0f);
    return border;
}

static void build_stats(Rid root) {
    Rwidget_add_child(root, ui_label("Statistics", 26.0f, 1));
    if (g_app.db.nhabits == 0) {
        Rwidget_add_child(root, ui_muted("No habits yet.", 14.0f));
        return;
    }
    if (!db_habit_by_id(&g_app.db, g_app.stats_habit))
        g_app.stats_habit = g_app.db.habits[0].id;

    Rid bar = ui_hbox(8.0f);
    Rid habit_combo = Rwidget_create(R_WIDGET_COMBO_BOX);
    int sel = 0;
    for (size_t i = 0; i < g_app.db.nhabits; i++) {
        char item[256];
        snprintf(item, sizeof item, "%s%s", g_app.db.habits[i].name,
                 g_app.db.habits[i].archived ? "  (archived)" : "");
        Rcombo_box_add_item(habit_combo, item);
        if (g_app.db.habits[i].id == g_app.stats_habit) sel = (int)i;
    }
    Rcombo_box_set_selected(habit_combo, sel);
    Rcombo_box_set_changed_callback(habit_combo, on_stats_habit, NULL);
    Rslot_set_fill(habit_combo, 1.0f);
    Rwidget_add_child(bar, habit_combo);

    Rid period = Rwidget_create(R_WIDGET_COMBO_BOX);
    static const char *periods[] = {"7 days", "30 days", "90 days", "365 days", "All time"};
    static const int period_days[] = {7, 30, 90, 365, 0};
    int psel = 1;
    for (int i = 0; i < 5; i++) {
        Rcombo_box_add_item(period, periods[i]);
        if (period_days[i] == g_app.stats_days) psel = i;
    }
    Rcombo_box_set_selected(period, psel);
    Rcombo_box_set_changed_callback(period, on_stats_period, NULL);
    Rwidget_add_child(bar, period);
    Rwidget_add_child(root, bar);

    Rid actions = ui_hbox(8.0f);
    const Habit *selected = db_habit_by_id_c(&g_app.db, g_app.stats_habit);
    Rwidget_add_child(actions, ui_danger_button("Clear statistics", on_clear_stats,
                                         (void *)(intptr_t)g_app.stats_habit));
    if (selected && selected->archived)
        Rwidget_add_child(actions, ui_danger_button("Delete habit", on_delete_habit,
                                             (void *)(intptr_t)g_app.stats_habit));
    Rwidget_add_child(root, actions);

    const Habit *h = db_habit_by_id_c(&g_app.db, g_app.stats_habit);
    int32_t today = date_today();
    int32_t from = g_app.stats_days > 0 ? today - (g_app.stats_days - 1) : h->start_day;
    Streak streak = stats_streak(&g_app.db, h, today);
    Rate rate = stats_rate(&g_app.db, h, from, today, today);

    char buf[64];
    snprintf(buf, sizeof buf, "%d", streak.current);
    char buf2[64];
    snprintf(buf2, sizeof buf2, "%d", streak.best);
    char buf3[64];
    if (rate.has)
        snprintf(buf3, sizeof buf3, "%d%%", rate.percent);
    else
        snprintf(buf3, sizeof buf3, "—");

    Rid cards = ui_hbox(10.0f);
    Rwidget_add_child(cards, stat_card(buf, "Current streak", 0.31f, 0.55f, 1.0f));
    Rwidget_add_child(cards, stat_card(buf2, "Best streak", 0.22f, 0.78f, 0.45f));
    Rwidget_add_child(cards, stat_card(buf3, "Completion", 0.66f, 0.42f, 0.95f));
    Rwidget_add_child(root, cards);

    Rid heat = Rwidget_create(R_WIDGET_CUSTOM);
    float size = 12 * 19.0f;
    Rwidget_set_size(heat, size, 7 * 19.0f);
    Rcustom_set_paint_callback(heat, paint_heatmap, &g_app);
    Rid heat_wrap = Rwidget_create(R_WIDGET_BORDER);
    Rborder_set_padding(heat_wrap, 16.0f, 16.0f, 16.0f, 16.0f);
    card_surface(heat_wrap);
    Rid hm_row = ui_hbox(6.0f);
    Rid days_col = ui_vbox(3.0f);
    static const char *day_labels[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    for (int i = 0; i < 7; i++) {
        Rid d = ui_muted(day_labels[i], 9.0f);
        Rwidget_set_size(d, 26.0f, 16.0f);
        Rwidget_add_child(days_col, d);
    }
    Rslot_set_valign(days_col, R_VALIGN_TOP);
    Rwidget_add_child(hm_row, days_col);
    Rslot_set_valign(heat, R_VALIGN_TOP);
    Rwidget_add_child(hm_row, heat);
    Rwidget_add_child(heat_wrap, hm_row);
    Rwidget_add_child(root, heat_wrap);

    Rid legend = ui_hbox(14.0f);
    struct { float r, g, b; const char *label; } entries[] = {
        {0.31f, 0.55f, 1.0f, "done"},
        {0.96f, 0.62f, 0.04f, "skipped"},
        {0.90f, 0.30f, 0.30f, "missed"},
    };
    for (int i = 0; i < 3; i++) {
        Rid item = ui_hbox(4.0f);
        Rid sw = Rwidget_create(R_WIDGET_BORDER);
        Rwidget_set_size(sw, 10.0f, 10.0f);
        Rborder_set_background(sw, entries[i].r, entries[i].g, entries[i].b, 0.66f);
        Rborder_set_corner_radius(sw, 0.0f, 0.0f, 0.0f, 0.0f);
        Rslot_set_valign(sw, R_VALIGN_CENTER);
        Rwidget_add_child(item, sw);
        Rwidget_add_child(item, ui_muted(entries[i].label, 11.0f));
        Rwidget_add_child(legend, item);
    }
    Rwidget_add_child(root, legend);
}

static void on_export(Rid button, void *ud) {
    (void)button;
    (void)ud;
    char *src = path_join(g_app.data_dir, "habits.jsonl");
    char *text = NULL;
    size_t len = 0;
    if (!file_read_all(src, &text, &len)) {
        free(src);
        show_message("Export failed", "Cannot read the data file.");
        return;
    }
    char name[128];
    char date[11];
    date_format(date_today(), date);
    snprintf(name, sizeof name, "daychain-%s.jsonl", date);
    const char *home = getenv("HOME");
    char *dst = home ? path_join(home, name) : xstrdup(name);
    bool ok = file_write_atomic(dst, text, len);
    char msg[1200];
    if (ok)
        snprintf(msg, sizeof msg, "Saved to %s", dst);
    else
        snprintf(msg, sizeof msg, "Cannot write %s", dst);
    free(text);
    free(src);
    free(dst);
    show_message(ok ? "Export" : "Export failed", msg);
}

static void settings_section(Rid col, const char *title) {
    Rid label = ui_muted(title, 11.0f);
    Rslot_set_padding(label, 0.0f, 12.0f, 0.0f, 0.0f);
    Rwidget_add_child(col, label);
}

static Rid setting_row(Rid col, const char *title, const char *value) {
    Rid row = ui_hbox(8.0f);
    Rwidget_add_child(row, ui_label(title, 13.0f, 0));
    Rwidget_add_child(row, ui_spacer());
    if (value && *value) Rwidget_add_child(row, ui_muted(value, 12.0f));
    Rwidget_add_child(col, row);
    return row;
}

static void build_settings(Rid root) {
    Rwidget_add_child(root, ui_label("Settings", 26.0f, 1));

    Rid content;
    Rid card = ui_card(&content);
    Rid col = ui_vbox(6.0f);
    add_center(content, col);

    settings_section(col, "APPEARANCE");
    Rid theme_row = setting_row(col, "Theme", g_app.light ? "Light" : "Dark");
    Rwidget_add_child(theme_row,
                      ui_button(g_app.light ? "Use dark" : "Use light", on_toggle_theme, NULL));

    Rid anim_row = setting_row(col, "Animations", g_app.animations ? "On" : "Off");
    Rwidget_add_child(anim_row,
                      ui_button(g_app.animations ? "Turn off" : "Turn on", on_toggle_animations,
                                NULL));

    settings_section(col, "DATA");
    Rid path = ui_muted(g_app.data_dir, 12.0f);
    Rlabel_set_wrap(path, R_TRUE);
    Rwidget_add_child(col, path);
    Rid export_row = ui_hbox(0.0f);
    Rwidget_add_child(export_row, ui_button("Export data", on_export, NULL));
    Rwidget_add_child(col, export_row);

    settings_section(col, "ABOUT");
    Rid version_row = setting_row(col, "Version", "daychain " APP_VERSION);
    (void)version_row;
    Rwidget_add_child(col,
                      ui_muted("A native client on Raven. The data is a plain JSONL "
                               "file: back it up or move it freely.",
                               12.0f));
    Rwidget_add_child(root, card);
}

/* ---------- theme ---------- */

static void apply_appearance(void) {
    Rtheme_apply(g_app.light ? R_THEME_LIGHT : R_THEME_DARK);
    /* Square everywhere: buttons, fields, menus, popups, cards. */
    Rtheme_set_metric(R_METRIC_CORNER_RADIUS, 0.0f);
    Rtheme_set_metric(R_METRIC_POPUP_RADIUS, 0.0f);
    Rset_animations_enabled(g_app.animations);
}

static void load_prefs(void) {
    char *path = path_join(g_app.data_dir, "ui.conf");
    char *text = NULL;
    size_t len = 0;
    if (file_read_all(path, &text, &len)) {
        if (strstr(text, "theme=light")) g_app.light = 1;
        if (strstr(text, "animations=off")) g_app.animations = 0;
        free(text);
    }
    free(path);
}

static void save_prefs(void) {
    char *path = path_join(g_app.data_dir, "ui.conf");
    char buf[96];
    snprintf(buf, sizeof buf, "theme=%s\nanimations=%s\n", g_app.light ? "light" : "dark",
             g_app.animations ? "on" : "off");
    file_write_atomic(path, buf, strlen(buf));
    free(path);
}

static void on_toggle_theme(Rid button, void *ud) {
    (void)button;
    (void)ud;
    g_app.light = !g_app.light;
    save_prefs();
    rebuild_all();
}

static void on_toggle_animations(Rid button, void *ud) {
    (void)button;
    (void)ud;
    g_app.animations = !g_app.animations;
    save_prefs();
    apply_appearance();
    rebuild();
}

/* ---------- window ---------- */

static void build_sidebar(Rid sidebar) {
    Rid brand = ui_label("Habits", 20.0f, 1);
    Rslot_set_padding(brand, 4.0f, 4.0f, 4.0f, 14.0f);
    Rwidget_add_child(sidebar, brand);

    static const char *names[SCREEN_COUNT] = {"Today", "Habits", "Quitting", "Statistics",
                                              "Settings"};
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (i == SCREEN_SETTINGS) Rwidget_add_child(sidebar, ui_spacer());
        Rwidget_add_child(sidebar, nav_row(names[i], (Screen)i, i));
    }
    Rwidget_add_child(sidebar, ui_muted("v" APP_VERSION, 11.0f));
}

static void rebuild(void) {
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (!g_app.nav[i]) continue;
        if (i == (int)g_app.screen) {
            Rborder_set_background(g_app.nav_bg[i], 0.31f, 0.55f, 1.0f, 0.16f);
            Rwidget_set_color(g_app.nav[i], R_COLOR_TEXT, 0.31f, 0.55f, 1.0f, 1.0f);
            /* hovering the current entry keeps the accent colour untinted */
            Rwidget_set_color(g_app.nav[i], R_COLOR_CONTROL_HOVERED, 0.31f, 0.55f, 1.0f, 0.10f);
            Rwidget_set_color(g_app.nav[i], R_COLOR_CONTROL_PRESSED, 0.31f, 0.55f, 1.0f, 0.18f);
        } else {
            Rborder_set_background(g_app.nav_bg[i], 0.0f, 0.0f, 0.0f, 0.0f);
            Rwidget_clear_color(g_app.nav[i], R_COLOR_TEXT);
            Rwidget_clear_color(g_app.nav[i], R_COLOR_CONTROL_HOVERED);
            Rwidget_clear_color(g_app.nav[i], R_COLOR_CONTROL_PRESSED);
        }
    }
    Rwidget_clear_children(g_app.content);
    switch (g_app.screen) {
        case SCREEN_TODAY: build_today(g_app.content); break;
        case SCREEN_HABITS: build_habits(g_app.content); break;
        case SCREEN_QUITTING: build_quitting(g_app.content); break;
        case SCREEN_STATS: build_stats(g_app.content); break;
        case SCREEN_SETTINGS: build_settings(g_app.content); break;
        default: break;
    }
}

static void rebuild_all(void) {
    apply_appearance();
    Rwidget_clear_children(g_app.root);
    for (int i = 0; i < SCREEN_COUNT; i++) g_app.nav[i] = R_INVALID_ID;

    /* The menu sits on its own panel surface, the same colour the cards use. */
    Rid sidebar_bg = Rwidget_create(R_WIDGET_BORDER);
    Rwidget_set_size(sidebar_bg, 206.0f, 0.0f);
    Rborder_set_padding(sidebar_bg, 16.0f, 18.0f, 12.0f, 14.0f);
    float pr, pg, pb, pa;
    Rtheme_get_color(R_COLOR_PANEL, &pr, &pg, &pb, &pa);
    Rborder_set_background(sidebar_bg, pr, pg, pb, 1.0f);
    Rborder_set_corner_radius(sidebar_bg, 0.0f, 0.0f, 0.0f, 0.0f);
    Rid sidebar = ui_vbox(4.0f);
    Rwidget_add_child(sidebar_bg, sidebar);
    build_sidebar(sidebar);
    Rwidget_add_child(g_app.root, sidebar_bg);

    /* a one-pixel line between the menu and the content, like the card outlines */
    Rid separator = Rwidget_create(R_WIDGET_BORDER);
    Rwidget_set_size(separator, 1.0f, 0.0f);
    float br, bg, bb, ba;
    Rtheme_get_color(R_COLOR_BORDER, &br, &bg, &bb, &ba);
    Rborder_set_background(separator, br, bg, bb, 1.0f);
    Rslot_set_auto(separator);
    Rwidget_add_child(g_app.root, separator);

    g_app.content = ui_vbox(14.0f);
    Rslot_set_fill(g_app.content, 1.0f);
    Rslot_set_padding(g_app.content, 26.0f, 22.0f, 26.0f, 20.0f);
    Rwidget_add_child(g_app.root, g_app.content);

    rebuild();
}

static int on_window_close(Rid window, void *ud) {
    (void)window;
    (void)ud;
    save_db();
    return R_TRUE;
}

static void on_error(RError code, RSeverity severity, const char *detail) {
    fprintf(stderr, "[raven] %s (%s): %s\n", Rerror_to_string(code), Rseverity_to_string(severity),
            detail);
}

static void usage(void) {
    printf("daychain %s — native client\n"
           "Usage: daychain [--data <dir>]\n"
           "Screenshot mode: DAYCHAIN_FRAMES=<n> DAYCHAIN_SHOT=<out.ppm>\n",
           APP_VERSION);
}

/* The same lock file the web server uses: two writers on one JSONL file would race. */
static int acquire_lock(const char *data_dir, char *err, size_t errsz) {
    char *locks = path_join(data_dir, "locks");
    if (mkdir_p(locks) != 0) {
        snprintf(err, errsz, "Cannot create %s", locks);
        free(locks);
        return -1;
    }
    char *path = path_join(locks, "habits.lock");
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        snprintf(err, errsz, "Cannot open %s", path);
        free(path);
        free(locks);
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        snprintf(err, errsz,
                 "Habits is already running: another window or the web server holds the lock "
                 "on this data directory.");
        close(fd);
        free(path);
        free(locks);
        return -1;
    }
    free(path);
    free(locks);
    return fd;
}

static char *resolve_data_dir(const char *arg) {
    if (arg) return xstrdup(arg);
    const char *env = getenv("DAYCHAIN_DATA_DIR");
    if (env && *env) return xstrdup(env);
    char *exe = exe_dir();
    char *dir = exe ? path_join(exe, "data") : xstrdup("data");
    free(exe);
    return dir;
}

int main(int argc, char **argv) {
    const char *data_arg = NULL;
    int screen_given = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc)
            data_arg = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("daychain %s\n", APP_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
            const char *name = argv[++i];
            if (strcmp(name, "habits") == 0)
                g_app.screen = SCREEN_HABITS;
            else if (strcmp(name, "quitting") == 0)
                g_app.screen = SCREEN_QUITTING;
            else if (strcmp(name, "stats") == 0)
                g_app.screen = SCREEN_STATS;
            else if (strcmp(name, "settings") == 0)
                g_app.screen = SCREEN_SETTINGS;
            else
                g_app.screen = SCREEN_TODAY;
            screen_given = 1;
        }
    }

    char *dir = resolve_data_dir(data_arg);
    snprintf(g_app.data_dir, sizeof g_app.data_dir, "%s", dir);
    free(dir);
    mkdir_p(g_app.data_dir);

    char lock_err[512] = "";
    int lock_fd = acquire_lock(g_app.data_dir, lock_err, sizeof lock_err);

    char err[512] = "";
    if (!store_load(&g_app.db, g_app.data_dir, err, sizeof err)) {
        fprintf(stderr, "daychain: %s\n", err);
        return 1;
    }
    if (!screen_given) g_app.screen = SCREEN_TODAY;
    g_app.date = date_today();

    Rerror_set_callback(on_error);
    if (!Rinit(R_BACKEND_VULKAN)) {
        fprintf(stderr, "daychain: cannot start Raven (Vulkan driver?)\n");
        return 1;
    }
    g_app.animations = 1;
    load_prefs();
    apply_appearance();

    g_app.window = Rwindow_create("Daychain", 1040, 700, R_WINDOW_DEFAULT);
    Rwindow_set_min_size(g_app.window, 880, 560);
    Rwindow_set_close_callback(g_app.window, on_window_close, NULL);

    g_app.root = ui_hbox(0.0f);
    Rwindow_set_content(g_app.window, g_app.root);
    rebuild_all();

    if (lock_fd < 0) {
        Rmessage_box(g_app.window, "Daychain", lock_err, R_MESSAGE_OK);
        Rshutdown();
        db_free(&g_app.db);
        return 1;
    }

    const char *dialog_env = getenv("DAYCHAIN_DIALOG");
    if (dialog_env && *dialog_env) {
        if (strcmp(dialog_env, "habit") == 0)
            open_habit_form(NULL);
        else if (strcmp(dialog_env, "note") == 0 && g_app.db.nhabits > 0)
            open_note_form(g_app.db.habits[0].id);
        else if (strcmp(dialog_env, "habit-edit") == 0 && g_app.db.nhabits > 0)
            open_habit_form(&g_app.db.habits[0]);
        else if (strcmp(dialog_env, "quit") == 0) open_quit_form(NULL);
    }

    /* Screenshot mode for the docs: render N frames, save the window, exit. */
    const char *frames_env = getenv("DAYCHAIN_FRAMES");
    if (frames_env && *frames_env) {
        int frames = atoi(frames_env);
        for (int i = 0; i < frames && Ris_running(); i++) {
            Rwindow_request_redraw(g_app.window);
            Rwait_frame(0.016);
        }
        const char *shot = getenv("DAYCHAIN_SHOT");
        if (shot && Rwindow_is_valid(g_app.window)) {
            int w = 0, h = 0;
            uint8_t *pixels = Rwindow_capture(g_app.window, &w, &h);
            if (pixels) {
                FILE *f = fopen(shot, "wb");
                if (f) {
                    fprintf(f, "P6\n%d %d\n255\n", w, h);
                    for (int i = 0; i < w * h; i++) fwrite(pixels + i * 4, 1, 3, f);
                    fclose(f);
                }
                Rpixels_free(pixels);
            }
        }
        Rshutdown();
        db_free(&g_app.db);
        return 0;
    }

    while (Rwait_frame(-1.0)) {
    }
    save_db();
    close(lock_fd);
    Rshutdown();
    db_free(&g_app.db);
    return 0;
}
