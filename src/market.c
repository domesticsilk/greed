#include "market.h"

#include "catalog_data.h"
#include "checksum.h"
#include "config.h"
#include "theme.h"
#include "version.h"

#include <curl/curl.h>
#include <json-c/json.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define CATALOG_URL "https://raw.githubusercontent.com/domesticsilk/greed/main/marketplace/catalog.json"
#define CATALOG_TTL (6 * 60 * 60)

typedef struct {
    char *data;
    size_t len, cap;
} Buf;

static pthread_once_t curl_once = PTHREAD_ONCE_INIT;
static void curl_setup(void) { curl_global_init(CURL_GLOBAL_DEFAULT); }

static const char *catalog_url(void) {
    const char *env = getenv("GREED_MARKET_API");
    if (env && *env) return env;
    return CATALOG_URL;
}

static const char *market_dir(void) {
    static char path[PATH_MAX * 2];
    snprintf(path, sizeof path, "%s/marketplace", data_dir());
    return path;
}

static const char *cache_path(void) {
    static char path[PATH_MAX * 2 + 32];
    snprintf(path, sizeof path, "%s/catalog.json", market_dir());
    return path;
}

static const char *installed_path(void) {
    static char path[PATH_MAX * 2 + 32];
    snprintf(path, sizeof path, "%s/installed.json", market_dir());
    return path;
}

static size_t buf_write(char *p, size_t size, size_t n, void *ud) {
    Buf *b = ud;
    size_t add = size * n;
    if (b->len + add > 2 * 1024 * 1024) return 0;
    if (b->len + add + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 8192;
        while (cap < b->len + add + 1) cap *= 2;
        char *next = realloc(b->data, cap);
        if (!next) return 0;
        b->data = next;
        b->cap = cap;
    }
    memcpy(b->data + b->len, p, add);
    b->len += add;
    b->data[b->len] = 0;
    return add;
}

static const char *jstr(struct json_object *v, const char *key) {
    struct json_object *o = NULL;
    if (!v || !json_object_object_get_ex(v, key, &o)) return NULL;
    return json_object_get_string(o);
}

static long long jnum(struct json_object *v, const char *key, long long fallback) {
    struct json_object *o = NULL;
    if (!v || !json_object_object_get_ex(v, key, &o)) return fallback;
    const char *s = json_object_get_string(o);
    return s && *s ? strtoll(s, NULL, 10) : fallback;
}

static unsigned hex_color(const char *s, unsigned fallback) {
    if (!s || !*s) return fallback;
    if (*s == '#') s++;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    return end != s && v <= 0xffffff ? (unsigned)v : fallback;
}

static bool sha_valid(const char *s) {
    if (!s || strlen(s) != 64) return false;
    for (int i = 0; i < 64; i++)
        if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

/* Wallpapers land next to the theme JSON, so the name must stay in that folder. */
static bool asset_name_valid(const char *s) {
    if (!s || !s[0] || strlen(s) >= 128) return false;
    if (strchr(s, '/') || strchr(s, '\\')) return false;
    return strcmp(s, ".") && strcmp(s, "..");
}

bool market_thumb_path(const char *id, char *out, size_t cap) {
    if (!asset_name_valid(id)) return false;
    snprintf(out, cap, "%s/thumbs/%s.jpg", market_dir(), id);
    return access(out, F_OK) == 0;
}

static void mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    size_t n = strlen(path);
    if (!n || n >= sizeof tmp) return;
    memcpy(tmp, path, n + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(tmp, 0755);
        *p = '/';
    }
    mkdir(tmp, 0755);
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    char *data = malloc((size_t)size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);
    data[got] = 0;
    if (len) *len = got;
    return data;
}

static bool write_file_atomic(const char *path, const char *data, size_t len) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    else snprintf(dir, sizeof dir, ".");
    mkdir_p(dir);

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s.tmp-XXXXXX", path);
    int fd = mkstemp(tmp);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp);
            return false;
        }
        off += (size_t)n;
    }
    close(fd);
    chmod(tmp, 0644);
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return false;
    }
    return true;
}

static void item_target(const MarketItem *it, char *out, size_t cap) {
    if (it->kind == MKT_THEME)
        snprintf(out, cap, "%s/themes/%s.json", data_dir(), it->id);
    else
        snprintf(out, cap, "%s/tools/%s/%s", data_dir(), it->id,
                 it->exec[0] ? it->exec : it->id);
}

static void installed_load(Market *m) {
    m->installed_count = 0;
    size_t len = 0;
    char *text = read_file(installed_path(), &len);
    if (!text) return;
    struct json_object *root = json_tokener_parse(text);
    free(text);
    if (!root) return;
    struct json_object *items = NULL;
    if (json_object_object_get_ex(root, "items", &items) &&
        json_object_is_type(items, json_type_array)) {
        int n = (int)json_object_array_length(items);
        for (int i = 0; i < n && m->installed_count < MKT_MAX_INSTALLED; i++) {
            struct json_object *e = json_object_array_get_idx(items, i);
            const char *id = jstr(e, "id");
            const char *path = jstr(e, "path");
            if (!id || !id[0] || !path || !path[0]) continue;
            if (access(path, F_OK) != 0) continue;
            MarketInstalled *r = &m->installed[m->installed_count++];
            snprintf(r->id, sizeof r->id, "%s", id);
            snprintf(r->version, sizeof r->version, "%s", jstr(e, "version") ?: "");
            snprintf(r->path, sizeof r->path, "%s", path);
            const char *extra = jstr(e, "extra");
            if (extra) snprintf(r->extra, sizeof r->extra, "%s", extra);
        }
    }
    json_object_put(root);
}

static void installed_save(Market *m) {
    struct json_object *root = json_object_new_object();
    struct json_object *items = json_object_new_array();
    for (int i = 0; i < m->installed_count; i++) {
        struct json_object *e = json_object_new_object();
        json_object_object_add(e, "id", json_object_new_string(m->installed[i].id));
        json_object_object_add(e, "version", json_object_new_string(m->installed[i].version));
        json_object_object_add(e, "path", json_object_new_string(m->installed[i].path));
        if (m->installed[i].extra[0])
            json_object_object_add(e, "extra", json_object_new_string(m->installed[i].extra));
        json_object_array_add(items, e);
    }
    json_object_object_add(root, "items", items);
    const char *text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    write_file_atomic(installed_path(), text, strlen(text));
    json_object_put(root);
}

static bool catalog_parse(const char *text, MarketItem **out, int *count) {
    struct json_object *root = json_tokener_parse(text ? text : "");
    if (!root) return false;
    struct json_object *items = NULL;
    if (!json_object_object_get_ex(root, "items", &items) ||
        !json_object_is_type(items, json_type_array)) {
        json_object_put(root);
        return false;
    }

    MarketItem *list = calloc(MKT_MAX_ITEMS, sizeof *list);
    if (!list) {
        json_object_put(root);
        return false;
    }
    int n = 0;
    int total = (int)json_object_array_length(items);
    for (int i = 0; i < total && n < MKT_MAX_ITEMS; i++) {
        struct json_object *e = json_object_array_get_idx(items, i);
        struct json_object *asset = NULL;
        const char *id = jstr(e, "id");
        const char *type = jstr(e, "type");
        const char *name = jstr(e, "name");
        if (!id || !id[0] || !asset_name_valid(id) || !type || !name || !name[0] ||
            !json_object_object_get_ex(e, "asset", &asset))
            continue;
        const char *url = jstr(asset, "url");
        const char *sha = jstr(asset, "sha256");
        if (!url || !url[0] || !sha_valid(sha)) continue;

        MarketItem *it = &list[n];
        snprintf(it->id, sizeof it->id, "%s", id);
        snprintf(it->name, sizeof it->name, "%s", name);
        const char *summary = jstr(e, "summary");
        const char *description = jstr(e, "description");
        const char *category = jstr(e, "category");
        const char *version = jstr(e, "version");
        const char *source = jstr(e, "source");
        const char *exec = jstr(asset, "exec");
        if (summary) snprintf(it->summary, sizeof it->summary, "%s", summary);
        if (description) snprintf(it->description, sizeof it->description, "%s", description);
        if (category) snprintf(it->category, sizeof it->category, "%s", category);
        if (version) snprintf(it->version, sizeof it->version, "%s", version);
        if (source) snprintf(it->source, sizeof it->source, "%s", source);
        if (exec) snprintf(it->exec, sizeof it->exec, "%s", exec);
        snprintf(it->url, sizeof it->url, "%s", url);
        snprintf(it->sha256, sizeof it->sha256, "%s", sha);
        it->size = jnum(asset, "size", jnum(e, "size", 0));

        if (!strcmp(type, "theme")) it->kind = MKT_THEME;
        else if (!strcmp(type, "tool")) it->kind = MKT_TOOL;
        else it->kind = MKT_ENGINE;

        struct json_object *req = NULL;
        if (json_object_object_get_ex(e, "requires", &req) &&
            json_object_is_type(req, json_type_array)) {
            size_t used = 0;
            int rn = (int)json_object_array_length(req);
            for (int k = 0; k < rn; k++) {
                const char *r = json_object_get_string(json_object_array_get_idx(req, k));
                if (!r || !r[0]) continue;
                int wrote = snprintf(it->requires + used, sizeof it->requires - used, "%s%s",
                                     used ? "," : "", r);
                if (wrote < 0 || (size_t)wrote >= sizeof it->requires - used) break;
                used += (size_t)wrote;
            }
        }

        struct json_object *preview = NULL;
        if (json_object_object_get_ex(e, "preview", &preview)) {
            it->preview_bg = hex_color(jstr(preview, "bg"), 0);
            it->preview_surface = hex_color(jstr(preview, "surface"), 0);
            it->preview_accent = hex_color(jstr(preview, "accent"), 0);
            it->preview_text = hex_color(jstr(preview, "text"), 0);
        }
        struct json_object *wallpaper = NULL;
        if (json_object_object_get_ex(e, "wallpaper", &wallpaper)) {
            const char *wurl = jstr(wallpaper, "url");
            const char *wsha = jstr(wallpaper, "sha256");
            const char *wname = jstr(wallpaper, "name");
            if (wurl && wurl[0] && sha_valid(wsha) && asset_name_valid(wname)) {
                snprintf(it->wallpaper_url, sizeof it->wallpaper_url, "%s", wurl);
                snprintf(it->wallpaper_sha256, sizeof it->wallpaper_sha256, "%s", wsha);
                snprintf(it->wallpaper_name, sizeof it->wallpaper_name, "%s", wname);
            }
        }
        struct json_object *thumb = NULL;
        if (json_object_object_get_ex(e, "thumb", &thumb)) {
            const char *turl = jstr(thumb, "url");
            const char *tsha = jstr(thumb, "sha256");
            long long tsize = jnum(thumb, "size", 0);
            if (turl && turl[0] && sha_valid(tsha) && tsize > 0 && tsize <= 1024 * 1024) {
                snprintf(it->thumb_url, sizeof it->thumb_url, "%s", turl);
                snprintf(it->thumb_sha256, sizeof it->thumb_sha256, "%s", tsha);
                it->thumb_size = tsize;
            }
        }
        n++;
    }
    json_object_put(root);
    if (!n) {
        free(list);
        return false;
    }
    *out = list;
    *count = n;
    return true;
}

static void set_catalog(Market *m, MarketItem *items, int count, const char *note) {
    pthread_mutex_lock(&m->lock);
    memcpy(m->items, items, (size_t)count * sizeof *items);
    m->count = count;
    snprintf(m->note, sizeof m->note, "%s", note ? note : "");
    m->stage = MKT_READY;
    m->error[0] = 0;
    pthread_mutex_unlock(&m->lock);
}

static const char *net_error(CURLcode rc) {
    switch (rc) {
    case CURLE_OPERATION_TIMEDOUT: return "The marketplace took too long to answer.";
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT: return "Could not reach the marketplace. Check your connection.";
    case CURLE_WRITE_ERROR: return "The marketplace reply was too large.";
    default: return "Could not reach the marketplace. Check your connection.";
    }
}

static bool http_get(const char *url, Buf *b, long *code, char *err, size_t errcap) {
    pthread_once(&curl_once, curl_setup);
    CURL *c = curl_easy_init();
    if (!c) {
        snprintf(err, errcap, "The marketplace could not start.");
        return false;
    }
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "greed/" GREED_VERSION);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, buf_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, b);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        snprintf(err, errcap, "%s", net_error(rc));
        return false;
    }
    if (*code == 404) {
        snprintf(err, errcap, "The marketplace catalog is not published yet.");
        return false;
    }
    if (*code != 200) {
        snprintf(err, errcap, "The marketplace returned HTTP %ld.", *code);
        return false;
    }
    return true;
}

/* Small previews are optional: any failure leaves the palette tile in place. */
static void cache_thumb(const MarketItem *it) {
    if (!it->thumb_url[0] || !sha_valid(it->thumb_sha256) || it->thumb_size <= 0) return;
    char path[PATH_MAX];
    market_thumb_path(it->id, path, sizeof path);
    char hex[65];
    if (access(path, F_OK) == 0) {
        if (checksum_hash_file(path, hex, sizeof hex) &&
            !strcasecmp(hex, it->thumb_sha256))
            return;
        unlink(path);
    }
    Buf b = {0};
    long code = 0;
    char err[256];
    if (!http_get(it->thumb_url, &b, &code, err, sizeof err) ||
        (long long)b.len != it->thumb_size) {
        free(b.data);
        return;
    }
    if (write_file_atomic(path, b.data, b.len) &&
        (!checksum_hash_file(path, hex, sizeof hex) || strcasecmp(hex, it->thumb_sha256)))
        unlink(path);
    free(b.data);
}

static void *fetch_worker(void *arg) {
    Market *m = arg;
    Buf b = {0};
    long code = 0;
    char err[512] = {0};
    bool net_ok = http_get(catalog_url(), &b, &code, err, sizeof err);

    if (net_ok && b.data) {
        MarketItem *items = NULL;
        int count = 0;
        if (catalog_parse(b.data, &items, &count)) {
            write_file_atomic(cache_path(), b.data, b.len);
            for (int i = 0; i < count; i++) cache_thumb(&items[i]);
            set_catalog(m, items, count, "");
            pthread_mutex_lock(&m->lock);
            m->fetched_at = (long long)time(NULL);
            pthread_mutex_unlock(&m->lock);
            free(items);
            free(b.data);
            return NULL;
        }
    }
    free(b.data);

    size_t len = 0;
    char *cached = read_file(cache_path(), &len);
    if (cached) {
        MarketItem *items = NULL;
        int count = 0;
        if (catalog_parse(cached, &items, &count)) {
            set_catalog(m, items, count,
                        "Showing the saved catalog. Refresh when you are online.");
            pthread_mutex_lock(&m->lock);
            m->fetched_at = (long long)time(NULL);
            pthread_mutex_unlock(&m->lock);
            free(items);
            free(cached);
            return NULL;
        }
        free(cached);
    }

    MarketItem *items = NULL;
    int count = 0;
    if (catalog_parse(fallback_catalog, &items, &count)) {
        set_catalog(m, items, count,
                    "Showing the built-in catalog. Refresh when you are online.");
        pthread_mutex_lock(&m->lock);
        m->fetched_at = (long long)time(NULL);
        pthread_mutex_unlock(&m->lock);
        free(items);
        return NULL;
    }

    pthread_mutex_lock(&m->lock);
    m->stage = MKT_ERROR;
    snprintf(m->error, sizeof m->error, "%s", err[0] ? err : "The catalog could not be read.");
    pthread_mutex_unlock(&m->lock);
    return NULL;
}

void market_init(Market *m) {
    memset(m, 0, sizeof *m);
    pthread_mutex_init(&m->lock, NULL);
    m->stage = MKT_IDLE;
    m->job_item = -1;
    installed_load(m);
}

void market_fetch(Market *m, bool force) {
    pthread_mutex_lock(&m->lock);
    bool busy = m->stage == MKT_LOADING;
    bool fresh = !force && m->fetched_at > 0 &&
                 (long long)time(NULL) - m->fetched_at < CATALOG_TTL;
    if (!busy && !fresh) {
        m->stage = MKT_LOADING;
        m->error[0] = 0;
        m->note[0] = 0;
    }
    pthread_mutex_unlock(&m->lock);
    if (busy || fresh) return;

    pthread_t t;
    if (pthread_create(&t, NULL, fetch_worker, m) != 0) {
        pthread_mutex_lock(&m->lock);
        m->stage = MKT_ERROR;
        snprintf(m->error, sizeof m->error, "Could not start the marketplace task.");
        pthread_mutex_unlock(&m->lock);
        return;
    }
    pthread_detach(t);
}

void market_snapshot(Market *m, MarketView *v) {
    pthread_mutex_lock(&m->lock);
    v->stage = m->stage;
    snprintf(v->error, sizeof v->error, "%s", m->error);
    snprintf(v->note, sizeof v->note, "%s", m->note);
    v->count = m->count;
    memcpy(v->items, m->items, (size_t)m->count * sizeof m->items[0]);
    v->installed_count = m->installed_count;
    memcpy(v->installed, m->installed, (size_t)m->installed_count * sizeof m->installed[0]);
    v->job_item = m->job_item;
    v->job_stage = m->job_stage;
    v->progress = m->progress;
    snprintf(v->job_error, sizeof v->job_error, "%s", m->job_error);
    snprintf(v->status, sizeof v->status, "%s", m->status);
    pthread_mutex_unlock(&m->lock);
}

int market_installed_version(const MarketView *v, const char *id, char *out, size_t cap) {
    for (int i = 0; i < v->installed_count; i++) {
        if (strcmp(v->installed[i].id, id)) continue;
        if (out && cap) snprintf(out, cap, "%s", v->installed[i].version);
        return 1;
    }
    return 0;
}

bool market_managed(Market *m, const char *id) {
    bool found = false;
    pthread_mutex_lock(&m->lock);
    for (int i = 0; i < m->installed_count; i++)
        if (!strcmp(m->installed[i].id, id)) {
            found = true;
            break;
        }
    pthread_mutex_unlock(&m->lock);
    return found;
}

const char *market_tool_path(const char *id, const char *exec) {
    static __thread char out[PATH_MAX];
    if (!id || !id[0]) return NULL;
    if (!exec || !exec[0]) exec = id;
    snprintf(out, sizeof out, "%s/tools/%s/%s", data_dir(), id, exec);
    if (access(out, X_OK) == 0) return out;

    const char *path = getenv("PATH");
    if (!path) return NULL;
    const char *p = path;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t n = colon ? (size_t)(colon - p) : strlen(p);
        if (n && n + strlen(exec) + 2 < sizeof out) {
            snprintf(out, sizeof out, "%.*s/%s", (int)n, p, exec);
            if (access(out, X_OK) == 0) return out;
        }
        if (!colon) break;
        p = colon + 1;
    }
    return NULL;
}

bool market_tool_available(const char *id) { return market_tool_path(id, id) != NULL; }

static int progress_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                       curl_off_t ulnow) {
    Market *m = ud;
    (void)ultotal;
    (void)ulnow;
    if (dltotal <= 0) return 0;
    float v = (float)((double)dlnow / (double)dltotal);
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    pthread_mutex_lock(&m->lock);
    m->progress = v;
    pthread_mutex_unlock(&m->lock);
    return 0;
}

static void job_error(Market *m, const char *msg) {
    pthread_mutex_lock(&m->lock);
    m->job_stage = MKT_JOB_ERROR;
    snprintf(m->job_error, sizeof m->job_error, "%s", msg);
    pthread_mutex_unlock(&m->lock);
}

static void job_installed(Market *m, const MarketItem *it, const char *target,
                          const char *extra) {
    pthread_mutex_lock(&m->lock);
    int slot = -1;
    for (int i = 0; i < m->installed_count; i++)
        if (!strcmp(m->installed[i].id, it->id)) {
            slot = i;
            break;
        }
    if (slot < 0 && m->installed_count < MKT_MAX_INSTALLED)
        slot = m->installed_count++;
    if (slot >= 0) {
        snprintf(m->installed[slot].id, sizeof m->installed[slot].id, "%s", it->id);
        snprintf(m->installed[slot].version, sizeof m->installed[slot].version, "%s",
                 it->version);
        snprintf(m->installed[slot].path, sizeof m->installed[slot].path, "%s", target);
        snprintf(m->installed[slot].extra, sizeof m->installed[slot].extra, "%s",
                 extra ? extra : "");
        installed_save(m);
    }
    m->job_stage = MKT_JOB_INSTALLED;
    m->progress = 1;
    snprintf(m->status, sizeof m->status, "Installed %s", it->name);
    pthread_mutex_unlock(&m->lock);
}

/* Download to a temporary file and check the published hash. */
static bool fetch_verified(Market *m, const char *url, const char *sha, char *tmp,
                           const char *what, char *err, size_t errcap) {
    int fd = mkstemp(tmp);
    if (fd < 0) {
        snprintf(err, errcap, "Could not write the %s. Check your disk space.", what);
        return false;
    }
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        unlink(tmp);
        snprintf(err, errcap, "Could not write the %s. Check your disk space.", what);
        return false;
    }

    pthread_once(&curl_once, curl_setup);
    CURL *c = curl_easy_init();
    CURLcode rc = CURLE_FAILED_INIT;
    long code = 0;
    if (c) {
        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "greed/" GREED_VERSION);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, m);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 1800L);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        rc = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(c);
    }
    if (fflush(f) != 0 || ferror(f)) rc = CURLE_WRITE_ERROR;
    fclose(f);

    if (rc != CURLE_OK) {
        unlink(tmp);
        if (rc == CURLE_HTTP_RETURNED_ERROR)
            snprintf(err, errcap, "The %s download was refused by the server.", what);
        else if (rc == CURLE_OPERATION_TIMEDOUT || rc == CURLE_COULDNT_CONNECT)
            snprintf(err, errcap, "%s", net_error(rc));
        else
            snprintf(err, errcap, "The %s download failed. Try again.", what);
        return false;
    }

    char hex[65];
    if (!checksum_hash_file(tmp, hex, sizeof hex)) {
        unlink(tmp);
        snprintf(err, errcap, "Could not verify the %s download.", what);
        return false;
    }
    if (strcasecmp(hex, sha)) {
        unlink(tmp);
        snprintf(err, errcap, "The %s download did not match its checksum.", what);
        return false;
    }
    return true;
}

static void *install_worker(void *arg) {
    Market *m = arg;
    MarketItem it;
    pthread_mutex_lock(&m->lock);
    it = m->items[m->job_item];
    pthread_mutex_unlock(&m->lock);

    char target[PATH_MAX], dir[PATH_MAX], tmp[PATH_MAX];
    item_target(&it, target, sizeof target);
    snprintf(dir, sizeof dir, "%s", target);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    else snprintf(dir, sizeof dir, ".");
    mkdir_p(dir);
    snprintf(tmp, sizeof tmp, "%s/.greed-market-XXXXXX", dir);

    char err[512];
    if (!fetch_verified(m, it.url, it.sha256, tmp,
                        it.kind == MKT_THEME ? "theme" : "tool", err, sizeof err)) {
        job_error(m, err);
        return NULL;
    }

    char wp_tmp[PATH_MAX] = "", wp_target[PATH_MAX] = "";
    if (it.kind == MKT_THEME && it.wallpaper_url[0]) {
        snprintf(wp_target, sizeof wp_target, "%s/%s", dir, it.wallpaper_name);
        snprintf(wp_tmp, sizeof wp_tmp, "%s/.greed-wallpaper-XXXXXX", dir);
        if (!fetch_verified(m, it.wallpaper_url, it.wallpaper_sha256, wp_tmp, "wallpaper",
                            err, sizeof err)) {
            unlink(tmp);
            job_error(m, err);
            return NULL;
        }
    }

    if (it.kind == MKT_THEME) {
        if (rename(tmp, target) != 0) {
            unlink(tmp);
            if (wp_tmp[0]) unlink(wp_tmp);
            job_error(m, "Could not install the theme.");
            return NULL;
        }
        if (wp_tmp[0] && rename(wp_tmp, wp_target) != 0) {
            unlink(wp_tmp);
            unlink(target);
            job_error(m, "Could not install the wallpaper.");
            return NULL;
        }
        greed_theme_reload();
        if (!greed_theme_exists(it.name)) {
            unlink(target);
            if (wp_target[0]) unlink(wp_target);
            greed_theme_reload();
            job_error(m, "The theme file is not valid.");
            return NULL;
        }
    } else {
        if (chmod(tmp, 0755) != 0 || rename(tmp, target) != 0) {
            unlink(tmp);
            job_error(m, "Could not install the tool.");
            return NULL;
        }
    }
    job_installed(m, &it, target, wp_target[0] ? wp_target : NULL);
    return NULL;
}

void market_install(Market *m, int index) {
    pthread_mutex_lock(&m->lock);
    bool busy = m->job_stage == MKT_JOB_DOWNLOADING;
    if (busy || index < 0 || index >= m->count || m->stage != MKT_READY) {
        pthread_mutex_unlock(&m->lock);
        return;
    }
    m->job_item = index;
    m->job_stage = MKT_JOB_DOWNLOADING;
    m->progress = 0;
    m->job_error[0] = 0;
    m->status[0] = 0;
    pthread_mutex_unlock(&m->lock);

    pthread_t t;
    if (pthread_create(&t, NULL, install_worker, m) != 0) {
        job_error(m, "Could not start the install task.");
        return;
    }
    pthread_detach(t);
}

bool market_remove(Market *m, int index) {
    pthread_mutex_lock(&m->lock);
    if (index < 0 || index >= m->count || m->job_stage == MKT_JOB_DOWNLOADING) {
        pthread_mutex_unlock(&m->lock);
        return false;
    }
    MarketItem it = m->items[index];
    int slot = -1;
    for (int i = 0; i < m->installed_count; i++)
        if (!strcmp(m->installed[i].id, it.id)) {
            slot = i;
            break;
        }
    if (slot < 0) {
        pthread_mutex_unlock(&m->lock);
        return false;
    }
    char path[PATH_MAX], extra[PATH_MAX];
    snprintf(path, sizeof path, "%s", m->installed[slot].path);
    snprintf(extra, sizeof extra, "%s", m->installed[slot].extra);
    memmove(&m->installed[slot], &m->installed[slot + 1],
            (size_t)(m->installed_count - slot - 1) * sizeof m->installed[0]);
    m->installed_count--;
    if (m->job_item == index) {
        m->job_item = -1;
        m->job_stage = MKT_JOB_NONE;
        m->job_error[0] = 0;
    }
    installed_save(m);
    m->job_stage = MKT_JOB_NONE;
    m->job_item = -1;
    m->progress = 0;
    snprintf(m->status, sizeof m->status, "Removed %s", it.name);
    pthread_mutex_unlock(&m->lock);

    unlink(path);
    if (extra[0]) unlink(extra);
    if (it.kind != MKT_THEME) {
        char dir[PATH_MAX];
        snprintf(dir, sizeof dir, "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = 0;
            rmdir(dir);
        }
    } else {
        greed_theme_reload();
    }
    return true;
}
