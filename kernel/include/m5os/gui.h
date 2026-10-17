#pragma once
#include <m5os/types.h>
#include <m5os/msg.h>

/*
 * M5OS window system.
 *
 * Screen (320 x 240):
 *     y   0..15    status bar      (drawn by the GUI service)
 *     y  16..29    title bar       of the front window
 *     y  30..199   client area     320 x 170  - the app draws here
 *     y 200..239   buttons         Home | Next | Close
 *
 * Every window is maximized; windows form a stack and only the FRONT one is visible.  Drawing
 * into a window that is not in front is discarded - the app repaints when it receives
 * GUI_EVT_EXPOSE (sent when its window comes to the front, or on window_draw()).
 *
 * Events reach the window's owner as IPC messages (msg.h) from the GUI task `guid`:
 * use gui_next_event() instead of decoding them by hand.  Coordinates are client-relative.
 *
 * Needs APP_PERM_DISPLAY (to create windows) and APP_PERM_IPC (to receive events).  A task can
 * only draw into windows of its own app.
 */
#define GUI_SCREEN_W      320
#define GUI_SCREEN_H      240
#define GUI_STATUS_H      16
#define GUI_TITLE_Y       16
#define GUI_TITLE_H       14
#define GUI_CLIENT_Y      (GUI_TITLE_Y + GUI_TITLE_H)          /* 30 */
#define GUI_CLIENT_W      GUI_SCREEN_W
#define GUI_CLIENT_H      170
#define GUI_STRIP_Y       (GUI_CLIENT_Y + GUI_CLIENT_H)        /* 200 */
#define GUI_STRIP_H       (GUI_SCREEN_H - GUI_STRIP_Y)
#define GUI_BUTTONS       3

#define GUI_WIN_HOME      0x1u        /* the launcher window: the Home button brings it to the front */

typedef uint32_t window_t;            /* > 0 */

typedef enum {
    GUI_EVT_EXPOSE = 1,               /* (re)paint everything: the window is now visible and empty */
    GUI_EVT_TOUCH_DOWN,
    GUI_EVT_TOUCH_MOVE,
    GUI_EVT_TOUCH_UP,
    GUI_EVT_CLOSE,                    /* the user pressed Close: finish and return from your main function */
} gui_evt_type_t;

typedef struct {
    gui_evt_type_t type;
    window_t       window;
    int            x, y;              /* client-relative (may be outside 0..W-1 on MOVE/UP when the finger left) */
} gui_event_t;

/* ---- windows ------------------------------------------------------------------- */
int  window_create(const char *title, uint32_t flags);      /* window id or negative ERR_*; it comes to the front */
int  window_destroy(window_t w);
int  window_focus(window_t w);                              /* bring to front (the owner gets GUI_EVT_EXPOSE) */
int  window_draw(window_t w);                               /* repaint the frame and ask the owner to repaint (EXPOSE) */
bool window_is_focused(window_t w);

/* ---- drawing, client coordinates, clipped; dropped when the window is not in front ---- */
int  window_clear(window_t w, uint16_t color);
int  window_fill_rect(window_t w, int x, int y, int rw, int rh, uint16_t color);
int  window_draw_rect(window_t w, int x, int y, int rw, int rh, uint16_t color);   /* outline */
int  window_draw_text(window_t w, int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);
int  window_draw_bitmap(window_t w, int x, int y, int bw, int bh, const uint16_t *rgb565);

/* ---- events --------------------------------------------------------------------- */
/* Wait for the next GUI event addressed to the calling task. ERR_TIMEOUT if none in time. */
int  gui_next_event(gui_event_t *ev, uint32_t timeout_ms);
bool gui_decode(const msg_t *m, gui_event_t *ev);          /* false if it is not a GUI message */

/* ---- service ---------------------------------------------------------------------- */
int  gui_init(void);                                       /* start the GUI task and draw the desktop (once) */
int  gui_focus_app(uint32_t app_id);                       /* bring an app's window to the front */
void gui_release_owner(uint32_t app_id);                   /* kernel (appd): an app ended - close its windows */
/* Simulated finger, same path as the real touch panel (used by the shell's `tap` and the tests). */
void gui_inject_touch(int kind /* 0 down, 1 move, 2 up */, int x, int y);
#define GUI_TOUCH_DOWN 0
#define GUI_TOUCH_MOVE 1
#define GUI_TOUCH_UP   2
/* Inspection */
int  gui_window_count(void);
int  gui_front_title(char *out, size_t cap);               /* title of the front window, or ERR_NOENT */
window_t gui_front_window(void);                           /* id of the front window, 0 if none */
uint32_t gui_front_app(void);                              /* app id owning the front window (0 = none / kernel) */
void gui_print_windows(void);

bool gui_selftest(void);
