#ifndef GREED_CONFIG_H
#define GREED_CONFIG_H

#include <stdbool.h>

typedef struct {
    char download_dir[1024];
    int max_concurrent;
    int rpc_port;
    bool auto_start_aria2;
    bool clipboard_monitor;
    bool notify_on_complete;
} Config;

void config_defaults(Config *c);
void config_load(Config *c);
bool config_save(const Config *c);
const char *config_dir(void);
const char *config_path(void);
void config_expand_tilde(char *path);

#endif
