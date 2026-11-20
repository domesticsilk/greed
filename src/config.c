#include "config.h"

#include <json-c/json.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char dir_buf[PATH_MAX];

const char *config_dir(void) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0])
        snprintf(dir_buf, sizeof dir_buf, "%s/greed", xdg);
    else if (home && home[0])
        snprintf(dir_buf, sizeof dir_buf, "%s/.config/greed", home);
    else
        snprintf(dir_buf, sizeof dir_buf, ".config/greed");
    return dir_buf;
}

const char *data_dir(void) {
    static char buf[PATH_MAX];
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0])
        snprintf(buf, sizeof buf, "%s/greed", xdg);
    else if (home && home[0])
        snprintf(buf, sizeof buf, "%s/.local/share/greed", home);
    else
        snprintf(buf, sizeof buf, ".local/share/greed");
    return buf;
}

const char *config_path(void) {
    static char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/config.json", config_dir());
    return path;
}

static const char *schedules_path(void) {
    static char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/schedules.json", config_dir());
    return path;
}

void config_expand_tilde(char *path) {
    if (path[0] != '~') return;
    const char *home = getenv("HOME");
    if (!home || !home[0]) return;
    char out[PATH_MAX];
    snprintf(out, sizeof out, "%s%s", home, path + 1);
    snprintf(path, PATH_MAX, "%s", out);
}

void config_defaults(Config *c) {
    memset(c, 0, sizeof *c);
    snprintf(c->download_dir, sizeof c->download_dir, "~/Downloads");
    c->max_concurrent = 5;
    c->rpc_port = 6800;
    c->max_download_limit = 0;
    c->max_upload_limit = 0;
    c->seed_after_complete = true;
    c->seed_ratio = 1.0f;
    c->auto_start_aria2 = true;
    c->clipboard_monitor = true;
    c->notify_on_complete = true;
    c->verify_after_complete = true;
    c->motion_speed = 1.0f;
    snprintf(c->theme, sizeof c->theme, "Dark Mono");
    snprintf(c->font, sizeof c->font, "default");
    c->sidebar_width = 0;
    c->text_scale = 1.0f;
    c->spacing = 1.0f;
    c->vim_mode = false;
}

static const char *jstr(struct json_object *v, const char *key) {
    struct json_object *o;
    if (json_object_object_get_ex(v, key, &o)) {
        const char *s = json_object_get_string(o);
        if (s) return s;
    }
    return NULL;
}

static bool jbool(struct json_object *v, const char *key, bool fallback) {
    struct json_object *o;
    if (json_object_object_get_ex(v, key, &o)) return json_object_get_boolean(o);
    return fallback;
}

static int jint(struct json_object *v, const char *key, int fallback) {
    struct json_object *o;
    if (json_object_object_get_ex(v, key, &o)) return json_object_get_int(o);
    return fallback;
}

static double jdouble(struct json_object *v, const char *key, double fallback) {
    struct json_object *o;
    if (json_object_object_get_ex(v, key, &o)) return json_object_get_double(o);
    return fallback;
}

void config_load(Config *c) {
    config_defaults(c);
    FILE *f = fopen(config_path(), "r");
    if (!f) {
        config_expand_tilde(c->download_dir);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    if (text && size >= 0) {
        size_t got = fread(text, 1, (size_t)size, f);
        text[got] = 0;
        struct json_object *v = json_tokener_parse(text);
        if (v) {
            const char *dir = jstr(v, "download_dir");
            if (dir) snprintf(c->download_dir, sizeof c->download_dir, "%s", dir);
            c->max_concurrent = jint(v, "max_concurrent", c->max_concurrent);
            c->rpc_port = jint(v, "rpc_port", c->rpc_port);
            c->max_download_limit = jint(v, "max_download_limit", c->max_download_limit);
            c->max_upload_limit = jint(v, "max_upload_limit", c->max_upload_limit);
            c->seed_after_complete = jbool(v, "seed_after_complete", c->seed_after_complete);
            c->seed_ratio = (float)jdouble(v, "seed_ratio", c->seed_ratio);
            c->auto_start_aria2 = jbool(v, "auto_start_aria2", c->auto_start_aria2);
            c->clipboard_monitor = jbool(v, "clipboard_monitor", c->clipboard_monitor);
            c->notify_on_complete = jbool(v, "notify_on_complete", c->notify_on_complete);
            c->verify_after_complete =
                jbool(v, "verify_after_complete", c->verify_after_complete);
            c->motion_speed = (float)jdouble(v, "motion_speed", c->motion_speed);
            if (c->motion_speed < 0.5f) c->motion_speed = 0.5f;
            if (c->motion_speed > 2.0f) c->motion_speed = 2.0f;
            const char *theme = jstr(v, "theme");
            if (theme) snprintf(c->theme, sizeof c->theme, "%s", theme);
            const char *font = jstr(v, "font");
            if (font) snprintf(c->font, sizeof c->font, "%s", font);
            c->sidebar_width = jint(v, "sidebar_width", c->sidebar_width);
            if (c->sidebar_width > 0 && c->sidebar_width < 140) c->sidebar_width = 140;
            if (c->sidebar_width > 420) c->sidebar_width = 420;
            c->text_scale = (float)jdouble(v, "text_scale", c->text_scale);
            if (c->text_scale < 0.85f) c->text_scale = 0.85f;
            if (c->text_scale > 1.30f) c->text_scale = 1.30f;
            c->spacing = (float)jdouble(v, "spacing", c->spacing);
            if (c->spacing < 0.9f) c->spacing = 0.9f;
            if (c->spacing > 1.12f) c->spacing = 1.12f;
            c->vim_mode = jbool(v, "vim_mode", c->vim_mode);
            json_object_put(v);
        }
    }
    free(text);
    fclose(f);
    config_expand_tilde(c->download_dir);
}

bool config_save(const Config *c) {
    mkdir(config_dir(), 0755);
    struct json_object *v = json_object_new_object();
    json_object_object_add(v, "download_dir", json_object_new_string(c->download_dir));
    json_object_object_add(v, "max_concurrent", json_object_new_int(c->max_concurrent));
    json_object_object_add(v, "rpc_port", json_object_new_int(c->rpc_port));
    json_object_object_add(v, "max_download_limit", json_object_new_int(c->max_download_limit));
    json_object_object_add(v, "max_upload_limit", json_object_new_int(c->max_upload_limit));
    json_object_object_add(v, "seed_after_complete", json_object_new_boolean(c->seed_after_complete));
    json_object_object_add(v, "seed_ratio", json_object_new_double(c->seed_ratio));
    json_object_object_add(v, "auto_start_aria2", json_object_new_boolean(c->auto_start_aria2));
    json_object_object_add(v, "clipboard_monitor", json_object_new_boolean(c->clipboard_monitor));
    json_object_object_add(v, "notify_on_complete", json_object_new_boolean(c->notify_on_complete));
    json_object_object_add(v, "verify_after_complete",
                           json_object_new_boolean(c->verify_after_complete));
    json_object_object_add(v, "motion_speed", json_object_new_double(c->motion_speed));
    json_object_object_add(v, "theme", json_object_new_string(c->theme));
    json_object_object_add(v, "font", json_object_new_string(c->font));
    json_object_object_add(v, "sidebar_width", json_object_new_int(c->sidebar_width));
    json_object_object_add(v, "text_scale", json_object_new_double(c->text_scale));
    json_object_object_add(v, "spacing", json_object_new_double(c->spacing));
    json_object_object_add(v, "vim_mode", json_object_new_boolean(c->vim_mode));
    const char *text = json_object_to_json_string_ext(v, JSON_C_TO_STRING_PRETTY);
    FILE *f = fopen(config_path(), "w");
    bool ok = false;
    if (f) {
        ok = fputs(text, f) >= 0;
        fputc('\n', f);
        fclose(f);
    }
    json_object_put(v);
    return ok;
}

int config_schedules_load(GreedScheduled *out, int cap) {
    FILE *f = fopen(schedules_path(), "r");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return 0;
    }
    char *text = malloc((size_t)size + 1);
    int n = 0;
    if (text) {
        size_t got = fread(text, 1, (size_t)size, f);
        text[got] = 0;
        struct json_object *v = json_tokener_parse(text);
        if (v && json_object_is_type(v, json_type_array)) {
            int len = (int)json_object_array_length(v);
            for (int i = 0; i < len && n < cap; i++) {
                struct json_object *o = json_object_array_get_idx(v, i);
                const char *url = jstr(o, "url");
                struct json_object *due_obj = NULL;
                long long due = 0;
                if (json_object_object_get_ex(o, "due", &due_obj))
                    due = (long long)json_object_get_int64(due_obj);
                if (!url || !url[0] || due <= 0) continue;
                snprintf(out[n].url, sizeof out[n].url, "%s", url);
                const char *dir = jstr(o, "dir");
                snprintf(out[n].dir, sizeof out[n].dir, "%s", dir ? dir : "");
                config_expand_tilde(out[n].dir);
                out[n].due = due;
                n++;
            }
        }
        if (v) json_object_put(v);
        free(text);
    }
    fclose(f);
    return n;
}

bool config_schedules_save(const GreedScheduled *items, int count) {
    mkdir(config_dir(), 0755);
    struct json_object *arr = json_object_new_array();
    for (int i = 0; i < count; i++) {
        struct json_object *o = json_object_new_object();
        json_object_object_add(o, "url", json_object_new_string(items[i].url));
        json_object_object_add(o, "dir", json_object_new_string(items[i].dir));
        json_object_object_add(o, "due", json_object_new_int64(items[i].due));
        json_object_array_add(arr, o);
    }
    const char *text = json_object_to_json_string_ext(arr, JSON_C_TO_STRING_PRETTY);
    FILE *f = fopen(schedules_path(), "w");
    bool ok = false;
    if (f) {
        ok = fputs(text, f) >= 0;
        fputc('\n', f);
        fclose(f);
    }
    json_object_put(arr);
    return ok;
}
