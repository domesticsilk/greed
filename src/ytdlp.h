#ifndef GREED_YTDLP_H
#define GREED_YTDLP_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

struct json_object;

typedef struct {
    pthread_mutex_t lock;
    bool running;
    int done;
    int total;
    char error[512];
} YtdlpJob;

/* Index of the best single-file format in a yt-dlp formats array, or -1. */
int ytdlp_best_format(struct json_object *formats);

void ytdlp_init(YtdlpJob *j);
bool ytdlp_available(void);
bool ytdlp_start(YtdlpJob *j, int rpc_port, const char *text, const char *dir);
void ytdlp_snapshot(YtdlpJob *j, bool *running, int *done, int *total, char *error, size_t cap);

#endif
