#include <assert.h>
#include <json-c/json.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/rpc.h"
#include "../src/ytdlp.h"

static char fake_tool[PATH_MAX];

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "check failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            abort();                                                             \
        }                                                                        \
    } while (0)

const char *market_tool_path(const char *id, const char *exec) {
    (void)id;
    (void)exec;
    return fake_tool[0] ? fake_tool : NULL;
}

void rpc_init(Rpc *r, int port) {
    (void)r;
    (void)port;
}

struct json_object *rpc_call(Rpc *r, const char *method, struct json_object *params) {
    (void)r;
    (void)method;
    (void)params;
    return NULL;
}

static int pick(const char *json) {
    struct json_object *formats = json_tokener_parse(json);
    assert(formats);
    int index = ytdlp_best_format(formats);
    json_object_put(formats);
    return index;
}

static void pair(const char *json, int max_height, int want_v, int want_a) {
    struct json_object *formats = json_tokener_parse(json);
    assert(formats);
    int v = -1, a = -1;
    bool ok = ytdlp_pick_pair(formats, max_height, &v, &a);
    json_object_put(formats);
    assert(ok == (want_v >= 0));
    assert(v == want_v);
    assert(a == want_a);
}

static void journal_recovery(void) {
    char tmpl[] = "/tmp/greed-ytdlp-XXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL);
    CHECK(setenv("XDG_CONFIG_HOME", dir, 1) == 0);
    char cfg[PATH_MAX];
    snprintf(cfg, sizeof cfg, "%s/greed", dir);
    CHECK(mkdir(cfg, 0755) == 0);

    char journal[PATH_MAX], video[PATH_MAX], audio[PATH_MAX], tmp[PATH_MAX];
    snprintf(journal, sizeof journal, "%s/merge.queue", cfg);
    snprintf(video, sizeof video, "%s/video.mp4", dir);
    snprintf(audio, sizeof audio, "%s/video.greed-137-audio.m4a", dir);
    snprintf(tmp, sizeof tmp, "%s/video.greed-tmp.mp4", dir);
    FILE *f = fopen(journal, "w");
    CHECK(f != NULL);
    fprintf(f, "0%c0%cmp4%c%s%c%s%c%s%c%s\n", 0x1f, 0x1f, 0x1f, video, 0x1f,
            audio, 0x1f, video, 0x1f, tmp);
    fclose(f);

    YtdlpJob job;
    ytdlp_init(&job);
    assert(ytdlp_busy());

    /* After a restart aria2 hands out new GIDs; the paths pair them again. */
    Download items[2];
    memset(items, 0, sizeof items);
    snprintf(items[0].gid, sizeof items[0].gid, "v1");
    snprintf(items[0].status, sizeof items[0].status, "active");
    snprintf(items[0].dir, sizeof items[0].dir, "%s", dir);
    snprintf(items[0].name, sizeof items[0].name, "video.mp4");
    items[0].file_count = 1;
    snprintf(items[1].gid, sizeof items[1].gid, "a1");
    snprintf(items[1].status, sizeof items[1].status, "active");
    snprintf(items[1].dir, sizeof items[1].dir, "%s", dir);
    snprintf(items[1].name, sizeof items[1].name, "video.greed-137-audio.m4a");
    items[1].file_count = 1;
    ytdlp_track(items, 2, 6800);
    assert(ytdlp_is_video("v1"));
    assert(ytdlp_is_audio_part("a1"));

    /* The journal kept the pair, so a second restart recovers it too. */
    YtdlpJob again;
    ytdlp_init(&again);
    assert(ytdlp_busy());
    ytdlp_track(items, 2, 6800);
    assert(ytdlp_is_video("v1"));
    assert(ytdlp_is_audio_part("a1"));

    /* Both halves finished while the app was closed: aria2 lists nothing,
     * the journal is still in the download stage, and the files are done. */
    snprintf(fake_tool, sizeof fake_tool, "%s/ffmpeg", dir);
    FILE *ff = fopen(fake_tool, "w");
    CHECK(ff != NULL);
    fputs("#!/bin/sh\n"
          "src=\n"
          "while [ $# -gt 0 ]; do\n"
          "  if [ \"$1\" = \"-i\" ]; then shift; src=$1; break; fi\n"
          "  shift\n"
          "done\n"
          "for last; do :; done\n"
          "cp \"$src\" \"$last\"\n",
          ff);
    fclose(ff);
    CHECK(chmod(fake_tool, 0755) == 0);

    FILE *vf = fopen(video, "w");
    CHECK(vf != NULL);
    fputs("video", vf);
    fclose(vf);
    FILE *af = fopen(audio, "w");
    CHECK(af != NULL);
    fputs("audio", af);
    fclose(af);

    f = fopen(journal, "w");
    CHECK(f != NULL);
    fprintf(f, "0%c0%cmp4%c%s%c%s%c%s%c%s\n", 0x1f, 0x1f, 0x1f, video, 0x1f,
            audio, 0x1f, video, 0x1f, tmp);
    fclose(f);

    YtdlpJob recovered;
    ytdlp_init(&recovered);
    CHECK(ytdlp_busy());
    ytdlp_track(NULL, 0, 6800);
    for (int i = 0; i < 100 && ytdlp_busy(); i++) usleep(10000);
    CHECK(!ytdlp_busy());
    struct stat sb;
    CHECK(stat(video, &sb) == 0 && sb.st_size == 5);
    CHECK(access(audio, F_OK) != 0);

    unlink(journal);
    unlink(fake_tool);
    unlink(video);
    unlink(tmp);
    rmdir(cfg);
    rmdir(dir);
}

int main(void) {
    assert(pick("[]") == -1);

    /* Separate tracks are not a single file. */
    assert(pick("[{\"url\":\"https://x/v.mp4\",\"vcodec\":\"avc1\",\"acodec\":\"none\"},"
                "{\"url\":\"https://x/a.m4a\",\"vcodec\":\"none\",\"acodec\":\"mp4a\"}]") == -1);

    /* A combined mp4 beats a higher-resolution webm. */
    assert(pick("[{\"url\":\"https://x/v.webm\",\"vcodec\":\"vp9\",\"acodec\":\"opus\","
                "\"ext\":\"webm\",\"height\":1080},"
                "{\"url\":\"https://x/v.mp4\",\"vcodec\":\"avc1\",\"acodec\":\"mp4a\","
                "\"ext\":\"mp4\",\"height\":360}]") == 1);

    /* HLS and DASH manifests are not downloadable as one file. */
    assert(pick("[{\"url\":\"https://x/hls.mp4\",\"vcodec\":\"avc1\",\"acodec\":\"mp4a\","
                "\"ext\":\"mp4\",\"protocol\":\"m3u8_native\"},"
                "{\"url\":\"https://x/ok.mp4\",\"vcodec\":\"avc1\",\"acodec\":\"mp4a\","
                "\"ext\":\"mp4\",\"protocol\":\"https\"}]") == 1);
    assert(pick("[{\"url\":\"https://x/seg.mp4\",\"vcodec\":\"avc1\",\"acodec\":\"mp4a\","
                "\"ext\":\"mp4\",\"protocol\":\"http_dash_segments\"}]") == -1);

    /* Generic extractors omit codec names; the file is still usable. */
    assert(pick("[{\"url\":\"https://x/a.avi\",\"ext\":\"avi\"},"
                "{\"url\":\"https://x/b.mp4\",\"ext\":\"mp4\"}]") == 1);
    assert(pick("[{\"url\":\"https://x/only.mp4\",\"ext\":\"mp4\"}]") == 0);

    const char *adaptive =
        "[{\"url\":\"https://x/1080.mp4\",\"vcodec\":\"avc1\",\"acodec\":\"none\","
        "\"ext\":\"mp4\",\"height\":1080,\"tbr\":4000},"
        "{\"url\":\"https://x/720.mp4\",\"vcodec\":\"avc1\",\"acodec\":\"none\","
        "\"ext\":\"mp4\",\"height\":720,\"tbr\":2500},"
        "{\"url\":\"https://x/audio.m4a\",\"vcodec\":\"none\",\"acodec\":\"mp4a\","
        "\"ext\":\"m4a\",\"abr\":128},"
        "{\"url\":\"https://x/audio.webm\",\"vcodec\":\"none\",\"acodec\":\"opus\","
        "\"ext\":\"webm\",\"abr\":160},"
        "{\"url\":\"https://x/combined.mp4\",\"vcodec\":\"avc1\",\"acodec\":\"mp4a\","
        "\"ext\":\"mp4\",\"height\":360,\"tbr\":700}]";

    pair(adaptive, 0, 0, 2);     /* best video, m4a audio */
    pair(adaptive, 720, 1, 2);   /* highest video at or below 720 */
    pair(adaptive, 480, -1, -1); /* no video under 480: caller falls back to combined */

    /* A video-only manifest is not a downloadable half. */
    pair("[{\"url\":\"https://x/1080.m3u8\",\"vcodec\":\"avc1\",\"acodec\":\"none\","
         "\"ext\":\"mp4\",\"height\":1080,\"protocol\":\"m3u8_native\"},"
         "{\"url\":\"https://x/a.m4a\",\"vcodec\":\"none\",\"acodec\":\"mp4a\","
         "\"ext\":\"m4a\",\"abr\":128}]",
         0, -1, -1);

    journal_recovery();

    puts("ytdlp ok");
    return 0;
}
