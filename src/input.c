#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <time.h>
#include <whale/client.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/types.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <xkbcommon/xkbcommon.h>

static void on_cursor_motion(struct wl_listener*, void*)
{
    wh_log(DEBUG, "cursor: motion");
}

static bool wh_input_is_client_focused(const WhaleClient* client)
{
    return client->comp->seat->keyboard_state.focused_surface ==
           client->xdg_toplevel->base->surface;
}

/**
 * Focus all inputs on the specified client, meaning the keyboard, 
 * pointer.
 * 
 * @param enter_x X coord where the pointer entered the client
 * @param enter_y Y coord where the pointer entered the client
 * @param client The client that should receive focus.
 * 
 * @returns 0 on success or a negative value on failure.
 */
static int wh_input_focus_all_inputs_on_client(
    double enter_x, double enter_y, const WhaleClient* client
)
{
    WhaleCompositor* comp = client->comp;
    struct wlr_seat* seat = comp->seat;
    struct wlr_surface* surf = client->xdg_toplevel->base->surface;

    struct wlr_keyboard* keyboard =
        &comp->keyboard_group.wlr_keyboard_group->keyboard;

    wlr_seat_keyboard_notify_enter(
        seat,
        surf,
        keyboard->keycodes,
        keyboard->num_keycodes,
        &keyboard->modifiers
    );

    wlr_seat_pointer_notify_enter(seat, surf, enter_x, enter_y);

    return 0;
}

static int wh_input_unfocus_all_inputs(const WhaleCompositor* comp)
{
    struct wlr_seat* seat = comp->seat;
    if (seat->keyboard_state.focused_surface)
    {
        wlr_seat_keyboard_notify_clear_focus(seat);
        wlr_seat_pointer_notify_clear_focus(seat);
    }

    return 0;
}

static void on_cursor_motion_absolute(struct wl_listener* listener, void* data)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.cursor_motion_absolute);

    struct wlr_pointer_motion_absolute_event* ev = data;

    double x;
    double y;
    wlr_cursor_absolute_to_layout_coords(
        comp->cursor, &ev->pointer->base, ev->x, ev->y, &x, &y
    );

    /* Get the top-most node over which our cursor is currently hovering. */
    WhaleClient* hovered_client = wh_client_get_at_coords(x, y, comp);
    if (!hovered_client)
    {
        /* This needs to be re-set every time in order to show up on screen
        (?)
         */
        wlr_cursor_set_xcursor(comp->cursor, comp->cursor_manager, "default");
        wh_input_unfocus_all_inputs(comp);
        return;
    }

    double surf_x = x - hovered_client->scene_tree->node.x;
    double surf_y = y - hovered_client->scene_tree->node.y;

    if (!wh_input_is_client_focused(hovered_client))
        wh_input_focus_all_inputs_on_client(surf_x, surf_y, hovered_client);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    u32 now_ms = now.tv_sec * 1000 + now.tv_nsec / 1000000;
    wlr_seat_pointer_notify_motion(comp->seat, now_ms, surf_x, surf_y);
}

static void on_cursor_button(struct wl_listener* listener, void* data)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.cursor_button);
    struct wlr_pointer_button_event* ev = data;

    wlr_seat_pointer_notify_button(
        comp->seat, ev->time_msec, ev->button, ev->state
    );
}

static void on_cursor_axis(struct wl_listener* listener, void* data)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.cursor_axis);

    struct wlr_pointer_axis_event* ev = data;

    wlr_seat_pointer_notify_axis(
        comp->seat,
        ev->time_msec,
        ev->orientation,
        ev->delta,
        ev->delta_discrete,
        ev->source,
        ev->relative_direction
    );
}

static void on_cursor_frame(struct wl_listener* listener, void*)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.cursor_frame);

    /* Notify the focused client. */
    wlr_seat_pointer_notify_frame(comp->seat);
}

static int wh_input_cursor_init(WhaleCompositor* comp)
{
    comp->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(comp->cursor, comp->output_layout);

    comp->cursor_manager = wlr_xcursor_manager_create(NULL, 24);
    setenv("XCURSOR_SIZE", "24", 1);

    LISTEN(
        &comp->cursor->events.motion,
        &comp->listeners.cursor_motion,
        on_cursor_motion
    );

    LISTEN(
        &comp->cursor->events.motion_absolute,
        &comp->listeners.cursor_motion_absolute,
        on_cursor_motion_absolute
    );

    LISTEN(
        &comp->cursor->events.button,
        &comp->listeners.cursor_button,
        on_cursor_button
    );

    LISTEN(
        &comp->cursor->events.axis, &comp->listeners.cursor_axis, on_cursor_axis
    );

    /* A "frame" is a logical grouping of related events that should be
    processed atomically. Frame events are sent after one or more pointer
    events and signal that those events can be processed. */
    LISTEN(
        &comp->cursor->events.frame,
        &comp->listeners.cursor_frame,
        on_cursor_frame
    );

    // wlr_cursor_warp_closest(
    //     comp->cursor, NULL, comp->cursor->x, comp->cursor->y
    // );
    wlr_cursor_set_xcursor(comp->cursor, comp->cursor_manager, "default");

    return 0;
}

static int
wl_input_pointer_init(struct wlr_pointer* pointer, WhaleCompositor* comp)
{
    wlr_cursor_attach_input_device(comp->cursor, &pointer->base);
    return 0;
}

static int
wl_input_keyboard_init(struct wlr_keyboard* keyboard, WhaleCompositor* comp)
{
    wlr_keyboard_set_keymap(
        keyboard, comp->keyboard_group.wlr_keyboard_group->keyboard.keymap
    );

    wlr_keyboard_group_add_keyboard(
        comp->keyboard_group.wlr_keyboard_group, keyboard
    );
    return 0;
}

static void on_new_input(struct wl_listener* listener, void* data)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.new_input);

    struct wlr_input_device* dev = data;
    wh_log(DEBUG, "input: new input (%s)", dev->name);

    u32 seat_caps = comp->seat->capabilities;

    switch (dev->type)
    {
    case WLR_INPUT_DEVICE_POINTER:
        struct wlr_pointer* ptr = wlr_pointer_from_input_device(dev);
        if (wl_input_pointer_init(ptr, comp) == 0)
            seat_caps |= WL_SEAT_CAPABILITY_POINTER;

        break;

    case WLR_INPUT_DEVICE_KEYBOARD:
        struct wlr_keyboard* keyboard = wlr_keyboard_from_input_device(dev);
        if (wl_input_keyboard_init(keyboard, comp) == 0)
            seat_caps |= WL_SEAT_CAPABILITY_KEYBOARD;

        break;

    default:
        wh_log(WARN, "input: Unhandled device (%s)", dev->name);
        break;
    }

    wlr_seat_set_capabilities(comp->seat, seat_caps);
}

static int wh_input_devices_init(WhaleCompositor* comp)
{
    LISTEN(
        &comp->backend->events.new_input,
        &comp->listeners.new_input,
        on_new_input
    );

    return 0;
}

static const struct xkb_rule_names xkb_rules = {
    /* can specify fields: rules, model, layout, variant, options */
    /* example:
    .options = "ctrl:nocaps",
    */
    .options = NULL,
};

static void on_keyboard_key(struct wl_listener* listener, void* data)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.keyboard_key);
    struct wlr_keyboard_key_event* ev = data;

    wlr_seat_keyboard_notify_key(
        comp->seat, ev->time_msec, ev->keycode, ev->state
    );
}

int keyrepeat(void* data)
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

static void on_keyboard_modifier(struct wl_listener* listener, void*)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.keyboard_modifier);

    wlr_seat_keyboard_notify_modifiers(
        comp->seat, &comp->keyboard_group.wlr_keyboard_group->keyboard.modifiers
    );
}

static int wh_input_keyboard_group_init(WhaleCompositor* comp)
{
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
        xkb_context, &xkb_rules, XKB_KEYMAP_COMPILE_NO_FLAGS
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

static void on_request_set_cursor(struct wl_listener* listener, void* data)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.seat_request_set_cursor);

    struct wlr_seat_pointer_request_set_cursor_event* ev = data;

    if (comp->seat->pointer_state.focused_client == ev->seat_client)
    {
        wlr_cursor_set_surface(
            comp->cursor, ev->surface, ev->hotspot_x, ev->hotspot_y
        );
    }
}

int wh_input_init(WhaleCompositor* comp)
{
    comp->seat = wlr_seat_create(comp->display, "seat_0");

    LISTEN(
        &comp->seat->events.request_set_cursor,
        &comp->listeners.seat_request_set_cursor,
        on_request_set_cursor
    );

    int st = wh_input_cursor_init(comp);
    if (st < 0)
        return st;

    st = wh_input_keyboard_group_init(comp);
    if (st < 0)
        return st;

    st = wh_input_devices_init(comp);
    if (st < 0)
        return st;

    return 0;
}
