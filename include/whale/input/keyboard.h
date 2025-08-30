
#ifndef WHALE_INPUT_KEYBOARD_H
#define WHALE_INPUT_KEYBOARD_H

#include <whale/compositor.h>
#include <whale/types.h>

typedef u32 wh_keyboard_mod_t;

typedef union
{
    u64 unsigned_64;
    char* string;
} BindingArg;

typedef struct
{
    xkb_keysym_t key;
    wh_keyboard_mod_t mod;
    void (*callback)(const BindingArg* arg);
    BindingArg arg;
} WhaleKeyboardBinding;

int wh_input_keyboard_ss_init(WhaleCompositor* comp);

int wh_input_keyboard_add(struct wlr_keyboard* keyboard, WhaleCompositor* comp);

#endif // !WHALE_INPUT_KEYBOARD_H
