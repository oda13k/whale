
#ifndef WHALE_INPUT_SEAT_H
#define WHALE_INPUT_SEAT_H

#include <whale/client/surface.h>
#include <wlr/types/wlr_seat.h>

int wh_seat_init();

void wh_seat_destroy();

WhaleSurface* wh_seat_refocus_input(bool focus_keyboard);

#endif // !WHALE_INPUT_SEAT_H
