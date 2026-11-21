#ifndef GREED_MARKET_H
#define GREED_MARKET_H

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

enum { MKT_IDLE = 0, MKT_LOADING, MKT_READY, MKT_ERROR };
enum { MKT_JOB_NONE = 0, MKT_JOB_DOWNLOADING, MKT_JOB_INSTALLED, MKT_JOB_ERROR };
enum { MKT_THEME = 0, MKT_ENGINE, MKT_TOOL };

#define MKT_MAX_ITEMS 48
#define MKT_MAX_INSTALLED 48

typedef struct {
    char id[64];
    char name[96];
    char summary[256];
    char description[1024];
    char category[32];
    char version[64];
    char source[512];
    char url[2048];
    char sha256[65];
    char exec[64];
    char requires[128];
    long long size;
    int kind;
    /* Optional wallpaper shipped with a theme, installed next to its JSON. */
    char wallpaper_url[2048];
    char wallpaper_sha256[65];
    char wallpaper_name[128];
    unsigned preview_bg, preview_surface, preview_accent, preview_text;
} MarketItem;

typedef struct {
    char id[64];
    char version[64];
    char path[PATH_MAX];
    char extra[PATH_MAX]; /* wallpaper installed with a theme, or empty */
} MarketInstalled;

typedef struct {
    pthread_mutex_t lock;
    int stage;
    char error[512];
    char note[256];
    int count;
    MarketItem items[MKT_MAX_ITEMS];
    int installed_count;
    MarketInstalled installed[MKT_MAX_INSTALLED];
    int job_item;
    int job_stage;
    float progress;
    char job_error[512];
    char status[256];
    long long fetched_at;
} Market;

typedef struct {
    int stage;
    char error[512];
    char note[256];
    int count;
    MarketItem items[MKT_MAX_ITEMS];
    int installed_count;
    MarketInstalled installed[MKT_MAX_INSTALLED];
    int job_item;
    int job_stage;
    float progress;
    char job_error[512];
    char status[256];
} MarketView;

void market_init(Market *m);
void market_fetch(Market *m, bool force);
void market_install(Market *m, int index);
bool market_remove(Market *m, int index);
void market_snapshot(Market *m, MarketView *v);
int market_installed_version(const MarketView *v, const char *id, char *out, size_t cap);
bool market_managed(Market *m, const char *id);
const char *market_tool_path(const char *id, const char *exec);
bool market_tool_available(const char *id);

#endif
