#ifndef GREED_MODEL_H
#define GREED_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

typedef struct {
    char gid[32];
    char status[24];
    char name[512];
    char dir[1024];
    char url[2048];
    char info_hash[64];
    char error[512];
    unsigned long long total;
    unsigned long long completed;
    unsigned long long down_speed;
    unsigned long long up_speed;
    unsigned connections;
    unsigned seeders;
    int file_count;
    long long added;           /* first seen by Greed, unix seconds */
    bool is_torrent;
} Download;

typedef struct {
    unsigned long long down_speed;
    unsigned long long up_speed;
    unsigned long long active;
    unsigned long long waiting;
    unsigned long long stopped;
} GlobalStat;

enum { CAT_NONE = 0, CAT_VIDEO, CAT_AUDIO, CAT_IMAGES, CAT_DOCUMENTS, CAT_ARCHIVES,
       CAT_PROGRAMS, CAT_OTHER, CAT_COUNT };

bool download_parse(struct json_object *v, Download *d);
double download_progress(const Download *d);
bool download_eta(const Download *d, unsigned long long *secs);
const char *download_status_label(const Download *d);
bool download_is_finished(const Download *d);
int download_category(const Download *d);

void fmt_speed(char *out, size_t cap, unsigned long long bps);
void fmt_bytes(char *out, size_t cap, unsigned long long bytes);
void fmt_eta(char *out, size_t cap, unsigned long long secs);

#endif
