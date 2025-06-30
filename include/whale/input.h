
#ifndef _WHALE_INPUT_H
#define _WHALE_INPUT_H

#include <whale/compositor.h>
#include <xkbcommon/xkbcommon.h>

int wh_input_init(WhaleCompositor* comp);

int wh_input_refocus_all_inputs_on_topmost_client_under_cursor(
    WhaleCompositor* comp, double* focused_surface_x, double* focused_surface_y
);

int wh_input_focus_on_client(WhaleClient* client);

WhaleClient* wh_input_get_focused_client();

#endif // !_WHALE_INPUT_H
