#ifndef GREED_LINKS_H
#define GREED_LINKS_H

#include <stdbool.h>

#define GREED_LINKS_MAX 16384

/* True when an argument is a download link or a torrent/metalink path. */
bool greed_link_arg(const char *s);

/* Join argv with newlines, turning file:// URIs into local paths.
 * Returns a malloc'd string capped at GREED_LINKS_MAX, or NULL. */
char *greed_join_links(int argc, char **argv);

#endif
