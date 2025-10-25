
#ifndef WHALE_INPUT_POINTER_H
#define WHALE_INPUT_POINTER_H

#include <linux/input-event-codes.h>
#include <whale/client/surface.h>
#include <whale/input/seat.h>
#include <wlr/xcursor.h>

int wh_pointer_init(struct wlr_seat* seat);
void wh_pointer_destroy();

int wh_pointer_attach_device(struct wlr_pointer* pointer);

WhaleSurface* wh_pointer_focus_update();
bool wh_pointer_focus_lost_surface(WhaleSurface* surface);

void wh_pointer_start_interactive_move(u32 button, WhaleSurface* surface);
void wh_pointer_start_interactive_resize(
    u32 button, u32 edge, WhaleSurface* surface
);
void wh_pointer_drop_interactive();

void wh_pointer_set_texture(const char* name);
struct wlr_xcursor* wh_pointer_get_texture(const char* name);

void wh_pointer_set_pos(const WhalePosition2D* pos);
void wh_pointer_get_pos(WhalePosition2D* out_pos);

#endif // !WHALE_INPUT_POINTER_H
