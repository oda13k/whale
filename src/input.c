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

static WhaleSurface* g_focused_surface;

static bool wh_input_is_surface_focused(const WhaleSurface* surface)
{
    return g_focused_surface == surface;
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
static int
wh_input_focus_surface(double enter_x, double enter_y, WhaleSurface* surface)
{
    if (wh_input_is_surface_focused(surface))
        return 0;

    struct wlr_seat* seat = g_comp->seat;

    struct wlr_keyboard* keyboard =
        &g_comp->keyboard_group.wlr_keyboard_group->keyboard;

    wlr_seat_keyboard_notify_enter(
        seat,
        surface->wlr_surface,
        keyboard->keycodes,
        keyboard->num_keycodes,
        &keyboard->modifiers
    );
    wlr_seat_pointer_notify_enter(seat, surface->wlr_surface, enter_x, enter_y);
    g_focused_surface = surface;

    return 0;
}

static int wh_input_unfocus()
{
    if (g_focused_surface)
    {
        wlr_seat_pointer_notify_clear_focus(g_comp->seat);
        wlr_seat_keyboard_notify_clear_focus(g_comp->seat);
        g_focused_surface = nullptr;
    }

    return 0;
}

static void on_cursor_motion(struct wl_listener*, void* data)
{
    struct wlr_pointer_motion_event* ev = data;
    struct wlr_cursor* cursor = g_comp->cursor;

    /* Move the cursor to the same position */
    wlr_cursor_move(cursor, &ev->pointer->base, ev->delta_x, ev->delta_y);

    wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
    WhaleSurface* surface = wh_input_focus_surface_at_coords(&cursor_pos);
    if (surface)
    {
        wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
        wh_pos2d_t surface_coords;
        wh_surface_layout_to_surface_coords(
            surface, &cursor_pos, &surface_coords
        );

        wlr_seat_pointer_notify_motion(
            g_comp->seat,
            wh_time_monotonic_now_ms(),
            surface_coords.x,
            surface_coords.y
        );
    }
}

static void on_cursor_motion_absolute(struct wl_listener*, void* data)
{
    struct wlr_pointer_motion_absolute_event* ev = data;
    struct wlr_cursor* cursor = g_comp->cursor;

    /* Set the cursor to the same position */
    wlr_cursor_warp_absolute(cursor, &ev->pointer->base, ev->x, ev->y);

    wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
    WhaleSurface* surface = wh_input_focus_surface_at_coords(&cursor_pos);
    if (surface)
    {
        wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
        wh_pos2d_t surface_coords;
        wh_surface_layout_to_surface_coords(
            surface, &cursor_pos, &surface_coords
        );

        wlr_seat_pointer_notify_motion(
            g_comp->seat,
            wh_time_monotonic_now_ms(),
            surface_coords.x,
            surface_coords.y
        );
    }
}

static void on_cursor_button(struct wl_listener*, void* data)
{
    struct wlr_pointer_button_event* ev = data;

    wlr_seat_pointer_notify_button(
        g_comp->seat, ev->time_msec, ev->button, ev->state
    );
}

static void on_cursor_axis(struct wl_listener*, void* data)
{
    struct wlr_pointer_axis_event* ev = data;

    wlr_seat_pointer_notify_axis(
        g_comp->seat,
        ev->time_msec,
        ev->orientation,
        ev->delta,
        ev->delta_discrete,
        ev->source,
        ev->relative_direction
    );
}

static void on_cursor_frame(struct wl_listener*, void*)
{
    /* Notify the focused client. */
    wlr_seat_pointer_notify_frame(g_comp->seat);
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
    wh_log(INFO, "input: new input (%s)", dev->name);

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

WhaleSurface* wh_input_focus_surface_at_coords(const wh_pos2d_t* pos)
{
    /* Get the top-most node over which our cursor is currently hovering. */
    WhaleSurface* surface = wh_surface_get_at_coords(pos->x, pos->y);

    if (!surface)
    {
        /* This needs to be re-set every time in order to show up on screen
        (?)
         */
        wlr_cursor_set_xcursor(
            g_comp->cursor, g_comp->cursor_manager, "default"
        );
        wh_input_unfocus();
        return nullptr;
    }

    wh_input_focus_surface(pos->x, pos->y, surface);
    return surface;
}

WhaleSurface* wh_input_get_focused_surface()
{
    return g_focused_surface;
}

wh_pos2d_t wh_input_get_cursor_pos()
{
    return (wh_pos2d_t){.x = g_comp->cursor->x, .y = g_comp->cursor->y};
}
