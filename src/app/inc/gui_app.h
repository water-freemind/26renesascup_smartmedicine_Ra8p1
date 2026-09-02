#ifndef GUI_APP_H
#define GUI_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the GUI Guider generated UI on the active LVGL display. */
bool gui_app_init(void);
void gui_app_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* GUI_APP_H */
