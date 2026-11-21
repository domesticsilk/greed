#ifndef GREED_BROWSER_H
#define GREED_BROWSER_H

#include <stdbool.h>
#include <stddef.h>

#define GREED_NATIVE_HOST "com.domesticsilk.greed"
#define GREED_EXTENSION_ID "jfbbpclejalgakpjijecldillabpiikh"

/* Parses one native messaging message. Fills newline-joined http(s) links and
 * sets ping for {"type":"ping"}. False with a short error on bad input. */
bool browser_parse_message(const char *json, char *links, size_t cap, bool *ping,
                           char *error, size_t errcap);

/* Registers or removes the native host for every browser profile found. */
int browser_install(void);
int browser_uninstall(void);

#endif
