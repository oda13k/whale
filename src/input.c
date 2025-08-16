/**
 * Copyright Olaru Alexandru.
 * Distributed under the MIT license.
 *
 * This file implements most of the input subsystem in whale. It deals with
 * client focus behaviour and input enumeration.
 */

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <whale/client/client.h>
#include <whale/input.h>
#include <whale/input/keyboard.h>
#include <whale/log.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>

typedef enum
{
    WHALE_POINTER_MODE_PASSTHROUGH,
    WHALE_POINTER_MODE_INTERACTIVE_MOVE,
    WHALE_POINTER_MODE_INTERACTIVE_RESIZE,
} WhalePointerMode;

static WhaleCompositor* g_comp;

static WhaleSurface* g_pointer_focus_surface;
static WhaleSurface* g_keyboard_focus_surface;

static bool g_config_focus_follows_pointer = true;
/* Always true if the focus doesn't follow the pointer */
static bool g_config_raise_to_top_on_click = true;

static struct
{
    WhalePointerMode mode;
    WhalePosition2D cursor_start_pos;
    WhalePosition2D client_start_pos;
    u32 resize_edge;
} g_pointer_mode;

#define POINTER_IS_INTERACTIVE()                                               \
    (g_pointer_mode.mode != WHALE_POINTER_MODE_PASSTHROUGH)

static s32 s32_3max(s32 a, s32 b, s32 c)
{
    s32 ret = a;

    if (b > ret)
        ret = b;

    if (c > ret)
        ret = c;

    return ret;
}

static void input_focus_pointer_on_surface(
    const WhalePosition2D* enter_coords, WhaleSurface* surface
)
{
    if (surface == g_pointer_focus_surface)
        return;

    wlr_seat_pointer_notify_enter(
        g_comp->seat, surface->wlr_surface, enter_coords->x, enter_coords->y
    );

    g_pointer_focus_surface = surface;
}

static void input_focus_keyboard_on_surface(WhaleSurface* surface)
{
    if (surface == g_keyboard_focus_surface)
        return;

    WhaleClient* new_client = wh_client_from_surface(surface);
    WhaleClient* old_client =
        g_keyboard_focus_surface
            ? wh_client_from_surface(g_keyboard_focus_surface)
            : nullptr;

    /* Toplevel client activation follows the keyboard focus */
    if (new_client != old_client)
    {
        if (old_client)
            wh_client_set_active(false, old_client);

        wh_client_set_active(true, new_client);
    }

    struct wlr_keyboard* keyboard =
        &g_comp->keyboard_group.wlr_keyboard_group->keyboard;

    switch (surface->type)
    {
    case SURFACE_TYPE_CLIENT:
        wlr_seat_keyboard_notify_enter(
            g_comp->seat,
            surface->wlr_surface,
            keyboard->keycodes,
            keyboard->num_keycodes,
            &keyboard->modifiers
        );
        g_keyboard_focus_surface = surface;
        break;

    case SURFACE_TYPE_POPUP:
    case SURFACE_TYPE_SUBSURFACE:
        /* If we are a popup or subsurface we want to focus the keyboard
        on the parent toplevel client as per spec. */
        wlr_seat_keyboard_notify_enter(
            g_comp->seat,
            new_client->surface->wlr_surface,
            keyboard->keycodes,
            keyboard->num_keycodes,
            &keyboard->modifiers
        );
        g_keyboard_focus_surface = new_client->surface;
        break;

    default:
        unreachable();
    }
}

static void input_unfocus_pointer_unchecked()
{
    wlr_seat_pointer_notify_clear_focus(g_comp->seat);
    g_pointer_focus_surface = nullptr;
}

static void input_unfocus_keyboard_unchecked()
{
    WhaleClient* client = wh_client_from_surface(g_keyboard_focus_surface);
    wh_client_set_active(false, client);
    wlr_seat_keyboard_notify_clear_focus(g_comp->seat);
    g_pointer_mode.mode = WHALE_POINTER_MODE_PASSTHROUGH;
    g_keyboard_focus_surface = nullptr;
}

static void handle_interactive_pointer_motion()
{
    WhaleClient* client = wh_client_from_surface(g_keyboard_focus_surface);

    if (g_pointer_mode.mode == WHALE_POINTER_MODE_INTERACTIVE_RESIZE)
    {
        WhalePosition2D cursor_pos = wh_input_get_cursor_pos();
        WhalePosition2D client_pos;
        wh_client_get_pos(&client_pos, client);

        WhaleSize2D client_size;
        client->surface->driver.get_size(&client_size, client->surface);

        WhaleSize2D min_client_size;
        client->surface->driver.get_minmax_size(
            &min_client_size, nullptr, client->surface
        );

        bool needs_move =
            g_pointer_mode.resize_edge != WLR_EDGE_RIGHT &&
            g_pointer_mode.resize_edge != WLR_EDGE_BOTTOM &&
            g_pointer_mode.resize_edge != (WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM);

        bool horizontal_edge = g_pointer_mode.resize_edge == WLR_EDGE_RIGHT ||
                               g_pointer_mode.resize_edge == WLR_EDGE_LEFT;

        bool vertical_edge = g_pointer_mode.resize_edge == WLR_EDGE_TOP ||
                             g_pointer_mode.resize_edge == WLR_EDGE_BOTTOM;

        WhaleSize2D new_size = {
            .w = vertical_edge
                     ? client_size.w
                     : (wh_dim_t)s32_3max(
                           cursor_pos.x - client_pos.x, min_client_size.w, 0
                       ),
            .h = horizontal_edge
                     ? client_size.h
                     : (wh_dim_t)s32_3max(
                           cursor_pos.y - client_pos.y, min_client_size.h, 0
                       )
        };

        client->surface->driver.set_size(&new_size, client->surface);
    }
    else if (g_pointer_mode.mode == WHALE_POINTER_MODE_INTERACTIVE_MOVE)
    {
        WhalePosition2D cursor_pos = wh_input_get_cursor_pos();
        wh_coord_t dx = cursor_pos.x - g_pointer_mode.cursor_start_pos.x;
        wh_coord_t dy = cursor_pos.y - g_pointer_mode.cursor_start_pos.y;

        WhalePosition2D new_pos = {
            .x = g_pointer_mode.client_start_pos.x + dx,
            .y = g_pointer_mode.client_start_pos.y + dy
        };

        wh_client_set_pos(&new_pos, client);
    }
}

static void handle_pointer_motion()
{
    if (POINTER_IS_INTERACTIVE())
    {
        handle_interactive_pointer_motion();
        return;
    }

    wh_input_refocus(false);

    if (g_pointer_focus_surface)
    {
        WhalePosition2D cursor_pos = wh_input_get_cursor_pos();
        WhalePosition2D surface_coords;
        wh_surface_layout_to_surface_coords(
            g_pointer_focus_surface, &cursor_pos, &surface_coords
        );

        wlr_seat_pointer_notify_motion(
            g_comp->seat,
            wh_time_monotonic_now_ms(),
            surface_coords.x,
            surface_coords.y
        );
    }
}

static void on_cursor_motion(struct wl_listener*, void* data)
{
    struct wlr_pointer_motion_event* ev = data;

    /* Move the cursor image to the same position */
    wlr_cursor_move(
        g_comp->cursor, &ev->pointer->base, ev->delta_x, ev->delta_y
    );

    handle_pointer_motion();
}

static void on_cursor_motion_absolute(struct wl_listener*, void* data)
{
    struct wlr_pointer_motion_absolute_event* ev = data;

    /* Move the cursor image to the same position */
    wlr_cursor_warp_absolute(g_comp->cursor, &ev->pointer->base, ev->x, ev->y);

    handle_pointer_motion();
}

static void on_cursor_button(struct wl_listener*, void* data)
{
    struct wlr_pointer_button_event* ev = data;

    if (POINTER_IS_INTERACTIVE() && ev->button == BTN_LEFT &&
        ev->state == WL_POINTER_BUTTON_STATE_RELEASED)
    {
        /* I guess the client *could* request a resize/move in some other way
        that is not a left-button click in which case this is bad UX... oh well.
        */
        g_pointer_mode.mode = WHALE_POINTER_MODE_PASSTHROUGH;
    }

    /* If the focus doesn't follow the cursor, we focus clients by clicking on
     * them */
    bool click = (ev->button == BTN_LEFT || ev->button == BTN_RIGHT ||
                  ev->button == BTN_MIDDLE) &&
                 ev->state == WL_POINTER_BUTTON_STATE_PRESSED;

    if (click)
    {
        WhalePosition2D cursor_pos = wh_input_get_cursor_pos();
        WhaleSurface* surface = wh_surface_get_topmost_at(&cursor_pos);

        if (!g_config_focus_follows_pointer &&
            surface != g_keyboard_focus_surface)
        {
            if (surface)
                input_focus_keyboard_on_surface(surface);
            else
                input_unfocus_keyboard_unchecked();
        }

        if (surface &&
            (!g_config_focus_follows_pointer || g_config_raise_to_top_on_click))
        {
            WhaleClient* client = wh_client_from_surface(surface);
            wlr_scene_node_raise_to_top(&client->scene_tree->node);
        }
    }

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

static int input_cursor_init(WhaleCompositor* comp)
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
input_pointer_init(struct wlr_pointer* pointer, WhaleCompositor* comp)
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
        if (input_pointer_init(ptr, comp) == 0)
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

    int st = input_cursor_init(comp);
    if (st < 0)
        return st;

    st = wh_input_keyboard_ss_init(comp);
    if (st < 0)
        return st;

    LISTEN(
        &comp->backend->events.new_input,
        &comp->listeners.new_input,
        on_new_input
    );

    return 0;
}

WhaleSurface* wh_input_refocus(bool force_keyboard)
{
    WhalePosition2D pointer_pos = wh_input_get_cursor_pos();
    WhaleSurface* surface = wh_surface_get_topmost_at(&pointer_pos);
    if (!surface)
    {
        if (g_pointer_focus_surface)
        {
            input_unfocus_pointer_unchecked();
            wlr_cursor_set_xcursor(
                g_comp->cursor, g_comp->cursor_manager, "default"
            );
        }

        if (g_keyboard_focus_surface &&
            (g_config_focus_follows_pointer || force_keyboard))
            input_unfocus_keyboard_unchecked();

        return nullptr;
    }

    WhalePosition2D surface_coords;
    wh_surface_layout_to_surface_coords(surface, &pointer_pos, &surface_coords);

    input_focus_pointer_on_surface(&surface_coords, surface);
    if (g_config_focus_follows_pointer || force_keyboard)
        input_focus_keyboard_on_surface(surface);

    return surface;
}

WhalePosition2D wh_input_get_cursor_pos()
{
    return (WhalePosition2D){.x = g_comp->cursor->x, .y = g_comp->cursor->y};
}

void wh_input_start_interactive_move(WhaleSurface* surface)
{
    /* Only the keyboard focused (active) surface can request a
    pointer mode. */
    if (surface != g_keyboard_focus_surface || POINTER_IS_INTERACTIVE())
        return;

    g_pointer_mode.mode = WHALE_POINTER_MODE_INTERACTIVE_MOVE;
    g_pointer_mode.cursor_start_pos = wh_input_get_cursor_pos();
    wh_client_get_pos(
        &g_pointer_mode.client_start_pos, wh_client_from_surface(surface)
    );
}

void wh_input_start_interactive_resize(u32 edge, WhaleSurface* surface)
{
    if (surface != g_keyboard_focus_surface || POINTER_IS_INTERACTIVE())
        return;

    g_pointer_mode.mode = WHALE_POINTER_MODE_INTERACTIVE_RESIZE;
    g_pointer_mode.cursor_start_pos = wh_input_get_cursor_pos();
    wh_client_get_pos(
        &g_pointer_mode.client_start_pos, wh_client_from_surface(surface)
    );
    g_pointer_mode.resize_edge = edge;
}