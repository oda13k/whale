
#ifndef WHALE_INPUT_KEYBOARD_H
#define WHALE_INPUT_KEYBOARD_H

#include <whale/client/surface.h>
#include <whale/input/seat.h>

typedef struct
{
    struct wlr_keyboard_group* wlr_keyboard_group;
    struct wl_event_source* key_repeat_source;
} KeyboardGroup;

int wh_keyboard_init(struct wlr_seat* seat);

void wh_keyboard_destroy();

int wh_keyboard_attach_device(struct wlr_keyboard* keyboard);

void wh_keyboard_focus_surface(WhaleSurface* surface);

void wh_keyboard_unfocus_unchecked();

WhaleSurface* wh_keyboard_get_focused_surface();

#endif // !WHALE_INPUT_KEYBOARD_H
