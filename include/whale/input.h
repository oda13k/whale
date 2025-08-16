
#ifndef WHALE_INPUT_H
#define WHALE_INPUT_H

#include <whale/client/surface.h>
#include <whale/compositor.h>

int wh_input_init(WhaleCompositor* comp);

WhaleSurface* wh_input_refocus(bool force_keyboard);

WhaleSurface* wh_input_get_focused_surface();

WhalePosition2D wh_input_get_cursor_pos();

void wh_input_start_interactive_move(WhaleSurface* surface);
void wh_input_start_interactive_resize(u32 edge, WhaleSurface* surface);

#endif // !WHALE_INPUT_H
