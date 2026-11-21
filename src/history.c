#include "history.h"

#include "config.h"

#include <json-c/json.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define HISTORY_MAX 2000

typedef struct {
    char key[HISTORY_KEY_MAX];
    long long added;
} HistoryEntry;

static HistoryEntry *entries;
static int entry_count, entry_cap;
static bool loaded, dirty;

static const char *history_path(void) {
    static char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/history.json", config_dir());
    return path;
}

static HistoryEntry *entry_find(const char *key) {
    for (int i = 0; i < entry_count; i++)
        if (!strcmp(entries[i].key, key)) return &entries[i];
    return NULL;
}

static bool entry_push(const char *key, long long added) {
    if (entry_count == entry_cap) {
        int cap = entry_cap ? entry_cap * 2 : 64;
        if (cap > HISTORY_MAX) cap = HISTORY_MAX;
        if (cap == entry_cap) return false;
        HistoryEntry *p = realloc(entries, (size_t)cap * sizeof *entries);
        if (!p) return false;
        entries = p;
        entry_cap = cap;
    }
    HistoryEntry *e = &entries[entry_count++];
    snprintf(e->key, sizeof e->key, "%s", key);
    e->added = added;
    return true;
}

/* A full map drops the oldest timestamp; the download is long gone anyway. */
static void entry_evict(void) {
    int oldest = 0;
    for (int i = 1; i < entry_count; i++)
        if (entries[i].added < entries[oldest].added) oldest = i;
    entries[oldest] = entries[entry_count - 1];
    entry_count--;
}

void history_load(void) {
    loaded = true;
    dirty = false;
    entry_count = 0;
    FILE *f = fopen(history_path(), "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 4 * 1024 * 1024) {
        fclose(f);
        return;
    }
    char *text = malloc((size_t)size + 1);
    if (!text) {
        fclose(f);
        return;
    }
    size_t got = fread(text, 1, (size_t)size, f);
    text[got] = 0;
    fclose(f);
    struct json_object *v = json_tokener_parse(text);
    free(text);
    if (!v) return;
    json_object_object_foreach(v, key, val) {
        long long added = json_object_get_int64(val);
        if (added > 0) entry_push(key, added);
    }
    json_object_put(v);
}

long long history_added(const char *key, long long now) {
    if (!loaded) history_load();
    HistoryEntry *e = entry_find(key);
    if (e) return e->added;
    if (entry_count >= HISTORY_MAX) entry_evict();
    if (!entry_push(key, now)) return now;
    dirty = true;
    return now;
}

void history_forget(const char *key) {
    if (!loaded) history_load();
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].key, key)) continue;
        entries[i] = entries[entry_count - 1];
        entry_count--;
        dirty = true;
        return;
    }
}

void history_flush(void) {
    if (!loaded || !dirty) return;
    mkdir(config_dir(), 0755);
    struct json_object *v = json_object_new_object();
    if (!v) return;
    for (int i = 0; i < entry_count; i++)
        json_object_object_add(v, entries[i].key, json_object_new_int64(entries[i].added));
    const char *text = json_object_to_json_string_ext(v, JSON_C_TO_STRING_PRETTY);
    bool ok = file_write_atomic(history_path(), text);
    json_object_put(v);
    if (ok) dirty = false;
}
