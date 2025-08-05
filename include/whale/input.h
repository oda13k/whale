
#ifndef WHALE_INPUT_H
#define WHALE_INPUT_H

#include <whale/compositor.h>
#include <whale/window/client.h>
#include <xkbcommon/xkbcommon.h>

int wh_input_init(WhaleCompositor* comp);

WhaleSurface* wh_input_focus_surface_at_coords(const wh_pos2d_t* pos);

WhaleSurface* wh_input_get_focused_surface();

wh_pos2d_t wh_input_get_cursor_pos();

#endif // !WHALE_INPUT_H
