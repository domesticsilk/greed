#include <assert.h>
#include <json-c/json.h>
#include <stdbool.h>
#include <stdio.h>

#include "../src/rpc.h"
#include "../src/ytdlp.h"

const char *market_tool_path(const char *id, const char *exec) {
    (void)id;
    (void)exec;
    return NULL;
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

    puts("ytdlp ok");
    return 0;
}
