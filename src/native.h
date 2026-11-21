#ifndef GREED_NATIVE_H
#define GREED_NATIVE_H

/* Speaks the Chromium native messaging protocol on stdin/stdout and hands the
 * links to the running Greed window, starting it when needed. */
int native_host_run(void);

#endif
