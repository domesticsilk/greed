#ifndef GREED_YTDLP_H
#define GREED_YTDLP_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    pthread_mutex_t lock;
    bool running;
    int done;
    int total;
    char error[512];
} YtdlpJob;

void ytdlp_init(YtdlpJob *j);
bool ytdlp_available(void);
bool ytdlp_start(YtdlpJob *j, int rpc_port, const char *text, const char *dir);
void ytdlp_snapshot(YtdlpJob *j, bool *running, int *done, int *total, char *error, size_t cap);

#endif
