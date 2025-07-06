
#ifndef WHALE_INPUT_H
#define WHALE_INPUT_H

#include <whale/compositor.h>
#include <xkbcommon/xkbcommon.h>

int wh_input_init(WhaleCompositor* comp);

WhaleClient* wh_input_focus_client_under(const wh_pos2d_t* pos);

WhaleClient* wh_input_get_focused_client();

wh_pos2d_t wh_input_get_cursor_pos();

#endif // !WHALE_INPUT_H
