#include <stdlib.h>
#include <time.h>
#include <whale/client/client.h>
#include <whale/input.h>
#include <whale/input/keyboard.h>
#include <whale/log.h>
#include <whale/utils.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>

static WhaleCompositor* g_comp;

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

    /* Set the cursor to the same position */
    wlr_cursor_warp(comp->cursor, &ev->pointer->base, x, y);

    double surf_x, surf_y;
    /* The focus follows the cursor. */
    int st = wh_input_refocus_all_inputs_on_topmost_client_under_cursor(
        comp, &surf_x, &surf_y
    );

    if (st == 0)
    {
        wlr_seat_pointer_notify_motion(
            comp->seat, wh_time_monotonic_now_ms(), surf_x, surf_y
        );
    }
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

    wlr_cursor_warp_closest(
        comp->cursor, NULL, comp->cursor->x, comp->cursor->y
    );
    wlr_cursor_set_xcursor(comp->cursor, comp->cursor_manager, "default");

    return 0;
}

static int
wh_input_pointer_init(struct wlr_pointer* pointer, WhaleCompositor* comp)
{
    wlr_cursor_attach_input_device(comp->cursor, &pointer->base);
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
        if (wh_input_pointer_init(ptr, comp) == 0)
            seat_caps |= WL_SEAT_CAPABILITY_POINTER;

        break;
        
    case WLR_INPUT_DEVICE_KEYBOARD:
        struct wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(dev);
        if (wh_input_keyboard_add(wlr_keyboard, comp) == 0)
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
    g_comp = comp;
    comp->seat = wlr_seat_create(comp->display, "seat_0");

    LISTEN(
        &comp->seat->events.request_set_cursor,
        &comp->listeners.seat_request_set_cursor,
        on_request_set_cursor
    );

    int st = wh_input_cursor_init(comp);
    if (st < 0)
        return st;

    st = wh_input_keyboard_ss_init(comp);
    if (st < 0)
        return st;

    st = wh_input_devices_init(comp);
    if (st < 0)
        return st;

    return 0;
}

int wh_input_refocus_all_inputs_on_topmost_client_under_cursor(
    WhaleCompositor* comp, double* focused_surface_x, double* focused_surface_y
)
{
    struct wlr_cursor* curs = comp->cursor;
    /* Get the top-most node over which our cursor is currently hovering. */
    WhaleClient* hovered_client =
        wh_client_get_at_coords(curs->x, curs->y, comp);

    if (!hovered_client)
    {
        /* This needs to be re-set every time in order to show up on screen
        (?)
         */
        wlr_cursor_set_xcursor(comp->cursor, comp->cursor_manager, "default");
        wh_input_unfocus_all_inputs(comp);
        return 1;
    }

    double surf_x = curs->x - (hovered_client->scene_tree->node.x -
                               hovered_client->xdg_toplevel->base->geometry.x);
    double surf_y = curs->y - (hovered_client->scene_tree->node.y -
                               hovered_client->xdg_toplevel->base->geometry.y);

    if (focused_surface_x)
        *focused_surface_x = surf_x;

    if (focused_surface_y)
        *focused_surface_y = surf_y;

    if (!wh_input_is_client_focused(hovered_client))
        wh_input_focus_all_inputs_on_client(surf_x, surf_y, hovered_client);

    return 0;
}

int wh_input_focus_on_client(WhaleClient* client)
{
    if (!wh_input_is_client_focused(client))
        wh_input_focus_all_inputs_on_client(0, 0, client);

    return 0;
}

WhaleClient* wh_input_get_focused_client()
{
    if (g_comp->seat->keyboard_state.focused_surface)
        return wh_client_from_wlr_surface(
            g_comp->seat->keyboard_state.focused_surface
        );

    return nullptr;
}
