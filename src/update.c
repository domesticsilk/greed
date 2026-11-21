#include "update.h"

#include "checksum.h"
#include "version.h"

#include <curl/curl.h>
#include <json-c/json.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The latest published AppImage, not a per-version URL, so the link is stable. */
#define ASSET_NAME "Greed-x86_64.AppImage"

typedef struct {
    char *data;
    size_t len, cap;
} Buf;

static const char *update_api(void) {
    /* Test hook: point the check at a local server or a mirror. */
    const char *env = getenv("GREED_UPDATE_API");
    if (env && *env) return env;
    return "https://api.github.com/repos/domesticsilk/greed/releases/latest";
}

static size_t buf_write(char *p, size_t size, size_t n, void *ud) {
    Buf *b = ud;
    size_t add = size * n;
    if (b->len + add > 1024 * 1024) return 0;
    if (b->len + add + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
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

static int version_cmp(const char *a, const char *b) {
    char *pa = (char *)a, *pb = (char *)b;
    for (int i = 0; i < 3; i++) {
        while (*pa && !isdigit((unsigned char)*pa)) pa++;
        while (*pb && !isdigit((unsigned char)*pb)) pb++;
        long x = *pa ? strtol(pa, &pa, 10) : 0;
        long y = *pb ? strtol(pb, &pb, 10) : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

static const char *net_error(CURLcode rc) {
    switch (rc) {
    case CURLE_OPERATION_TIMEDOUT: return "GitHub took too long to answer.";
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT: return "Could not reach GitHub. Check your connection.";
    case CURLE_WRITE_ERROR: return "The reply from GitHub was too large.";
    case CURLE_FAILED_INIT: return "The download could not start.";
    default: return "The update check failed. Check your connection.";
    }
}

static void *check_worker(void *arg) {
    GreedUpdate *u = arg;
    Buf b = {0};
    CURLcode rc = CURLE_FAILED_INIT;
    long code = 0;

    CURL *c = curl_easy_init();
    if (c) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
        curl_easy_setopt(c, CURLOPT_URL, update_api());
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "greed/" GREED_VERSION);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, buf_write);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        rc = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(c);
    }

    int stage = UPD_ERROR;
    char error[512] = {0}, version[64] = {0}, url[2048] = {0}, sha_url[2048] = {0};
    char sha256[65] = {0};

    if (rc != CURLE_OK) {
        snprintf(error, sizeof error, "%s", net_error(rc));
    } else if (code == 404) {
        snprintf(error, sizeof error, "No releases have been published yet.");
    } else if (code != 200) {
        snprintf(error, sizeof error, "GitHub returned HTTP %ld.", code);
    } else {
        struct json_object *root = json_tokener_parse(b.data ? b.data : "");
        struct json_object *tag = NULL, *assets = NULL;
        if (!root || !json_object_object_get_ex(root, "tag_name", &tag)) {
            snprintf(error, sizeof error, "GitHub sent an unexpected reply.");
        } else {
            json_object_object_get_ex(root, "assets", &assets);
            const char *raw = json_object_get_string(tag);
            if (raw) snprintf(version, sizeof version, "%s", raw);
            if (version[0] == 'v' || version[0] == 'V') memmove(version, version + 1,
                                                               strlen(version));
            if (!version[0]) {
                snprintf(error, sizeof error, "GitHub sent an unexpected reply.");
            } else if (version_cmp(version, GREED_VERSION) <= 0) {
                stage = UPD_CURRENT;
            } else {
                int n = assets ? json_object_array_length(assets) : 0;
                for (int i = 0; i < n; i++) {
                    struct json_object *asset = json_object_array_get_idx(assets, i);
                    struct json_object *name = NULL, *link = NULL;
                    if (!json_object_object_get_ex(asset, "name", &name)) continue;
                    const char *aname = json_object_get_string(name);
                    if (!aname || !json_object_object_get_ex(asset, "browser_download_url", &link))
                        continue;
                    if (!strcmp(aname, ASSET_NAME)) {
                        snprintf(url, sizeof url, "%s", json_object_get_string(link));
                        struct json_object *digest = NULL;
                        if (json_object_object_get_ex(asset, "digest", &digest)) {
                            const char *hex = json_object_get_string(digest);
                            if (hex) checksum_parse_hex(hex, sha256, sizeof sha256);
                        }
                    } else if (!strcmp(aname, ASSET_NAME ".sha256"))
                        snprintf(sha_url, sizeof sha_url, "%s", json_object_get_string(link));
                }
                if (!url[0]) snprintf(error, sizeof error, "The latest release has no AppImage.");
                else stage = UPD_AVAILABLE;
            }
        }
        if (root) json_object_put(root);
    }
    free(b.data);

    pthread_mutex_lock(&u->lock);
    u->stage = stage;
    u->progress = 0;
    snprintf(u->version, sizeof u->version, "%s", version);
    snprintf(u->url, sizeof u->url, "%s", url);
    snprintf(u->sha_url, sizeof u->sha_url, "%s", sha_url);
    snprintf(u->sha256, sizeof u->sha256, "%s", sha256);
    snprintf(u->error, sizeof u->error, "%s", error);
    pthread_mutex_unlock(&u->lock);
    return NULL;
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

static int progress_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                       curl_off_t ulnow) {
    GreedUpdate *u = ud;
    (void)ultotal;
    (void)ulnow;
    if (dltotal <= 0) return 0;
    float v = (float)((double)dlnow / (double)dltotal);
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    pthread_mutex_lock(&u->lock);
    u->progress = v;
    pthread_mutex_unlock(&u->lock);
    return 0;
}

static bool fetch_text(const char *url, char *out, size_t cap) {
    Buf b = {0};
    CURL *c = curl_easy_init();
    if (!c) return false;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "greed/" GREED_VERSION);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, buf_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    bool ok = rc == CURLE_OK && b.data && b.data[0];
    if (ok) snprintf(out, cap, "%s", b.data);
    free(b.data);
    return ok;
}

/* The .sha256 asset catches a truncated or corrupted transfer. It is not a
 * signature: a compromised release could carry a matching checksum. */
static bool verify_sha256(const char *path, const char *url, char *error, size_t cap) {
    char listing[1024], want[65], got[65];
    if (!fetch_text(url, listing, sizeof listing) ||
        !checksum_parse_hex(listing, want, sizeof want)) {
        snprintf(error, cap, "The release checksum is missing.");
        return false;
    }
    if (!checksum_hash_file(path, got, sizeof got)) {
        snprintf(error, cap, "Could not verify the download.");
        return false;
    }
    if (strcmp(want, got) != 0) {
        snprintf(error, cap, "The download failed its checksum.");
        return false;
    }
    return true;
}

/* The digest rides along with the release JSON, so the common path needs no
 * second request. The .sha256 asset covers API replies that omit it. */
static bool verify_download(const char *path, const char *digest, const char *url, char *error,
                            size_t cap) {
    if (!digest || !digest[0]) return verify_sha256(path, url, error, cap);
    char got[65];
    if (!checksum_hash_file(path, got, sizeof got)) {
        snprintf(error, cap, "Could not verify the download.");
        return false;
    }
    if (strcmp(digest, got) != 0) {
        snprintf(error, cap, "The download failed its checksum.");
        return false;
    }
    return true;
}

/* The download lands next to the target and is renamed into place, so a failed
 * transfer never leaves a half-written executable.
 * shortcut: the GitHub HTTPS connection is the trust boundary, no signature
 * check; add minisign verification if releases are ever signed. */
static void *download_worker(void *arg) {
    GreedUpdate *u = arg;
    char target[PATH_MAX], url[2048], sha_url[2048], sha256[65];
    pthread_mutex_lock(&u->lock);
    snprintf(target, sizeof target, "%s", u->target);
    snprintf(url, sizeof url, "%s", u->url);
    snprintf(sha_url, sizeof sha_url, "%s", u->sha_url);
    snprintf(sha256, sizeof sha256, "%s", u->sha256);
    pthread_mutex_unlock(&u->lock);

    char error[512] = {0};
    int stage = UPD_ERROR;

    if (!target[0]) {
        snprintf(error, sizeof error, "There is no folder to install into.");
    } else if (!url[0]) {
        snprintf(error, sizeof error, "The release has no download link.");
    } else {
        char dir[PATH_MAX], tmp[PATH_MAX];
        snprintf(dir, sizeof dir, "%s", target);
        char *slash = strrchr(dir, '/');
        if (slash) *slash = 0;
        else snprintf(dir, sizeof dir, ".");
        mkdir_p(dir);
        snprintf(tmp, sizeof tmp, "%s/.greed-update-XXXXXX", dir);

        int fd = mkstemp(tmp);
        if (fd < 0) {
            snprintf(error, sizeof error, "Could not write the update: %s.", strerror(errno));
        } else {
            FILE *f = fdopen(fd, "wb");
            CURLcode rc = CURLE_FAILED_INIT;
            long code = 0;
            if (f) {
                CURL *c = curl_easy_init();
                if (c) {
                    curl_easy_setopt(c, CURLOPT_URL, url);
                    curl_easy_setopt(c, CURLOPT_USERAGENT, "greed/" GREED_VERSION);
                    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
                    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
                    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
                    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress_cb);
                    curl_easy_setopt(c, CURLOPT_XFERINFODATA, u);
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
            } else {
                close(fd);
            }

            unsigned char magic[20] = {0};
            FILE *check = fopen(tmp, "rb");
            size_t got = check ? fread(magic, 1, sizeof magic, check) : 0;
            if (check) fclose(check);

            if (rc != CURLE_OK) {
                if (rc == CURLE_HTTP_RETURNED_ERROR)
                    snprintf(error, sizeof error, "The release asset could not be downloaded.");
                else if (rc == CURLE_OPERATION_TIMEDOUT || rc == CURLE_COULDNT_CONNECT)
                    snprintf(error, sizeof error, "%s", net_error(rc));
                else
                    snprintf(error, sizeof error, "The download failed. Try again.");
            } else if (got < sizeof magic || memcmp(magic, "\x7f" "ELF", 4) ||
                       magic[4] != 2 || magic[18] != 0x3E || magic[19] != 0) {
                snprintf(error, sizeof error, "The downloaded file is not an x86-64 AppImage.");
            } else if (!verify_download(tmp, sha256, sha_url, error, sizeof error)) {
                /* error already set */
            } else if (chmod(tmp, 0755) != 0) {
                snprintf(error, sizeof error, "Could not make the update executable: %s.",
                         strerror(errno));
            } else if (rename(tmp, target) != 0) {
                snprintf(error, sizeof error, "Could not install the update: %s.",
                         strerror(errno));
            } else {
                stage = UPD_INSTALLED;
            }
            if (stage != UPD_INSTALLED) unlink(tmp);
        }
    }

    pthread_mutex_lock(&u->lock);
    u->stage = stage;
    snprintf(u->error, sizeof u->error, "%s", error);
    pthread_mutex_unlock(&u->lock);
    return NULL;
}

void update_init(GreedUpdate *u) {
    memset(u, 0, sizeof *u);
    pthread_mutex_init(&u->lock, NULL);
    u->stage = UPD_IDLE;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    const char *app = getenv("APPIMAGE");
    if (app && *app) {
        char real[PATH_MAX];
        u->appimage = true;
        snprintf(u->target, sizeof u->target, "%s", realpath(app, real) ? real : app);
    } else {
        const char *home = getenv("HOME");
        if (home && *home) snprintf(u->target, sizeof u->target, "%s/.local/bin/greed", home);
    }
}

static void start(GreedUpdate *u, int stage, void *(*fn)(void *)) {
    pthread_mutex_lock(&u->lock);
    bool busy = u->stage == UPD_CHECKING || u->stage == UPD_DOWNLOADING;
    if (!busy) {
        u->stage = stage;
        u->progress = 0;
        u->error[0] = 0;
    }
    pthread_mutex_unlock(&u->lock);
    if (busy) return;

    pthread_t t;
    if (pthread_create(&t, NULL, fn, u) != 0) {
        pthread_mutex_lock(&u->lock);
        u->stage = UPD_ERROR;
        snprintf(u->error, sizeof u->error, "Could not start the update task.");
        pthread_mutex_unlock(&u->lock);
        return;
    }
    pthread_detach(t);
}

void update_check(GreedUpdate *u) { start(u, UPD_CHECKING, check_worker); }
void update_download(GreedUpdate *u) { start(u, UPD_DOWNLOADING, download_worker); }

void update_snapshot(GreedUpdate *u, GreedUpdateView *v) {
    pthread_mutex_lock(&u->lock);
    v->stage = u->stage;
    v->appimage = u->appimage;
    v->progress = u->progress;
    snprintf(v->version, sizeof v->version, "%s", u->version);
    snprintf(v->error, sizeof v->error, "%s", u->error);
    snprintf(v->target, sizeof v->target, "%s", u->target);
    pthread_mutex_unlock(&u->lock);
}
