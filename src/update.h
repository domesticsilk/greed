#ifndef GREED_UPDATE_H
#define GREED_UPDATE_H

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>

enum {
    UPD_IDLE = 0,
    UPD_CHECKING,
    UPD_CURRENT,
    UPD_AVAILABLE,
    UPD_DOWNLOADING,
    UPD_INSTALLED,
    UPD_ERROR,
};

typedef struct {
    pthread_mutex_t lock;
    int stage;
    bool appimage;
    float progress;
    char version[64];
    char url[2048];
    char sha_url[2048];
    char sha256[65];
    char error[512];
    char target[PATH_MAX];
} GreedUpdate;

typedef struct {
    int stage;
    bool appimage;
    float progress;
    char version[64];
    char error[512];
    char target[PATH_MAX];
} GreedUpdateView;

void update_init(GreedUpdate *u);
void update_check(GreedUpdate *u);
void update_download(GreedUpdate *u);
void update_snapshot(GreedUpdate *u, GreedUpdateView *v);

#endif
