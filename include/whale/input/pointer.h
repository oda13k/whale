
#ifndef WHALE_INPUT_POINTER_H
#define WHALE_INPUT_POINTER_H

#include <whale/client/surface.h>
#include <whale/input/seat.h>

int wh_pointer_init(struct wlr_seat* seat);
void wh_pointer_destroy();

int wh_pointer_attach_device(struct wlr_pointer* pointer);

WhaleSurface* wh_pointer_update_focus(bool allow_keyboard);

void wh_pointer_start_interactive_move(WhaleSurface* surface);
void wh_pointer_start_interactive_resize(u32 edge, WhaleSurface* surface);
void wh_pointer_drop_interactive();

void wh_pointer_set_texture(const char* name);

void wh_pointer_get_pos(WhalePosition2D* out_pos);

#endif // !WHALE_INPUT_POINTER_H
