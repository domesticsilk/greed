#ifndef GREED_TRAY_H
#define GREED_TRAY_H

/* Starts a detached tray process after the GUI exits. It docks one icon while
 * downloads are active or waiting and exits when they finish, the engine
 * stops, or the GUI starts again. Does nothing without an X11 tray. */
void tray_spawn(void);

/* `greed tray`: run the indicator in the foreground. */
int tray_run(void);

#endif
