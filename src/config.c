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

const char *config_path(void) {
    static char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/config.json", config_dir());
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
