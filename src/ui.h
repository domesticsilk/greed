#ifndef GREED_UI_H
#define GREED_UI_H

/* Start the GUI. initial_links, when set, is newline-separated links or
 * torrent/metalink paths to prefill in the Add dialog. */
int ui_run(const char *initial_links);

#endif
