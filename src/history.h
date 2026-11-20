#ifndef GREED_HISTORY_H
#define GREED_HISTORY_H

#include <stdbool.h>
#include <stddef.h>

/* Longest download key: "p:" + dir + "/" + name. */
#define HISTORY_KEY_MAX 1600

/* Replace the in-memory map with the one on disk. Called lazily. */
void history_load(void);

/* First-seen time for key; records now when the key is new. */
long long history_added(const char *key, long long now);

/* Drop a key once its download is gone. */
void history_forget(const char *key);

/* Write pending changes to history.json. */
void history_flush(void);

#endif
