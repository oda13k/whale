
#ifndef WHALE_INPUT_KEYBOARD_BINDINGS_H
#define WHALE_INPUT_KEYBOARD_BINDINGS_H

#include <whale/input/keyboard.h>
#include <whale/types.h>
#include <xkbcommon/xkbcommon.h>

int wh_keyboard_bindings_init();

void wh_keyboard_bindings_destroy();

bool wh_keyboard_bindings_modifiers_match(u32 modifiers);

bool wh_keyboard_bindings_try_handle_key(
    const xkb_keysym_t* keysims,
    size_t keysim_count,
    u32 modifiers,
    enum wl_keyboard_key_state key_state
);

#endif // WHALE_INPUT_KEYBOARD_BINDINGS_H
