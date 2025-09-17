
#ifndef WHALE_INPUT_CLIPBOARD_H
#define WHALE_INPUT_CLIPBOARD_H

#include <whale/compositor.h>
#include <whale/input/seat.h>

int wh_clipboard_init(struct wlr_seat* seat);

void wh_clipboard_destroy();

#endif // !WHALE_INPUT_CLIPBOARD_H
