#include "ui.h"

#include "config.h"
#include "logo.h"
#include "model.h"
#include "rpc.h"
#include "x11.h"

#include "cui.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

enum { TAB_ALL, TAB_ACTIVE, TAB_WAITING, TAB_STOPPED, TAB_COMPLETE, TAB_COUNT };
enum { HIST = 72 };

typedef struct {
    char gid[32];
    char status[24];
    bool checked;
} Known;

typedef struct {
    Config cfg;
    Config edit;
    Rpc rpc;
    Download *items;
    int count;
    int cap;
    GlobalStat gs;
    bool connected;
    bool engine_applied;
    int tab;
    bool show_stats;
    char filter[128];
    bool focus_search;
    char selected[32];
    bool show_add;
    bool show_settings;
    int settings_tab;
    int settings_seen_tab;
    char add_urls[16384];
    char add_dir[1024];
    char add_name[256];
    int add_conn;
    char status[256];
    double last_poll;
    double next_clip;
    Known *known;
    int known_count;
    char clip_link[2048];
    char last_clip[2048];
    int clip_toast;
    double last_click;
    char last_click_gid[32];
    GreedFrame frame;
    bool frame_ready;
    bool demo_details;
    float hist_down[HIST];
    float hist_up[HIST];
    char hist_text[HIST][16];
    const char *hist_labels[HIST];
    int hist_count;
} App;

static const char *tab_names[TAB_COUNT] = {"All", "Active", "Waiting", "Stopped", "Complete"};
static const char *tab_icons[TAB_COUNT] = {CUI_ICON_LAYERS, CUI_ICON_ACTIVITY, CUI_ICON_CLOCK,
                                           CUI_ICON_CIRCLE_ALERT, CUI_ICON_CIRCLE_CHECK};

static void set_status(App *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof a->status, fmt, ap);
    va_end(ap);
}

static void apply_theme(cui_ctx *ui) {
    cui_theme *t = cui_theme_get(ui);
    t->colors[CUI_TOKEN_BACKGROUND] = CUI_RGB(0x121419);
    t->colors[CUI_TOKEN_SURFACE] = CUI_RGB(0x16181d);
    t->colors[CUI_TOKEN_SURFACE_RAISED] = CUI_RGB(0x1d2026);
    t->colors[CUI_TOKEN_SURFACE_MUTED] = CUI_RGB(0x16181d);
    t->colors[CUI_TOKEN_FOREGROUND] = CUI_RGB(0xecefee);
    t->colors[CUI_TOKEN_TEXT_SECONDARY] = CUI_RGB(0x9a9fa4);
    t->colors[CUI_TOKEN_TEXT_MUTED] = CUI_RGB(0x7d7977);
    t->colors[CUI_TOKEN_BORDER] = CUI_RGB(0x2a2d34);
    t->colors[CUI_TOKEN_BORDER_SUBTLE] = CUI_RGB(0x1e2126);
    t->colors[CUI_TOKEN_BORDER_STRONG] = CUI_RGB(0x47484e);
    t->colors[CUI_TOKEN_ACCENT] = CUI_RGB(0x95849b);
    t->colors[CUI_TOKEN_ACCENT_STRONG] = CUI_RGB(0xcbaeb9);
    t->colors[CUI_TOKEN_ACCENT_SUBTLE] = CUI_RGB(0x2a2d34);
    t->colors[CUI_TOKEN_ACCENT_FOREGROUND] = CUI_RGB(0x121419);
    t->colors[CUI_TOKEN_SUCCESS] = CUI_RGB(0x8fa99a);
    t->colors[CUI_TOKEN_WARNING] = CUI_RGB(0xb49a70);
    t->colors[CUI_TOKEN_DANGER] = CUI_RGB(0xf87171);
    t->colors[CUI_TOKEN_INFO] = CUI_RGB(0x95849b);
    t->colors[CUI_TOKEN_HOVER] = CUI_RGB(0x1d2026);
    t->colors[CUI_TOKEN_ACTIVE] = CUI_RGB(0x2a2d34);
    t->colors[CUI_TOKEN_FOCUS_RING] = CUI_RGB(0xcbaeb9);
    t->colors[CUI_TOKEN_SERIES_1] = CUI_RGB(0xcbaeb9);
    t->colors[CUI_TOKEN_SERIES_2] = CUI_RGB(0x9a9fa4);
    t->colors[CUI_TOKEN_SERIES_3] = CUI_RGB(0x7d7977);
    t->colors[CUI_TOKEN_SERIES_4] = CUI_RGB(0x8fa99a);
    t->colors[CUI_TOKEN_SERIES_5] = CUI_RGB(0xf87171);
    t->colors[CUI_TOKEN_SERIES_6] = CUI_RGB(0xb49a70);
}

static cui_color status_color(const Download *d) {
    if (!strcmp(d->status, "active")) return CUI_COLOR_ACCENT_STRONG;
    if (!strcmp(d->status, "complete")) return CUI_COLOR_SUCCESS;
    if (!strcmp(d->status, "paused")) return CUI_COLOR_WARNING;
    if (!strcmp(d->status, "error")) return CUI_COLOR_DANGER;
    return CUI_COLOR_TEXT_MUTED;
}

static cui_tone status_tone(const Download *d) {
    if (!strcmp(d->status, "active")) return CUI_TONE_ACCENT;
    if (!strcmp(d->status, "complete")) return CUI_TONE_SUCCESS;
    if (!strcmp(d->status, "paused")) return CUI_TONE_WARNING;
    if (!strcmp(d->status, "error")) return CUI_TONE_DANGER;
    return CUI_TONE_NEUTRAL;
}

static Download *find_item(App *a, const char *gid) {
    for (int i = 0; i < a->count; i++)
        if (!strcmp(a->items[i].gid, gid)) return &a->items[i];
    return NULL;
}

static void notify_send(const char *name) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("notify-send", "notify-send", "Greed — download complete", name, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
}

static void remember_statuses(App *a, const Download *items, int n, bool notify) {
    Known *next = n ? calloc((size_t)n, sizeof *next) : NULL;
    for (int i = 0; i < n; i++) {
        snprintf(next[i].gid, sizeof next[i].gid, "%s", items[i].gid);
        snprintf(next[i].status, sizeof next[i].status, "%s", items[i].status);
        for (int k = 0; k < a->known_count; k++) {
            if (strcmp(a->known[k].gid, items[i].gid)) continue;
            next[i].checked = a->known[k].checked;
            if (notify && a->cfg.notify_on_complete && !strcmp(items[i].status, "complete") &&
                strcmp(a->known[k].status, "complete"))
                notify_send(items[i].name);
            break;
        }
    }
    free(a->known);
    a->known = next;
    a->known_count = n;
}

static void poll_engine(App *a) {
    Download *fresh = NULL;
    int n = 0, cap = 0;
    bool ok = false;

    struct json_object *waiting = json_object_new_array();
    json_object_array_add(waiting, json_object_new_int(0));
    json_object_array_add(waiting, json_object_new_int(1000));
    struct json_object *stopped = json_object_new_array();
    json_object_array_add(stopped, json_object_new_int(0));
    json_object_array_add(stopped, json_object_new_int(1000));

    const char *methods[3] = {"aria2.tellActive", "aria2.tellWaiting", "aria2.tellStopped"};
    struct json_object *params[3] = {NULL, waiting, stopped};
    for (int m = 0; m < 3; m++) {
        struct json_object *v = rpc_call(&a->rpc, methods[m], params[m]);
        if (!v) continue;
        ok = true;
        size_t len = json_object_array_length(v);
        for (size_t i = 0; i < len; i++) {
            Download d;
            if (!download_parse(json_object_array_get_idx(v, i), &d)) continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 32;
                Download *p = realloc(fresh, (size_t)cap * sizeof *fresh);
                if (!p) break;
                fresh = p;
            }
            fresh[n++] = d;
        }
        json_object_put(v);
    }

    if (!ok) {
        free(fresh);
        a->connected = false;
        memset(&a->gs, 0, sizeof a->gs);
        return;
    }
    a->connected = true;
    remember_statuses(a, fresh, n, a->known_count > 0);
    free(a->items);
    a->items = fresh;
    a->count = n;
    a->cap = cap;
    if (a->selected[0] && !find_item(a, a->selected)) a->selected[0] = 0;

    struct json_object *st = rpc_call(&a->rpc, "aria2.getGlobalStat", NULL);
    if (st) {
        const char *keys[5] = {"downloadSpeed", "uploadSpeed", "numActive", "numWaiting",
                               "numStopped"};
        unsigned long long *out[5] = {&a->gs.down_speed, &a->gs.up_speed, &a->gs.active,
                                      &a->gs.waiting, &a->gs.stopped};
        for (int i = 0; i < 5; i++) {
            struct json_object *o = json_object_object_get(st, keys[i]);
            if (o) *out[i] = strtoull(json_object_get_string(o), NULL, 10);
        }
        json_object_put(st);
    }
}

static void sample_speed(App *a) {
    if (a->hist_count < HIST) a->hist_count++;
    else {
        memmove(a->hist_down, a->hist_down + 1, sizeof(float) * (HIST - 1));
        memmove(a->hist_up, a->hist_up + 1, sizeof(float) * (HIST - 1));
    }
    int last = a->hist_count - 1;
    a->hist_down[last] = (float)(a->gs.down_speed / 1024.0);
    a->hist_up[last] = (float)(a->gs.up_speed / 1024.0);
    for (int i = 0; i < a->hist_count; i++) {
        if (i == last) snprintf(a->hist_text[i], sizeof a->hist_text[i], "now");
        else if ((last - i) % 12 == 0)
            snprintf(a->hist_text[i], sizeof a->hist_text[i], "-%ds", (int)((last - i) * 0.8));
        else a->hist_text[i][0] = 0;
        a->hist_labels[i] = a->hist_text[i];
    }
}

static void apply_engine_options(App *a) {
    if (!a->connected) return;
    if (aria2_apply_options(&a->rpc, &a->cfg)) a->engine_applied = true;
}

static bool looks_like_link(const char *s) {
    return !strncmp(s, "http://", 7) || !strncmp(s, "https://", 8) || !strncmp(s, "ftp://", 6) ||
           !strncmp(s, "magnet:", 7);
}

static void poll_clipboard(App *a, cui_ctx *ui) {
    if (!a->cfg.clipboard_monitor) return;
    double now = cui_time(ui);
    if (now < a->next_clip) return;
    a->next_clip = now + 1.2;

    char text[2048] = {0};
    FILE *p = popen("timeout 1 wl-paste -n 2>/dev/null", "r");
    if (!p) return;
    if (!fgets(text, sizeof text, p)) text[0] = 0;
    pclose(p);
    size_t len = strlen(text);
    while (len && (text[len - 1] == '\n' || text[len - 1] == '\r')) text[--len] = 0;
    if (!looks_like_link(text) || !strcmp(text, a->last_clip)) return;
    snprintf(a->last_clip, sizeof a->last_clip, "%s", text);
    snprintf(a->clip_link, sizeof a->clip_link, "%s", text);
    a->clip_toast = cui_toast(ui, "Link copied", .description = a->clip_link,
                              .action = "Download", .tone = CUI_TONE_INFO, .duration = 8);
}

static bool is_torrent_path(const char *s) {
    size_t n = strlen(s);
    return n > 8 && !strcmp(s + n - 8, ".torrent");
}

static void add_links(App *a, const char *text, const char *dir, const char *out, int conn) {
    char *copy = strdup(text);
    if (!copy) return;
    bool single = !strchr(text, '\n');
    int added = 0;
    char err[256] = {0};

    for (char *line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
        while (*line == ' ' || *line == '\t') line++;
        size_t len = strlen(line);
        while (len && (line[len - 1] == ' ' || line[len - 1] == '\r' || line[len - 1] == '\t'))
            line[--len] = 0;
        if (!*line) continue;

        struct json_object *opts = json_object_new_object();
        json_object_object_add(opts, "dir", json_object_new_string(dir));
        if (out && out[0] && single) json_object_object_add(opts, "out", json_object_new_string(out));
        if (conn > 1) {
            json_object_object_add(opts, "split", json_object_new_int(conn));
            json_object_object_add(opts, "max-connection-per-server", json_object_new_int(conn));
        }

        struct json_object *result = NULL;
        if (is_torrent_path(line) && access(line, R_OK) == 0) {
            size_t b64len = 0;
            char *b64 = base64_file(line, &b64len);
            if (b64) {
                struct json_object *params = json_object_new_array();
                json_object_array_add(params, json_object_new_string_len(b64, (int)b64len));
                json_object_array_add(params, json_object_new_array());
                json_object_array_add(params, opts);
                result = rpc_call(&a->rpc, "aria2.addTorrent", params);
                free(b64);
            } else {
                json_object_put(opts);
                snprintf(err, sizeof err, "cannot read %s", line);
                break;
            }
        } else {
            struct json_object *params = json_object_new_array();
            struct json_object *urls = json_object_new_array();
            json_object_array_add(urls, json_object_new_string(line));
            json_object_array_add(params, urls);
            json_object_array_add(params, opts);
            result = rpc_call(&a->rpc, "aria2.addUri", params);
        }
        if (!result) {
            snprintf(err, sizeof err, "%s", a->rpc.error);
            break;
        }
        json_object_put(result);
        added++;
    }
    free(copy);

    if (err[0]) {
        set_status(a, "Added %d, error: %s", added, err);
    } else {
        set_status(a, "Added %d download%s", added, added == 1 ? "" : "s");
        a->add_urls[0] = 0;
        a->add_name[0] = 0;
        a->show_add = false;
    }
    a->last_poll = 0;
}

static void open_add(App *a) {
    a->show_add = true;
    snprintf(a->add_dir, sizeof a->add_dir, "%s", a->cfg.download_dir);
    a->add_conn = a->cfg.max_concurrent;
}

static void open_settings(App *a) {
    a->edit = a->cfg;
    a->settings_tab = 0;
    a->settings_seen_tab = -1;
    a->show_settings = true;
}

static Known *known_find(App *a, const char *gid) {
    for (int i = 0; i < a->known_count; i++)
        if (!strcmp(a->known[i].gid, gid)) return &a->known[i];
    return NULL;
}

static int checked_count(App *a) {
    int n = 0;
    for (int i = 0; i < a->known_count; i++) n += a->known[i].checked;
    return n;
}

static void clear_checks(App *a) {
    for (int i = 0; i < a->known_count; i++) a->known[i].checked = false;
}

static void toggle_download(App *a, Download *d) {
    bool paused = !strcmp(d->status, "paused");
    struct json_object *params = json_object_new_array();
    json_object_array_add(params, json_object_new_string(d->gid));
    struct json_object *v = rpc_call(&a->rpc, paused ? "aria2.unpause" : "aria2.pause", params);
    if (!v) {
        set_status(a, "Error: %s", a->rpc.error);
        return;
    }
    json_object_put(v);
    set_status(a, paused ? "Resumed" : "Paused");
    a->last_poll = 0;
}

static void toggle_selected(App *a) {
    Download *d = find_item(a, a->selected);
    if (d) toggle_download(a, d);
}

static void bulk_toggle(App *a, bool pause) {
    int n = 0;
    for (int i = 0; i < a->known_count; i++) {
        if (!a->known[i].checked) continue;
        Download *d = find_item(a, a->known[i].gid);
        if (!d) continue;
        bool can = pause ? (!strcmp(d->status, "active") || !strcmp(d->status, "waiting"))
                         : !strcmp(d->status, "paused");
        if (!can) continue;
        toggle_download(a, d);
        n++;
    }
    if (n) set_status(a, pause ? "Paused %d downloads" : "Resumed %d downloads", n);
}

static void remove_download(App *a, Download *d) {
    struct json_object *params = json_object_new_array();
    json_object_array_add(params, json_object_new_string(d->gid));
    struct json_object *v = rpc_call(&a->rpc, download_is_finished(d) ? "aria2.removeDownloadResult"
                                                                     : "aria2.forceRemove",
                                     params);
    if (!v) {
        set_status(a, "Error: %s", a->rpc.error);
        return;
    }
    json_object_put(v);
    if (!download_is_finished(d)) {
        struct json_object *purge = json_object_new_array();
        json_object_array_add(purge, json_object_new_string(d->gid));
        struct json_object *r = rpc_call(&a->rpc, "aria2.removeDownloadResult", purge);
        if (r) json_object_put(r);
    }
    set_status(a, "Removed");
    if (!strcmp(a->selected, d->gid)) a->selected[0] = 0;
    a->last_poll = 0;
}

static void remove_selected(App *a) {
    Download *d = find_item(a, a->selected);
    if (d) remove_download(a, d);
}

static void bulk_remove(App *a) {
    int n = 0;
    for (int i = 0; i < a->known_count; i++) {
        if (!a->known[i].checked) continue;
        Download *d = find_item(a, a->known[i].gid);
        if (!d) continue;
        remove_download(a, d);
        n++;
    }
    if (n) set_status(a, "Removed %d downloads", n);
    clear_checks(a);
}

static void start_engine(App *a) {
    if (!aria2_available()) {
        set_status(a, "aria2 binary not found");
        return;
    }
    if (aria2_ensure_running(&a->cfg, true)) {
        a->connected = true;
        a->engine_applied = false;
        apply_engine_options(a);
        set_status(a, "aria2 started");
        a->last_poll = 0;
    } else {
        set_status(a, "aria2 failed to start");
    }
}

static bool tab_matches(int tab, const Download *d) {
    switch (tab) {
    case TAB_ACTIVE: return !strcmp(d->status, "active");
    case TAB_WAITING: return !strcmp(d->status, "waiting") || !strcmp(d->status, "paused");
    case TAB_STOPPED: return !strcmp(d->status, "error") || !strcmp(d->status, "removed");
    case TAB_COMPLETE: return !strcmp(d->status, "complete");
    default: return true;
    }
}

static bool contains_ci(const char *haystack, const char *needle) {
    if (!needle[0]) return true;
    size_t n = strlen(needle);
    for (const char *p = haystack; *p; p++)
        if (!strncasecmp(p, needle, n)) return true;
    return false;
}

static int collect_rows(App *a, int *rows, int cap) {
    int n = 0;
    for (int i = 0; i < a->count; i++) {
        const Download *d = &a->items[i];
        if (!tab_matches(a->tab, d)) continue;
        if (!contains_ci(d->name, a->filter) && !contains_ci(d->url, a->filter)) continue;
        if (rows && n < cap) rows[n] = i;
        n++;
    }
    return n;
}

static cui_texture logo_texture(cui_ctx *ui) {
    static cui_texture tex;
    if (tex) return tex;
    int n = GREED_LOGO_W * GREED_LOGO_H;
    unsigned char *rgba = malloc((size_t)n * 4);
    if (!rgba) return 0;
    for (int i = 0; i < n; i++) {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = greed_logo_alpha[i];
    }
    tex = cui_image_create(ui, GREED_LOGO_W, GREED_LOGO_H, rgba);
    free(rgba);
    return tex;
}

static void titlebar(App *a, cui_ctx *ui) {
    char down[32], up[32];
    fmt_speed(down, sizeof down, a->gs.down_speed);
    fmt_speed(up, sizeof up, a->gs.up_speed);

    cui_row(ui, .width = CUI_FILL, .height = CUI_PX(46), .bg = CUI_COLOR_SURFACE, .pad_x = 10,
            .gap = 8, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .id = "dragzone", .width = CUI_FILL, .height = CUI_FILL, .gap = 8,
                .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, CUI_ICON_DOWNLOAD, 18, CUI_COLOR_ACCENT_STRONG);
            cui_texture logo = logo_texture(ui);
            if (logo)
                cui_image(ui, logo, .width = CUI_PX(GREED_LOGO_W / 2),
                          .height = CUI_PX(GREED_LOGO_H / 2), .tint = CUI_COLOR_FOREGROUND);
            else
                cui_text(ui, "Greed", .font = CUI_FONT_MEDIUM, .size = 15);
            cui_gap(ui, 6);
            cui_icon(ui, CUI_ICON_ARROW_DOWN, 13, CUI_COLOR_ACCENT_STRONG);
            cui_text(ui, down, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
            cui_icon(ui, CUI_ICON_ARROW_UP, 13, CUI_COLOR_TEXT_MUTED);
            cui_text(ui, up, .size = 12, .color = CUI_COLOR_TEXT_MUTED, .tabular = true);
        }
        if (cui_button(ui, "Add", .icon = CUI_ICON_PLUS, .variant = CUI_BUTTON_PRIMARY,
                       .size = CUI_SIZE_SM))
            open_add(a);
        if (cui_icon_button(ui, CUI_ICON_SETTINGS, .size = CUI_SIZE_SM, .tooltip = "Settings"))
            open_settings(a);
        cui_box(ui, .width = CUI_PX(1), .height = CUI_PX(18), .bg = CUI_COLOR_BORDER) {}
        if (cui_icon_button(ui, CUI_ICON_MINUS, .size = CUI_SIZE_SM, .tooltip = "Minimize"))
            frame_minimize(&a->frame);
        bool maximized = frame_is_maximized(&a->frame);
        if (cui_icon_button(ui, maximized ? CUI_ICON_MINIMIZE_2 : CUI_ICON_MAXIMIZE_2,
                            .size = CUI_SIZE_SM, .tooltip = maximized ? "Restore" : "Maximize"))
            frame_toggle_maximize(&a->frame);
        if (cui_icon_button(ui, CUI_ICON_X, .size = CUI_SIZE_SM, .tooltip = "Close"))
            cui_app_quit(NULL);
    }
}

static void frame_interactions(App *a, cui_ctx *ui) {
    if (!cui_mouse_clicked(ui, CUI_MOUSE_LEFT)) return;
    cui_vec2 m = cui_mouse_pos(ui);
    cui_vec2 v = cui_viewport_size(ui);

    if (!frame_is_maximized(&a->frame)) {
        const float edge = 5.0f;
        bool left = m.x <= edge, right = m.x >= v.x - edge;
        bool top = m.y <= edge, bottom = m.y >= v.y - edge;
        int dir = -1;
        if (top && left) dir = 0;
        else if (top && right) dir = 2;
        else if (bottom && left) dir = 6;
        else if (bottom && right) dir = 4;
        else if (top) dir = 1;
        else if (bottom) dir = 5;
        else if (left) dir = 7;
        else if (right) dir = 3;
        if (dir >= 0) {
            frame_begin_resize(&a->frame, dir);
            return;
        }
    }

    cui_rect dz = cui_get_rect(ui, cui_make_id(ui, "dragzone"));
    if (m.x >= dz.x && m.x <= dz.x + dz.w && m.y >= dz.y && m.y <= dz.y + dz.h)
        frame_begin_move(&a->frame);
}

typedef struct {
    const float *values;
    int count;
    cui_color color;
} SparkData;

/* Same Catmull-Rom curve as the statistics chart, small enough for the sidebar. */
static void sidebar_spark_paint(cui_ctx *ui, cui_rect r, void *user) {
    SparkData *d = user;
    if (d->count < 2 || r.w < 4 || r.h < 4) return;
    float lo = d->values[0], hi = d->values[0];
    for (int i = 1; i < d->count; i++) {
        if (d->values[i] < lo) lo = d->values[i];
        if (d->values[i] > hi) hi = d->values[i];
    }
    if (hi - lo < 1e-6f) hi = lo + 1;

    cui_vec2 in[HIST];
    for (int i = 0; i < d->count; i++)
        in[i] = (cui_vec2){r.x + 2 + (r.w - 4) * (float)i / (float)(d->count - 1),
                           r.y + 3 + (r.h - 6) * (1 - (d->values[i] - lo) / (hi - lo))};

    enum { SEG = 8 };
    cui_vec2 out[HIST * SEG];
    int k = 0;
    for (int i = 0; i < d->count - 1; i++) {
        cui_vec2 p0 = in[i > 0 ? i - 1 : i], p1 = in[i], p2 = in[i + 1],
                  p3 = in[i + 2 < d->count ? i + 2 : i + 1];
        for (int s = 0; s < SEG; s++) {
            float t = (float)s / (float)SEG, t2 = t * t, t3 = t2 * t;
            float m1 = (p2.y - p0.y) * 0.5f, m2 = (p3.y - p1.y) * 0.5f;
            float dy = p2.y - p1.y;
            if ((dy > 0 && m1 < 0) || (dy < 0 && m1 > 0)) m1 = 0;
            if ((dy > 0 && m2 < 0) || (dy < 0 && m2 > 0)) m2 = 0;
            float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t;
            float h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
            out[k++] = (cui_vec2){p1.x + (p2.x - p1.x) * t,
                                  h00 * p1.y + h10 * m1 + h01 * p2.y + h11 * m2};
        }
    }
    out[k++] = in[d->count - 1];
    cui_draw_polyline(ui, out, k, 1.75f, d->color, false);
    cui_draw_circle(ui, out[k - 1], 3, d->color);
}

static void sidebar(App *a, cui_ctx *ui) {
    int counts[TAB_COUNT] = {0};
    for (int i = 0; i < a->count; i++)
        for (int t = 0; t < TAB_COUNT; t++)
            if (tab_matches(t, &a->items[i])) counts[t]++;

    cui_column(ui, .width = CUI_PX(198), .height = CUI_FILL, .bg = CUI_COLOR_SURFACE, .pad = 10,
               .gap = 2) {
        cui_nav_section(ui, "LIBRARY");
        for (int t = 0; t < TAB_COUNT; t++) {
            char badge[16] = {0};
            if (counts[t] > 0) snprintf(badge, sizeof badge, "%d", counts[t]);
            if (cui_nav_item(ui, tab_names[t], .icon = tab_icons[t],
                             .badge = badge[0] ? badge : NULL,
                             .selected = !a->show_stats && a->tab == t, .group = "tabs")) {
                a->tab = t;
                a->show_stats = false;
            }
        }
        cui_box(ui, .height = CUI_PX(14)) {}
        cui_separator(ui);
        cui_nav_section(ui, "INSIGHTS");
        if (cui_nav_item(ui, "Statistics", .icon = CUI_ICON_CHART_LINE,
                         .selected = a->show_stats, .group = "tabs"))
            a->show_stats = true;
        cui_box(ui, .height = CUI_PX(14)) {}
        cui_separator(ui);
        cui_nav_section(ui, "TRANSFER");
        cui_row(ui, .width = CUI_FILL, .gap = 6, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(6), .height = CUI_PX(6), .radius = 3,
                    .bg = CUI_COLOR_ACCENT_STRONG);
            cui_text(ui, "Download", .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);
            cui_spacer(ui);
            cui_text(ui, "1 min", .size = 10, .color = CUI_COLOR_TEXT_MUTED);
        }
        SparkData spark = {a->hist_down, a->hist_count, CUI_COLOR_ACCENT_STRONG};
        cui_canvas(ui, "sidebar-spark", CUI_FILL, CUI_PX(32), sidebar_spark_paint, &spark,
                   sizeof spark);
        char num[32];
        cui_row(ui, .width = CUI_FILL) {
            cui_text(ui, "Active", .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            cui_spacer(ui);
            snprintf(num, sizeof num, "%llu", a->gs.active);
            cui_text(ui, num, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
        }
        cui_row(ui, .width = CUI_FILL) {
            cui_text(ui, "Queued", .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            cui_spacer(ui);
            snprintf(num, sizeof num, "%llu", a->gs.waiting);
            cui_text(ui, num, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
        }
        cui_spacer(ui);
        cui_separator(ui);
        cui_row(ui, .width = CUI_FILL, .pad_top = 8, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(8), .height = CUI_PX(8), .radius = 4,
                    .bg = a->connected ? CUI_COLOR_SUCCESS : CUI_COLOR_DANGER);
            cui_text(ui, a->connected ? "aria2 connected" : "aria2 offline", .size = 12,
                     .color = CUI_COLOR_TEXT_MUTED);
        }
    }
}

static void kv(cui_ctx *ui, const char *key, const char *value) {
    cui_row(ui, .width = CUI_FILL) {
        cui_text(ui, key, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        cui_spacer(ui);
        cui_text(ui, value, .size = 12, .width = CUI_PX(170), .align = CUI_TEXT_RIGHT, .nowrap = true);
    }
}

static void details(App *a, cui_ctx *ui) {
    Download *d = find_item(a, a->selected);
    if (!d) return;
    cui_separator(ui);
    cui_column(ui, .width = CUI_PX(290), .height = CUI_FILL, .bg = CUI_COLOR_SURFACE, .pad = 14,
               .gap = 8) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_text(ui, "DETAILS", .size = 11, .font = CUI_FONT_MEDIUM,
                     .color = CUI_COLOR_TEXT_MUTED);
            cui_spacer(ui);
            if (cui_icon_button(ui, CUI_ICON_X, .size = CUI_SIZE_SM, .tooltip = "Close"))
                a->selected[0] = 0;
        }
        cui_scroll(ui, .id = "details", .height = CUI_FILL, .gap = 10) {
            cui_text(ui, d->name, .font = CUI_FONT_MEDIUM, .size = 15, .line_height = 1.3f);
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_badge(ui, download_status_label(d), .tone = status_tone(d), .dot = true);
            }
            double p = download_progress(d);
            cui_progress(ui, (float)p, .height = 6, .tone = status_tone(d));
            char a1[32], a2[32];
            fmt_bytes(a1, sizeof a1, d->completed);
            fmt_bytes(a2, sizeof a2, d->total);
            char pct[32], amount[80];
            snprintf(pct, sizeof pct, "%.1f%%", p * 100.0);
            snprintf(amount, sizeof amount, "%s / %s", a1, a2);
            cui_row(ui, .width = CUI_FILL) {
                cui_text(ui, pct, .size = 12, .tabular = true);
                cui_spacer(ui);
                cui_text(ui, amount, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .tabular = true);
            }
            char speed[32], up[32], eta[32];
            fmt_speed(speed, sizeof speed, d->down_speed);
            fmt_speed(up, sizeof up, d->up_speed);
            unsigned long long secs;
            if (download_eta(d, &secs)) fmt_eta(eta, sizeof eta, secs);
            else snprintf(eta, sizeof eta, "—");
            kv(ui, "Speed", speed);
            kv(ui, "Upload", up);
            kv(ui, "ETA", eta);
            char num[32];
            snprintf(num, sizeof num, "%u", d->connections);
            kv(ui, "Connections", num);
            if (d->is_torrent) {
                snprintf(num, sizeof num, "%u", d->seeders);
                kv(ui, "Seeders", num);
            }
            if (d->file_count > 1) {
                snprintf(num, sizeof num, "%d", d->file_count);
                kv(ui, "Files", num);
            }
            cui_text(ui, "Directory", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            cui_text(ui, d->dir, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .nowrap = true);
            cui_text(ui, "GID", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            cui_text(ui, d->gid, .size = 12, .font = CUI_FONT_MONO,
                     .color = CUI_COLOR_TEXT_SECONDARY);
            if (d->error[0])
                cui_alert(ui, "Download failed", .description = d->error, .tone = CUI_TONE_DANGER,
                          .icon = CUI_ICON_CIRCLE_ALERT);
            bool paused = !strcmp(d->status, "paused");
            bool running = !strcmp(d->status, "active") || !strcmp(d->status, "waiting");
            if ((paused || running) &&
                cui_button(ui, paused ? "Resume" : "Pause",
                           .icon = paused ? CUI_ICON_PLAY : CUI_ICON_PAUSE, .size = CUI_SIZE_SM,
                           .full_width = true))
                toggle_selected(a);
            cui_row(ui, .gap = 8) {
                if (cui_button(ui, "Folder", .icon = CUI_ICON_FOLDER_OPEN, .size = CUI_SIZE_SM,
                               .variant = CUI_BUTTON_GHOST, .width = CUI_FILL))
                    cui_app_open_url(d->dir);
                if (cui_button(ui, "Remove", .icon = CUI_ICON_TRASH_2, .size = CUI_SIZE_SM,
                               .variant = CUI_BUTTON_GHOST, .width = CUI_FILL))
                    remove_selected(a);
            }
        }
    }
}

static void download_table(App *a, cui_ctx *ui) {
    int rows[512];
    int n = collect_rows(a, rows, 512);
    if (n > 512) n = 512;
    if (n == 0) {
        cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_CENTER,
                   .justify = CUI_JUSTIFY_CENTER, .gap = 10) {
            cui_icon(ui, CUI_ICON_DOWNLOAD, 36, CUI_COLOR_TEXT_MUTED);
            cui_text(ui, a->count ? "Nothing in this tab" : "No downloads yet",
                     .font = CUI_FONT_DISPLAY, .size = 18);
            cui_text(ui, "Paste a link, or add a magnet / .torrent file.", .size = 13,
                     .color = CUI_COLOR_TEXT_SECONDARY);
            if (cui_button(ui, "Add download", .icon = CUI_ICON_PLUS,
                           .variant = CUI_BUTTON_PRIMARY))
                open_add(a);
        }
        return;
    }

    static const cui_table_column cols[] = {
        {"", CUI_PX(28), CUI_TEXT_START, false},
        {"NAME", CUI_FILL, CUI_TEXT_START, false},
        {"SIZE", CUI_PX(80), CUI_TEXT_RIGHT, false},
        {"PROGRESS", CUI_PX(150), CUI_TEXT_START, false},
        {"SPEED", CUI_PX(92), CUI_TEXT_RIGHT, false},
        {"ETA", CUI_PX(64), CUI_TEXT_RIGHT, false},
        {"STATUS", CUI_PX(104), CUI_TEXT_START, false},
    };
    cui_table(ui, "downloads", cols, 7, .height = CUI_FILL, .striped = true, .compact = true,
              .selectable = true) {
        for (int r = 0; r < n; r++) {
            Download *d = &a->items[rows[r]];
            bool selected = !strcmp(a->selected, d->gid);
            bool clicked = cui_table_row(ui, selected);
            bool on_pick = false;
            cui_rect pr = {0};
            cui_vec2 m = {0};
            cui_table_cell(ui); {
                Known *k = known_find(a, d->gid);
                if (k) {
                    char pick[64];
                    snprintf(pick, sizeof pick, "##pick-%s", d->gid);
                    /* The table row owns the click, so the pick column is hit-tested here. */
                    pr = cui_get_rect(ui, cui_make_id(ui, pick) ^ 0xC0EC0001u);
                    cui_checkbox(ui, pick, &k->checked);
                    m = cui_mouse_pos(ui);
                    if (clicked && m.x >= pr.x && m.x <= pr.x + pr.w && m.y >= pr.y &&
                        m.y <= pr.y + pr.h) {
                        k->checked = !k->checked;
                        on_pick = true;
                    }
                }
            }
            if (clicked && !on_pick) {
                double now = cui_time(ui);
                if (now - a->last_click < 0.35 && !strcmp(a->last_click_gid, d->gid)) {
                    cui_app_open_url(d->dir);
                } else {
                    snprintf(a->selected, sizeof a->selected, "%s", d->gid);
                }
                a->last_click = now;
                snprintf(a->last_click_gid, sizeof a->last_click_gid, "%s", d->gid);
            }
            cui_table_cell(ui); {
                cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_box(ui, .width = CUI_PX(8), .height = CUI_PX(8), .radius = 4,
                            .bg = status_color(d));
                    cui_text(ui, d->name, .width = CUI_FILL, .nowrap = true);
                }
            }
            cui_table_cell(ui); {
                char size[32];
                if (d->total) fmt_bytes(size, sizeof size, d->total);
                else snprintf(size, sizeof size, "—");
                cui_text(ui, size, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .align = CUI_TEXT_RIGHT, .width = CUI_FILL, .tabular = true);
            }
            cui_table_cell(ui); {
                double p = download_progress(d);
                char pct[16];
                snprintf(pct, sizeof pct, "%.0f%%", p * 100.0);
                cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_progress(ui, (float)p, .height = 6, .width = CUI_FILL, .tone = status_tone(d));
                    cui_text(ui, pct, .size = 11, .color = CUI_COLOR_TEXT_MUTED,
                             .width = CUI_PX(36), .align = CUI_TEXT_RIGHT, .tabular = true);
                }
            }
            cui_table_cell(ui); {
                char speed[32];
                if (!strcmp(d->status, "active")) fmt_speed(speed, sizeof speed, d->down_speed);
                else snprintf(speed, sizeof speed, "—");
                cui_text(ui, speed, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .align = CUI_TEXT_RIGHT, .width = CUI_FILL, .tabular = true);
            }
            cui_table_cell(ui); {
                char eta[32];
                unsigned long long secs;
                if (download_eta(d, &secs)) fmt_eta(eta, sizeof eta, secs);
                else snprintf(eta, sizeof eta, "—");
                cui_text(ui, eta, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .align = CUI_TEXT_RIGHT, .width = CUI_FILL, .tabular = true);
            }
            cui_table_cell(ui); {
                cui_text(ui, download_status_label(d), .size = 12, .color = status_color(d),
                         .nowrap = true);
            }
        }
    }
}

static int count_status(const App *a, const char *status) {
    int n = 0;
    for (int i = 0; i < a->count; i++)
        if (!strcmp(a->items[i].status, status)) n++;
    return n;
}

static bool disk_usage(const char *path, double *used_gb, double *total_gb,
                       unsigned long long *free_bytes) {
    struct statvfs s;
    if (statvfs(path, &s) != 0 || s.f_blocks == 0 || s.f_frsize == 0) return false;
    unsigned long long total = (unsigned long long)s.f_blocks * s.f_frsize;
    unsigned long long avail = (unsigned long long)s.f_bavail * s.f_frsize;
    *total_gb = (double)total / (1024.0 * 1024.0 * 1024.0);
    *used_gb = (double)(total - avail) / (1024.0 * 1024.0 * 1024.0);
    *free_bytes = avail;
    return true;
}

static void stats_page(App *a, cui_ctx *ui) {
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 14, .gap = 12) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_column(ui, .gap = 2) {
                cui_heading(ui, "Statistics", 2);
                cui_muted(ui, a->connected ? "Live from the aria2 engine."
                                           : "Engine offline — showing the last samples.");
            }
            cui_spacer(ui);
            if (a->connected)
                cui_badge(ui, "Live", .tone = CUI_TONE_SUCCESS, .dot = true);
        }
        cui_row(ui, .width = CUI_FILL, .gap = 10, .align = CUI_ALIGN_STRETCH) {
            char v[32];
            snprintf(v, sizeof v, "%d", count_status(a, "active"));
            cui_metric(ui, "Active", v, .icon = CUI_ICON_ACTIVITY);
            snprintf(v, sizeof v, "%d", count_status(a, "waiting") + count_status(a, "paused"));
            cui_metric(ui, "Waiting", v, .icon = CUI_ICON_CLOCK);
            snprintf(v, sizeof v, "%d", count_status(a, "complete"));
            cui_metric(ui, "Complete", v, .icon = CUI_ICON_CIRCLE_CHECK);
        }
        cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 10) {
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_text(ui, "Transfer speed", .font = CUI_FONT_MEDIUM, .size = 14);
                cui_spacer(ui);
                cui_text(ui, "KB/s · last minute", .size = 11, .color = CUI_COLOR_TEXT_MUTED);
            }
            if (a->hist_count > 1) {
                cui_series series[2] = {
                    {"Download", a->hist_down, a->hist_count, CUI_COLOR_ACCENT_STRONG},
                    {"Upload", a->hist_up, a->hist_count, CUI_COLOR_SERIES(1)},
                };
                cui_line_chart(ui, "speed-history", series, 2, .height = 170,
                               .labels = a->hist_labels, .label_count = a->hist_count,
                               .smooth = true, .area = true);
            } else {
                cui_muted(ui, "Collecting samples…");
            }
        }
        cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_STRETCH) {
            cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 8) {
                cui_text(ui, "Downloads by state", .font = CUI_FONT_MEDIUM, .size = 14);
                float values[5] = {
                    (float)count_status(a, "active"),
                    (float)count_status(a, "waiting"),
                    (float)count_status(a, "paused"),
                    (float)count_status(a, "complete"),
                    (float)(count_status(a, "error") + count_status(a, "removed")),
                };
                static const char *labels[5] = {"Active", "Waiting", "Paused", "Complete",
                                                "Failed"};
                float total = 0;
                for (int i = 0; i < 5; i++) total += values[i];
                if (total > 0)
                    cui_donut_chart(ui, "state-donut", values, labels, 5, .height = 170);
                else
                    cui_muted(ui, "No downloads yet.");
            }
            cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 10) {
                cui_text(ui, "Disk space", .font = CUI_FONT_MEDIUM, .size = 14);
                double used = 0, total = 0;
                unsigned long long free_bytes = 0;
                if (disk_usage(a->cfg.download_dir, &used, &total, &free_bytes)) {
                    float used_gb = (float)((long long)(used * 10.0) / 10.0);
                    float total_gb = (float)((long long)(total * 10.0) / 10.0);
                    cui_meter(ui, "Used", used_gb, total_gb, "GB");
                    char free_text[32];
                    fmt_bytes(free_text, sizeof free_text, free_bytes);
                    cui_row(ui, .width = CUI_FILL) {
                        cui_text(ui, "Free", .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                        cui_spacer(ui);
                        cui_text(ui, free_text, .size = 12, .tabular = true);
                    }
                } else {
                    cui_muted(ui, "Cannot read the download folder.");
                }
                cui_muted(ui, a->cfg.download_dir);
            }
        }
    }
}

static void content(App *a, cui_ctx *ui) {
    if (a->show_stats) {
        stats_page(a, ui);
        return;
    }
    Download *sel = find_item(a, a->selected);
    bool can_pause = sel && (!strcmp(sel->status, "active") || !strcmp(sel->status, "waiting"));
    bool can_resume = sel && !strcmp(sel->status, "paused");

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 14, .gap = 10) {
        if (!a->connected) {
            cui_row(ui, .width = CUI_FILL, .bg = CUI_COLOR_SURFACE_RAISED, .border = 1,
                    .border_color = CUI_COLOR_DANGER, .radius = 8, .pad_x = 12, .pad_y = 8,
                    .gap = 10, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_CIRCLE_ALERT, 16, CUI_COLOR_DANGER);
                cui_text(ui, "aria2 is offline", .font = CUI_FONT_MEDIUM);
                cui_text(ui, "Start the engine to download.", .size = 12,
                         .color = CUI_COLOR_TEXT_SECONDARY);
                cui_spacer(ui);
                if (cui_button(ui, "Start aria2", .size = CUI_SIZE_SM)) start_engine(a);
            }
        }
        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_search(ui, "filter", a->filter, sizeof a->filter, .placeholder = "Search downloads",
                       .width = CUI_FILL, .autofocus = a->focus_search);
            a->focus_search = false;
            char shown[32];
            snprintf(shown, sizeof shown, "%d shown", collect_rows(a, NULL, 0));
            cui_text(ui, shown, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            if (cui_icon_button(ui, CUI_ICON_PAUSE, .size = CUI_SIZE_SM, .tooltip = "Pause",
                                .disabled = !can_pause))
                toggle_selected(a);
            if (cui_icon_button(ui, CUI_ICON_PLAY, .size = CUI_SIZE_SM, .tooltip = "Resume",
                                .disabled = !can_resume))
                toggle_selected(a);
            if (cui_icon_button(ui, CUI_ICON_TRASH_2, .size = CUI_SIZE_SM, .tooltip = "Remove",
                                .disabled = !sel))
                remove_selected(a);
        }
        int picks = checked_count(a);
        if (picks > 0) {
            cui_row(ui, .width = CUI_FILL, .bg = CUI_COLOR_SURFACE_RAISED, .border = 1,
                    .border_color = CUI_COLOR_BORDER, .radius = 8, .pad_x = 12, .pad_y = 6,
                    .gap = 8, .align = CUI_ALIGN_CENTER) {
                char label[48];
                snprintf(label, sizeof label, "%d selected", picks);
                cui_text(ui, label, .font = CUI_FONT_MEDIUM, .size = 12,
                         .color = CUI_COLOR_ACCENT_STRONG);
                cui_spacer(ui);
                if (cui_button(ui, "Pause", .icon = CUI_ICON_PAUSE, .size = CUI_SIZE_SM,
                               .variant = CUI_BUTTON_GHOST))
                    bulk_toggle(a, true);
                if (cui_button(ui, "Resume", .icon = CUI_ICON_PLAY, .size = CUI_SIZE_SM,
                               .variant = CUI_BUTTON_GHOST))
                    bulk_toggle(a, false);
                if (cui_button(ui, "Remove", .icon = CUI_ICON_TRASH_2, .size = CUI_SIZE_SM,
                               .variant = CUI_BUTTON_GHOST))
                    bulk_remove(a);
                if (cui_icon_button(ui, CUI_ICON_X, .size = CUI_SIZE_SM,
                                    .tooltip = "Clear selection"))
                    clear_checks(a);
            }
        }
        download_table(a, ui);
    }
}

static void statusbar(App *a, cui_ctx *ui) {
    char count[48];
    snprintf(count, sizeof count, "%d downloads", a->count);
    cui_status_bar(ui, "statusbar") {
        cui_status_bar_text(ui, a->status);
        cui_status_bar_spacer(ui);
        cui_status_bar_text(ui, count);
    }
}

static void add_dialog(App *a, cui_ctx *ui) {
    if (!a->show_add) return;
    cui_dialog(ui, "Add download", &a->show_add, .width = 560) {
        cui_label(ui, "Links");
        cui_textarea(ui, "links", a->add_urls, sizeof a->add_urls,
                     .placeholder = "One link per line — http, magnet, or .torrent",
                     .height = 104);
        cui_label(ui, "Save to");
        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_input(ui, "add-dir", a->add_dir, sizeof a->add_dir, .width = CUI_FILL);
            if (cui_button(ui, "Browse…", .size = CUI_SIZE_SM)) {
                char path[PATH_MAX];
                if (cui_app_file_dialog(NULL,
                                        &(cui_file_dialog_opts){.kind = CUI_FILE_FOLDER,
                                                                .title = "Save downloads to"},
                                        path, sizeof path))
                    snprintf(a->add_dir, sizeof a->add_dir, "%s", path);
            }
        }
        cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
            cui_input(ui, "add-name", a->add_name, sizeof a->add_name,
                      .placeholder = "Filename (optional, single link)", .width = CUI_FILL);
            double conn = a->add_conn;
            if (cui_number(ui, "add-conn", &conn, 1, 16, .label = "Connections", .step = 1))
                a->add_conn = (int)conn;
        }
        if (cui_button(ui, "Open .torrent…", .icon = CUI_ICON_FILE, .size = CUI_SIZE_SM,
                       .variant = CUI_BUTTON_GHOST)) {
            char path[PATH_MAX];
            if (cui_app_file_dialog(NULL,
                                    &(cui_file_dialog_opts){.kind = CUI_FILE_OPEN,
                                                            .title = "Open torrent",
                                                            .filter_name = "Torrent",
                                                            .filter = "torrent"},
                                    path, sizeof path)) {
                char *text = malloc(sizeof a->add_urls);
                if (text) {
                    snprintf(text, sizeof a->add_urls, "%s", a->add_urls);
                    size_t used = strlen(text);
                    snprintf(text + used, sizeof a->add_urls - used, "%s%s", used ? "\n" : "", path);
                    add_links(a, text, a->add_dir, NULL, a->add_conn);
                    free(text);
                }
            }
        }
        cui_dialog_footer(ui) {
            if (cui_button(ui, "Cancel", .variant = CUI_BUTTON_GHOST)) a->show_add = false;
            if (cui_button(ui, "Add", .variant = CUI_BUTTON_PRIMARY)) {
                const char *name = a->add_name[0] ? a->add_name : NULL;
                add_links(a, a->add_urls, a->add_dir, name, a->add_conn);
            }
        }
    }
}

static void settings_dialog(App *a, cui_ctx *ui) {
    if (!a->show_settings) return;
    static const char *tabs[] = {"General", "Bandwidth", "Torrents"};
    static const char *icons[] = {CUI_ICON_SETTINGS, CUI_ICON_SLIDERS_HORIZONTAL,
                                  CUI_ICON_GLOBE};
    float body_h = cui_viewport_size(ui).y > 760 ? 430 : 300;
    cui_dialog(ui, "Settings", &a->show_settings, .width = 640) {
        cui_tabs(ui, "settings-tabs", tabs, 3, &a->settings_tab, .icons = icons);
        if (a->settings_seen_tab != a->settings_tab) {
            cui_scroll_to(ui, "settings-body", 0);
            a->settings_seen_tab = a->settings_tab;
        }
        cui_scroll(ui, .id = "settings-body", .width = CUI_FILL, .height = CUI_PX(body_h),
                   .gap = 16, .fade_edges = true,
                   .fade_color = CUI_COLOR_SURFACE_RAISED) {
            if (a->settings_tab == 0) {
                cui_heading(ui, "Storage", 4);
                cui_label(ui, "Save to");
                cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_input(ui, "set-dir", a->edit.download_dir,
                              sizeof a->edit.download_dir, .width = CUI_FILL);
                    if (cui_button(ui, "Browse…", .size = CUI_SIZE_SM)) {
                        char path[PATH_MAX];
                        if (cui_app_file_dialog(NULL,
                                                &(cui_file_dialog_opts){
                                                    .kind = CUI_FILE_FOLDER,
                                                    .title = "Save downloads to"},
                                                path, sizeof path))
                            snprintf(a->edit.download_dir, sizeof a->edit.download_dir, "%s",
                                     path);
                    }
                }
                cui_muted(ui, "New downloads start here. Existing downloads keep their folder.");

                cui_heading(ui, "Engine", 4);
                float concurrent = (float)a->edit.max_concurrent;
                if (cui_slider(ui, "Max concurrent downloads", &concurrent, 1, 16,
                               .step = 1, .show_value = true, .width = CUI_FILL))
                    a->edit.max_concurrent = (int)concurrent;
                double port = a->edit.rpc_port;
                if (cui_number(ui, "set-port", &port, 1024, 65535, .label = "RPC port",
                               .step = 1, .width = CUI_PX(240)))
                    a->edit.rpc_port = (int)port;
                cui_muted(ui, "A new RPC port applies after Greed restarts.");

                cui_heading(ui, "Behavior", 4);
                cui_switch(ui, "Start aria2 automatically", &a->edit.auto_start_aria2,
                           .description = "Launch the engine when Greed opens.");
                cui_switch(ui, "Watch clipboard for links", &a->edit.clipboard_monitor,
                           .description = "Offer a download when a link is copied.");
                cui_switch(ui, "Notify when a download completes", &a->edit.notify_on_complete,
                           .description = "Send a desktop notification on completion.");
            } else if (a->settings_tab == 1) {
                cui_heading(ui, "Live throughput", 4);
                if (a->hist_count > 1) {
                    float peak = 0;
                    for (int i = 0; i < a->hist_count; i++)
                        if (a->hist_down[i] > peak) peak = a->hist_down[i];
                    char limit_label[48];
                    cui_chart_annotation limit_line;
                    int lines = 0;
                    float limit = (float)a->edit.max_download_limit;
                    if (limit > 0) {
                        snprintf(limit_label, sizeof limit_label, "Limit %d KB/s",
                                 a->edit.max_download_limit);
                        limit_line = (cui_chart_annotation){
                            .kind = CUI_ANNOTATION_Y, .y = limit, .label = limit_label};
                        lines = 1;
                    }
                    cui_series series = {"Download", a->hist_down, a->hist_count,
                                         CUI_COLOR_ACCENT_STRONG};
                    cui_line_chart(ui, "bw-history", &series, 1, .height = 150,
                                   .labels = a->hist_labels, .label_count = a->hist_count,
                                   .smooth = true, .area = true, .format = "%.0f",
                                   .annotations = lines ? &limit_line : NULL,
                                   .annotation_count = lines, .min = 0,
                                   .max = limit > peak ? limit * 1.15f : 0);
                } else {
                    cui_muted(ui, "Collecting samples…");
                }
                cui_muted(ui, "Download speed in KB/s, sampled over the last minute.");

                cui_heading(ui, "Global limits", 4);
                cui_muted(ui, "0 means unlimited. Limits apply to every download, and reach "
                              "the running engine when you save.");
                float download = (float)a->edit.max_download_limit;
                if (cui_slider(ui, "Download limit", &download, 0, 102400, .step = 128,
                               .show_value = true, .format = "%.0f KB/s", .width = CUI_FILL))
                    a->edit.max_download_limit = (int)download;
                float upload = (float)a->edit.max_upload_limit;
                if (cui_slider(ui, "Upload limit", &upload, 0, 51200, .step = 64,
                               .show_value = true, .format = "%.0f KB/s", .width = CUI_FILL))
                    a->edit.max_upload_limit = (int)upload;
                cui_muted(ui, "The title bar shows the current transfer rates.");
            } else {
                cui_heading(ui, "Seeding", 4);
                cui_switch(ui, "Keep seeding after download",
                           &a->edit.seed_after_complete,
                           .description = "Share finished torrents with other peers.");
                float ratio = a->edit.seed_ratio;
                if (cui_slider(ui, "Stop at ratio", &ratio, 0.5f, 5.0f, .step = 0.5f,
                               .show_value = true, .format = "%.1f×",
                               .disabled = !a->edit.seed_after_complete,
                               .width = CUI_FILL))
                    a->edit.seed_ratio = ratio;
                cui_muted(ui, "aria2 stops a torrent when its uploaded bytes reach this "
                              "multiple of the downloaded bytes.");
            }
        }
        cui_dialog_footer(ui) {
            if (cui_button(ui, "Cancel", .variant = CUI_BUTTON_GHOST)) a->show_settings = false;
            if (cui_button(ui, "Save", .variant = CUI_BUTTON_PRIMARY)) {
                a->cfg = a->edit;
                if (!config_save(&a->cfg)) set_status(a, "Could not save settings");
                else set_status(a, "Settings saved");
                a->engine_applied = false;
                apply_engine_options(a);
                a->show_settings = false;
                a->last_poll = 0;
            }
        }
    }
}

static void handle_keys(App *a, cui_ctx *ui) {
    if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_N)) open_add(a);
    if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_F)) {
        a->show_stats = false;
        a->focus_search = true;
    }
    if (cui_wants_keyboard(ui)) return;
    if (cui_key_pressed(ui, CUI_KEY_DELETE)) remove_selected(a);
    if (cui_key_pressed(ui, CUI_KEY_SPACE)) toggle_selected(a);
    if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) {
        a->selected[0] = 0;
        clear_checks(a);
    }
}

static void frame(cui_ctx *ui, void *user) {
    App *a = user;
    apply_theme(ui);

    if (!a->frame_ready) {
        void *native = cui_app_native_window(NULL);
        if (frame_init(&a->frame, native)) frame_make_frameless(&a->frame);
        a->frame_ready = true;
    }

    handle_keys(a, ui);

    double now = cui_time(ui);
    if (now - a->last_poll >= 0.8) {
        a->last_poll = now;
        bool was_connected = a->connected;
        poll_engine(a);
        if (a->connected && (!was_connected || !a->engine_applied))
            apply_engine_options(a);
        sample_speed(a);
    }
    /* Screenshot/test hook: GREED_DEMO=details selects the first download. */
    if (a->demo_details && !a->selected[0] && a->count)
        snprintf(a->selected, sizeof a->selected, "%s", a->items[0].gid);
    poll_clipboard(a, ui);
    cui_request_wakeup(ui, 0.8f);

    if (a->clip_toast >= 0 && cui_toast_action_clicked(ui, a->clip_toast)) {
        add_links(a, a->clip_link, a->cfg.download_dir, NULL, a->cfg.max_concurrent);
        cui_toast_dismiss(ui, a->clip_toast);
        a->clip_toast = -1;
    }

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .bg = CUI_COLOR_BACKGROUND) {
        titlebar(a, ui);
        frame_interactions(a, ui);
        cui_separator(ui);
        cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_STRETCH) {
            sidebar(a, ui);
            content(a, ui);
            details(a, ui);
        }
        cui_separator(ui);
        statusbar(a, ui);
    }

    add_dialog(a, ui);
    settings_dialog(a, ui);
}

int ui_run(void) {
    Config cfg;
    config_load(&cfg);
    if (cfg.auto_start_aria2) aria2_ensure_running(&cfg, true);

    App *a = calloc(1, sizeof *a);
    if (!a) return 1;
    a->cfg = cfg;
    a->edit = cfg;
    a->clip_toast = -1;
    a->add_conn = cfg.max_concurrent;
    snprintf(a->add_dir, sizeof a->add_dir, "%s", cfg.download_dir);
    snprintf(a->status, sizeof a->status, "Ready");
    rpc_init(&a->rpc, cfg.rpc_port);

    /* Test hook for screenshots: GREED_DEMO=add|settings|settings-bandwidth|settings-torrents|details|stats|empty */
    const char *demo = getenv("GREED_DEMO");
    if (demo && !strcmp(demo, "add")) open_add(a);
    else if (demo && !strcmp(demo, "settings")) open_settings(a);
    else if (demo && !strcmp(demo, "settings-bandwidth")) {
        open_settings(a);
        a->settings_tab = 1;
    } else if (demo && !strcmp(demo, "settings-torrents")) {
        open_settings(a);
        a->settings_tab = 2;
    } else if (demo && !strcmp(demo, "details")) a->demo_details = true;
    else if (demo && !strcmp(demo, "stats")) a->show_stats = true;
    else if (demo && !strcmp(demo, "empty")) snprintf(a->filter, sizeof a->filter, "zzzz");

    int rc = cui_app_run(&(cui_app_desc){.title = "Greed",
                                         .width = 1240,
                                         .height = 780,
                                         .min_width = 1000,
                                         .min_height = 620,
                                         .theme = CUI_THEME_DARK,
                                         .accent = CUI_ACCENT_VIOLET,
                                         .app_id = "greed"},
                         frame, a);

    frame_shutdown(&a->frame);
    free(a->items);
    free(a->known);
    free(a);
    return rc;
}
