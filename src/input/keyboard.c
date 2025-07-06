
#include <whale/client/client.h>
#include <whale/input.h>
#include <whale/input/keyboard.h>
#include <whale/log.h>
#include <whale/utils.h>
#include <xkbcommon/xkbcommon.h>

static const struct xkb_rule_names g_static_xkb_rules = {
    /* can specify fields: rules, model, layout, variant, options */
    /* example:
    .options = "ctrl:nocaps",
    */
    .options = "caps:swapescape",
};

static WhaleCompositor* g_comp;

#define MOD_NORMAL WLR_MODIFIER_LOGO
#define MOD_IMPORTANT (MOD_NORMAL | WLR_MODIFIER_SHIFT)

#define MODS_DISCARD_CAPS(mods) (mods & (~WLR_MODIFIER_CAPS))

static void terminate_focused_client(void*)
{
    WhaleClient* client = wh_input_get_focused_client();
    if (client)
        wh_client_send_close(client);
}

static void spawn_term(void*)
{
    wh_spawn_process("/bin/alacritty");
}

static const WhaleKeyboardBinding g_static_bindings[] = {
    {.mod = MOD_IMPORTANT,
     .key = XKB_KEY_c,
     .callback = terminate_focused_client},
    {.mod = MOD_NORMAL, .key = XKB_KEY_grave, .callback = spawn_term},
};

static size_t binding_count =
    sizeof(g_static_bindings) / sizeof(WhaleKeyboardBinding);

static void on_keyboard_key(struct wl_listener* listener, void* data)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.keyboard_key);
    struct wlr_keyboard_key_event* ev = data;

    /* Get a list of keysims for the pressed keycode. This is a list
    because a keycode *can* correspond to multiple xkb keysyms depending
    on the keyboard layout? */
    const xkb_keysym_t* keys;
    const size_t key_count = xkb_state_key_get_syms(
        comp->keyboard_group.wlr_keyboard_group->keyboard.xkb_state,
        ev->keycode + 8,
        &keys
    );

    const u32 mods = wlr_keyboard_get_modifiers(
        &comp->keyboard_group.wlr_keyboard_group->keyboard
    );

    bool handled = false;
    if (ev->state == WL_KEYBOARD_KEY_STATE_PRESSED)
    {
        for (size_t i = 0; i < binding_count && !handled; ++i)
        {
            const WhaleKeyboardBinding* binding = &g_static_bindings[i];
            /* Discard caps because we don't care if that is active or not
            for our use case. */
            if (MODS_DISCARD_CAPS(mods) != binding->mod)
                continue;

            for (size_t k = 0; k < key_count; ++k)
            {
                if (xkb_keysym_to_lower(keys[k]) == binding->key)
                {
                    binding->callback(binding->callback_data);
                    handled = true;
                    break;
                }
            }
        }
    }

    if (!handled)
    {
        /* Pass it along to the focused client. */
        wlr_seat_keyboard_notify_key(
            comp->seat, ev->time_msec, ev->keycode, ev->state
        );
    }
}

static void on_keyboard_modifier(struct wl_listener* listener, void*)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.keyboard_modifier);

    wlr_seat_keyboard_notify_modifiers(
        comp->seat, &comp->keyboard_group.wlr_keyboard_group->keyboard.modifiers
    );
}

static int keyrepeat(void* data)
{
    KeyboardGroup* group = data;
    if (group->wlr_keyboard_group->keyboard.repeat_info.rate <= 0)
        return 0;

    wl_event_source_timer_update(
        group->key_repeat_source,
        1000 / group->wlr_keyboard_group->keyboard.repeat_info.rate
    );

    return 0;
}

int wh_input_keyboard_ss_init(WhaleCompositor* comp)
{
    g_comp = comp;

    comp->keyboard_group.wlr_keyboard_group = wlr_keyboard_group_create();
    if (!comp->keyboard_group.wlr_keyboard_group)
    {
        wh_log(ERR, "input: Failed to create wlr keyboard group.");
        return -1;
    }

    struct xkb_context* xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_context)
    {
        wh_log(ERR, "input: Failed to create new xkb context.");
        return -1;
    }

    struct xkb_keymap* xkb_keymap = xkb_keymap_new_from_names(
        xkb_context, &g_static_xkb_rules, XKB_KEYMAP_COMPILE_NO_FLAGS
    );

    if (!xkb_keymap)
    {
        wh_log(ERR, "input: Failed to compile xkb keymap.");
        return -1;
    }

    wlr_keyboard_set_keymap(
        &comp->keyboard_group.wlr_keyboard_group->keyboard, xkb_keymap
    );

    xkb_keymap_unref(xkb_keymap);
    xkb_context_unref(xkb_context);

    wlr_keyboard_set_repeat_info(
        &comp->keyboard_group.wlr_keyboard_group->keyboard, 25, 400
    );
    comp->keyboard_group.key_repeat_source = wl_event_loop_add_timer(
        wl_display_get_event_loop(comp->display),
        keyrepeat,
        &comp->keyboard_group
    );

    /* Set up listeners for keyboard events */
    LISTEN(
        &comp->keyboard_group.wlr_keyboard_group->keyboard.events.key,
        &comp->listeners.keyboard_key,
        on_keyboard_key
    );
    LISTEN(
        &comp->keyboard_group.wlr_keyboard_group->keyboard.events.modifiers,
        &comp->listeners.keyboard_modifier,
        on_keyboard_modifier
    );

    wlr_seat_set_keyboard(
        comp->seat, &comp->keyboard_group.wlr_keyboard_group->keyboard
    );

    return 0;
}

int wh_input_keyboard_add(struct wlr_keyboard* keyboard, WhaleCompositor* comp)
{
    wlr_keyboard_set_keymap(
        keyboard, comp->keyboard_group.wlr_keyboard_group->keyboard.keymap
    );

    wlr_keyboard_group_add_keyboard(
        comp->keyboard_group.wlr_keyboard_group, keyboard
    );

    return 0;
}
