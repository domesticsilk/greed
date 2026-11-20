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

    puts("ytdlp ok");
    return 0;
}
