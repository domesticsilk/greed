#ifndef GREED_REQUEST_H
#define GREED_REQUEST_H

#include <stdbool.h>
#include <stddef.h>

struct json_object;

#define REQUEST_REFERER_MAX 2048
#define REQUEST_USER_AGENT_MAX 512
#define REQUEST_COOKIE_MAX 4096
#define REQUEST_HEADERS_MAX 8192

/* Per-download HTTP headers. Values stay in memory: nothing here is saved. */
typedef struct {
    char referer[REQUEST_REFERER_MAX];
    char user_agent[REQUEST_USER_AGENT_MAX];
    char cookie[REQUEST_COOKIE_MAX];
    char headers[REQUEST_HEADERS_MAX]; /* one "Name: value" per line */
} RequestOptions;

bool request_options_empty(const RequestOptions *o);
void request_options_clear(RequestOptions *o);

/* False with a message when a header line has no valid "Name: value". */
bool request_options_validate(const RequestOptions *o, char *err, size_t cap);

/* True when the user supplied this header, in a field or as an extra line. */
bool request_options_has_header(const RequestOptions *o, const char *name);

/* Appends the user's headers to an aria2 "header" array. */
void request_options_add_headers(struct json_object *headers, const RequestOptions *o);
/* Adds a "header" array to an aria2 options object when any header is set. */
void request_options_apply_aria2(struct json_object *opts, const RequestOptions *o);

/* Writes the headers to an unlinked 0600 file for yt-dlp and returns its
 * inherited fd. path receives "/dev/fd/N" for --config-locations. Values
 * stay out of the process list. Returns -1 on failure. */
int request_options_ytdlp_config(const RequestOptions *o, char *path, size_t cap);

#endif
