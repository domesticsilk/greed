#ifndef GREED_YTDLP_H
#define GREED_YTDLP_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "model.h"
#include "request.h"

struct json_object;

/* Highest stream height to keep; 0 means best available. */
typedef enum {
    YTDLP_QUALITY_BEST = 0,
    YTDLP_QUALITY_1080,
    YTDLP_QUALITY_720,
    YTDLP_QUALITY_480,
    YTDLP_QUALITY_360
} YtdlpQuality;

typedef struct {
    pthread_mutex_t lock;
    bool running;
    int done;
    int total;
    int processed;
    char error[512];
    char note[256];
} YtdlpJob;

/* Index of the best single-file format in a yt-dlp formats array, or -1. */
int ytdlp_best_format(struct json_object *formats);

void ytdlp_init(YtdlpJob *j);
bool ytdlp_available(void);
bool ytdlp_ffmpeg_available(void);
/* Picks a video-only and audio-only pair within max_height (0 = best). */
bool ytdlp_pick_pair(struct json_object *formats, int max_height, int *video, int *audio);
bool ytdlp_start(YtdlpJob *j, int rpc_port, const char *text, const char *dir,
                 YtdlpQuality quality, const RequestOptions *req);
void ytdlp_snapshot(YtdlpJob *j, bool *running, int *done, int *total, int *processed,
                    char *error, size_t cap, char *note, size_t note_cap);

/* Called after every aria2 poll with all parsed rows. Aggregates the hidden
 * audio half into its video row, starts ffmpeg when both halves are done, and
 * marks rows that must not be shown. */
void ytdlp_track(Download *items, int n, int port);
bool ytdlp_is_audio_part(const char *gid);
bool ytdlp_is_video(const char *gid);
bool ytdlp_is_youtube(const char *gid);
bool ytdlp_busy(void);
/* The hidden audio half of a merged download, if there is one. */
bool ytdlp_audio_gid(const char *gid, char *out, size_t cap);
/* Cancels a tracked job. Returns true and fills audio_gid when the row has a
 * hidden audio half that the caller must remove from aria2 too. */
bool ytdlp_forget(const char *gid, char *audio_gid, size_t cap);

#endif
