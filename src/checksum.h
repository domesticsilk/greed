#ifndef GREED_CHECKSUM_H
#define GREED_CHECKSUM_H

#include <stdbool.h>
#include <stddef.h>

/* Blocking SHA-256 for installers. False when sha256sum is missing or the file cannot be read. */
bool checksum_hash_file(const char *path, char *hex, size_t cap);

/* Queue a SHA-256 job for path. False when it is already queued/running or the queue is full. */
bool checksum_queue(const char *path);

/* Non-blocking. Returns 1 when a job finished, filling path and hex (hex is empty on failure). */
int checksum_poll(char *path, size_t path_cap, char *hex, size_t hex_cap);

/* Stored state for path: 0 none, 1 verified, 2 mismatch. Fills hex when present. */
int checksum_stored(const char *path, char *hex, size_t cap);

/* Persist a checksum. On a mismatch pass the stored baseline with verified = false. */
bool checksum_record(const char *path, const char *hex, bool verified);

#endif
