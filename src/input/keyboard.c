
#include "keyboard_bindings.h"
#include "whale/compositor.h"
#include <wayland-server-protocol.h>
#include <whale/client/client.h>
#include <whale/input/keyboard.h>
#include <whale/input/pointer.h>
#include <whale/input/seat.h>
#include <whale/log.h>
#include <whale/utils/proc.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <xkbcommon/xkbcommon.h>

static const struct xkb_rule_names g_static_xkb_rules = {
    /* can specify fields: rules, model, layout, variant, options */
    /* example:
    .options = "ctrl:nocaps",
    */
    .options = "caps:swapescape",
};

static struct wlr_seat* g_seat;

static WhaleSurface* g_focused_surface;

static KeyboardGroup g_keyboard_group;

WH_CALLBACK(keyboard_key, struct wl_listener*, void* data)
{
    struct wlr_keyboard_key_event* ev = data;

    bool handled = false;
    const u32 modifiers = wlr_keyboard_get_modifiers(
        &g_keyboard_group.wlr_keyboard_group->keyboard
    );

    if (wh_keyboard_bindings_modifiers_match(modifiers))
    {
        /* Get a list of keysims for the pressed keycode. This is a list
        because a keycode *can* correspond to multiple xkb keysyms depending
        on the keyboard layout? */
        const xkb_keysym_t* keysims;
        const size_t keysim_count = (size_t)xkb_state_key_get_syms(
            g_keyboard_group.wlr_keyboard_group->keyboard.xkb_state,
            ev->keycode + 8,
            &keysims
        );

        handled = wh_keyboard_bindings_try_handle_key(
            keysims, keysim_count, modifiers, ev->state
        );
    }

    if (!handled)
    {
        /* Pass it along to the focused client. */
        wlr_seat_keyboard_notify_key(
            g_seat, ev->time_msec, ev->keycode, ev->state
        );
    }
}

WH_CALLBACK(keyboard_modifiers, struct wl_listener*, void*)
{
    wlr_seat_keyboard_notify_modifiers(
        g_seat, &g_keyboard_group.wlr_keyboard_group->keyboard.modifiers
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

int wh_keyboard_init(struct wlr_seat* seat)
{
    g_seat = seat;

    g_keyboard_group.wlr_keyboard_group = wlr_keyboard_group_create();
    if (!g_keyboard_group.wlr_keyboard_group)
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
        &g_keyboard_group.wlr_keyboard_group->keyboard, xkb_keymap
    );

    xkb_keymap_unref(xkb_keymap);
    xkb_context_unref(xkb_context);

    wlr_keyboard_set_repeat_info(
        &g_keyboard_group.wlr_keyboard_group->keyboard, 25, 300
    );
    g_keyboard_group.key_repeat_source = wl_event_loop_add_timer(
        wl_display_get_event_loop(wh_compositor_get_wl_display()),
        keyrepeat,
        &g_keyboard_group
    );

    wh_keyboard_bindings_init();

    WH_LISTEN(
        &g_keyboard_group.wlr_keyboard_group->keyboard.events.key, keyboard_key
    );

    WH_LISTEN(
        &g_keyboard_group.wlr_keyboard_group->keyboard.events.modifiers,
        keyboard_modifiers
    );

    wlr_seat_set_keyboard(
        g_seat, &g_keyboard_group.wlr_keyboard_group->keyboard
    );

    return 0;
}

void wh_keyboard_destroy()
{
    wlr_seat_set_keyboard(g_seat, nullptr);

    TODO_LOG(
        "keyboard: Do attached keyboards need to be explicitely destroyed????"
    );

    WH_UNLISTEN(keyboard_modifiers);
    WH_UNLISTEN(keyboard_key);

    wh_keyboard_bindings_destroy();

    wl_event_source_remove(g_keyboard_group.key_repeat_source);
    g_keyboard_group.key_repeat_source = nullptr;

    wlr_keyboard_group_destroy(g_keyboard_group.wlr_keyboard_group);
    g_keyboard_group.wlr_keyboard_group = nullptr;

    g_seat = nullptr;
}

int wh_keyboard_attach_device(struct wlr_keyboard* keyboard)
{
    wlr_keyboard_set_keymap(
        keyboard, g_keyboard_group.wlr_keyboard_group->keyboard.keymap
    );

    wlr_keyboard_group_add_keyboard(
        g_keyboard_group.wlr_keyboard_group, keyboard
    );

    return 0;
}

void wh_keyboard_focus_surface(WhaleSurface* surface)
{
    if (surface == g_focused_surface)
        return;

    WhaleClient* new_client = wh_client_from_surface(surface);
    WhaleClient* old_client =
        g_focused_surface ? wh_client_from_surface(g_focused_surface) : nullptr;

    /* Toplevel client activation follows the keyboard focus */
    if (new_client != old_client)
    {
        if (old_client)
            wh_client_set_active(false, old_client);

        wh_client_set_active(true, new_client);
    }

    struct wlr_keyboard* keyboard =
        &g_keyboard_group.wlr_keyboard_group->keyboard;

    switch (surface->type)
    {
    case SURFACE_TYPE_CLIENT:
        wlr_seat_keyboard_notify_enter(
            g_seat,
            surface->wlr_surface,
            keyboard->keycodes,
            keyboard->num_keycodes,
            &keyboard->modifiers
        );
        g_focused_surface = surface;
        break;

    case SURFACE_TYPE_POPUP:
    case SURFACE_TYPE_SUBSURFACE:
        /* If we are a popup or subsurface we want to focus the keyboard
        on our parent window as per spec. */
        wlr_seat_keyboard_notify_enter(
            g_seat,
            new_client->surface->wlr_surface,
            keyboard->keycodes,
            keyboard->num_keycodes,
            &keyboard->modifiers
        );
        g_focused_surface = new_client->surface;
        break;

    default:
        WH_ASSERT_NOT_REACHED();
    }
}

void wh_keyboard_unfocus_unchecked()
{
    WhaleClient* client = wh_client_from_surface(g_focused_surface);
    wh_client_set_active(false, client);

    wlr_seat_keyboard_notify_clear_focus(g_seat);
    g_focused_surface = nullptr;

    wh_pointer_drop_interactive();
}

WhaleSurface* wh_keyboard_get_focused_surface()
{
    return g_focused_surface;
}

bool wh_keyboard_is_modifier_active(u32 mod)
{
    const u32 modifiers = wlr_keyboard_get_modifiers(
        &g_keyboard_group.wlr_keyboard_group->keyboard
    );

    return WH_KEYBOARD_MODS_DISCARD_CAPS(modifiers) == mod;
}
