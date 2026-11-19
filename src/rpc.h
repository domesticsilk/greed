#ifndef GREED_RPC_H
#define GREED_RPC_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

#include "config.h"

typedef struct {
    int port;
    char error[512];
} Rpc;

void rpc_init(Rpc *r, int port);
void rpc_set_port(Rpc *r, int port);

/* Calls a JSON-RPC method. Takes ownership of params (may be NULL).
 * Returns the "result" value (new reference, caller puts) or NULL; on failure
 * the message is in r->error. */
struct json_object *rpc_call(Rpc *r, const char *method, struct json_object *params);
bool rpc_probe(Rpc *r);

/* aria2 engine */
const char *aria2_binary(void);
const char *aria2_lib_dir(void);
bool aria2_available(void);
bool aria2_spawn(const Config *cfg);
/* Applies the config's global limits to a running engine. */
bool aria2_apply_options(Rpc *r, const Config *cfg);
/* Starts aria2 when it is not running. Returns true when the RPC answers. */
bool aria2_ensure_running(const Config *cfg, bool auto_start);

/* Base64-encodes a whole file (aria2.addTorrent). Caller frees. */
char *base64_file(const char *path, size_t *out_len);

#endif
