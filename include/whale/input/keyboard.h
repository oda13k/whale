
#ifndef WHALE_INPUT_KEYBOARD_H
#define WHALE_INPUT_KEYBOARD_H

#include <whale/client/surface.h>
#include <whale/input/seat.h>

#define WH_KEYBOARD_MOD_NORMAL WLR_MODIFIER_ALT

#define WH_KEYBOARD_MODS_DISCARD_CAPS(_mods) (_mods & ~((u32)WLR_MODIFIER_CAPS))

typedef struct
{
    struct wlr_keyboard_group* wlr_keyboard_group;
    struct wl_event_source* key_repeat_source;
} KeyboardGroup;

int wh_keyboard_init(struct wlr_seat* seat);
void wh_keyboard_destroy();

int wh_keyboard_attach_device(struct wlr_keyboard* keyboard);

void wh_keyboard_focus_surface(WhaleSurface* surface);
void wh_keyboard_unfocus();

WhaleSurface* wh_keyboard_get_focused_surface();

bool wh_keyboard_is_modifier_active(u32 mod);

#endif // !WHALE_INPUT_KEYBOARD_H
