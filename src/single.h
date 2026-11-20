#ifndef GREED_SINGLE_H
#define GREED_SINGLE_H

#include <stdbool.h>

/* One GUI instance per user. The first process holds the lock and serves
 * link handoffs; later processes write the links and exit. */
bool single_acquire(void);
void single_set_window(unsigned long win);
bool single_focus(void);
bool single_handoff(const char *links);
char *single_take_handoff(void);
void single_release(void);

#endif
