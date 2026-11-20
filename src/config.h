#ifndef GREED_CONFIG_H
#define GREED_CONFIG_H

#include <stdbool.h>

/* Download table columns; the enum order is the display order. */
enum {
    GREED_COL_NAME = 0,
    GREED_COL_SIZE,
    GREED_COL_PROGRESS,
    GREED_COL_SPEED,
    GREED_COL_ETA,
    GREED_COL_ADDED,
    GREED_COL_LOCATION,
    GREED_COL_CONNECTIONS,
    GREED_COL_STATUS,
    GREED_COL_COUNT
};

#define GREED_COL_BIT(c) (1u << (c))
#define GREED_COLUMNS_DEFAULT                                                  \
    (GREED_COL_BIT(GREED_COL_NAME) | GREED_COL_BIT(GREED_COL_SIZE) |           \
     GREED_COL_BIT(GREED_COL_PROGRESS) | GREED_COL_BIT(GREED_COL_SPEED) |      \
     GREED_COL_BIT(GREED_COL_ETA) | GREED_COL_BIT(GREED_COL_ADDED) |           \
     GREED_COL_BIT(GREED_COL_STATUS))

typedef struct {
    char download_dir[1024];
    int max_concurrent;
    int rpc_port;
    int max_download_limit;    /* KB/s, 0 = unlimited */
    int max_upload_limit;      /* KB/s, 0 = unlimited */
    bool seed_after_complete;
    float seed_ratio;
    bool auto_start_aria2;
    bool clipboard_monitor;
    bool notify_on_complete;
    bool verify_after_complete;
    float motion_speed;        /* 1 = normal, higher is faster */
    char theme[48];
    char font[128];            /* default, sans, serif, mono, or an installed family */
    bool logo_app_font;        /* draw the wordmark in the app font instead of the logo image */
    int sidebar_width;         /* px, 0 = the theme's default */
    float text_scale;          /* 0.85 - 1.30 */
    float spacing;             /* 0.9 compact, 1 normal, 1.12 spacious */
    bool vim_mode;
    unsigned columns;          /* GREED_COL_* bitmask */
    int sort_column;           /* GREED_COL_* */
    bool sort_desc;
    int col_width[GREED_COL_COUNT]; /* 0 = default, points at text scale 1 */
} Config;

#define GREED_MAX_SCHEDULED 64

typedef struct {
    char url[2048];
    char dir[1024];
    long long due;             /* unix seconds */
} GreedScheduled;

void config_defaults(Config *c);
void config_load(Config *c);
bool config_save(const Config *c);
int config_schedules_load(GreedScheduled *out, int cap);
bool config_schedules_save(const GreedScheduled *items, int count);
const char *config_dir(void);
const char *data_dir(void);
const char *config_path(void);
void config_expand_tilde(char *path);

#endif
