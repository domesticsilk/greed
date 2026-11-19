#ifndef GREED_CONFIG_H
#define GREED_CONFIG_H

#include <stdbool.h>

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
const char *config_path(void);
void config_expand_tilde(char *path);

#endif
