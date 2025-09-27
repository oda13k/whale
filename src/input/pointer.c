
#include <linux/input-event-codes.h>
#include <whale/client/client.h>
#include <whale/input/keyboard.h>
#include <whale/input/pointer.h>
#include <whale/input/seat.h>
#include <whale/log.h>
#include <whale/output/output.h>
#include <whale/output/scene.h>
#include <whale/types.h>
#include <whale/utils/proc.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_xcursor_manager.h>

#define POINTER_IS_INTERACTIVE()                                               \
    (g_pointer.mode != WHALE_POINTER_MODE_PASSTHROUGH)

typedef enum
{
    WHALE_POINTER_MODE_PASSTHROUGH,
    WHALE_POINTER_MODE_INTERACTIVE_MOVE,
    WHALE_POINTER_MODE_INTERACTIVE_RESIZE,
} WhalePointerMode;

typedef struct
{
    WhaleSurface* focused_surface;
    WhalePointerMode mode;
    struct wlr_cursor* wlr_cursor;
    struct
    {
        WhalePosition2D pointer_start_pos;
        WhalePosition2D client_start_pos;
        u32 resize_edge;
    } interactive;

    struct wlr_scene_tree* drag_icon_tree;

} WhalePointer;

static struct wlr_seat* g_seat;
static struct wlr_xcursor_manager* g_xcursor_manager;
static WhalePointer g_pointer;

static bool g_config_focus_follows_pointer = true;

static u32 u32_2max(u32 a, u32 b)
{
    if (a > b)
        return a;
    else
        return b;
}

static void pointer_focus_surface(
    const WhalePosition2D* enter_coords, WhaleSurface* surface
)
{
    wlr_seat_pointer_notify_enter(
        g_seat, surface->wlr_surface, enter_coords->x, enter_coords->y
    );

    g_pointer.focused_surface = surface;
}

static void pointer_unfocus_unchecked()
{
    wlr_seat_pointer_notify_clear_focus(g_seat);
    g_pointer.focused_surface = nullptr;
}

static void handle_interactive_pointer_motion()
{
    WhaleClient* client = wh_client_from_surface(g_pointer.focused_surface);

    if (g_pointer.mode == WHALE_POINTER_MODE_INTERACTIVE_RESIZE)
    {
        WhalePosition2D cursor_pos;
        wh_pointer_get_pos(&cursor_pos);

        WhaleGeometry2D client_geom;
        wh_client_get_geometry(&client_geom, client);

        WhaleSize2D min_client_size;
        client->surface->driver.get_minmax_size(
            &min_client_size, nullptr, client->surface
        );

        // bool needs_move =
        //     g_pointer.interactive.resize_edge != WLR_EDGE_RIGHT &&
        //     g_pointer.interactive.resize_edge != WLR_EDGE_BOTTOM &&
        //     g_pointer.interactive.resize_edge != (WLR_EDGE_RIGHT |
        //     WLR_EDGE_BOTTOM);

        bool horizontal_edge =
            g_pointer.interactive.resize_edge == WLR_EDGE_RIGHT ||
            g_pointer.interactive.resize_edge == WLR_EDGE_LEFT;

        bool vertical_edge =
            g_pointer.interactive.resize_edge == WLR_EDGE_TOP ||
            g_pointer.interactive.resize_edge == WLR_EDGE_BOTTOM;

        wh_coord_t dx = cursor_pos.x - client_geom.pos.x;
        wh_coord_t dy = cursor_pos.y - client_geom.pos.y;

        WhaleSize2D new_size = {
            .w = vertical_edge
                     ? client_geom.size.w
                     : u32_2max(dx < 0 ? 0 : (u32)dx, min_client_size.w),
            .h = horizontal_edge
                     ? client_geom.size.h
                     : u32_2max(dy < 0 ? 0 : (u32)dy, min_client_size.h)
        };

        client->surface->driver.set_size(&new_size, client->surface);
    }
    else if (g_pointer.mode == WHALE_POINTER_MODE_INTERACTIVE_MOVE)
    {
        WhalePosition2D cursor_pos;
        wh_pointer_get_pos(&cursor_pos);

        wh_coord_t dx =
            cursor_pos.x - g_pointer.interactive.pointer_start_pos.x;
        wh_coord_t dy =
            cursor_pos.y - g_pointer.interactive.pointer_start_pos.y;

        WhalePosition2D new_pos = {
            .x = g_pointer.interactive.client_start_pos.x + dx,
            .y = g_pointer.interactive.client_start_pos.y + dy
        };

        WhaleOutput* output = wh_output_get_at(&cursor_pos);
        if (output && output != client->workspace->parent_output)
        {
            WhaleWorkspace* old_ws = wh_workspace_unbind_client(client);
            WhaleWorkspace* new_ws = output->active_workspace;
            wh_workspace_bind_client(client, new_ws);

            if (client->prev_layer == WH_LAYER_TILING)
            {
                wh_workspace_arrange(old_ws);
                wh_workspace_arrange(new_ws);
            }
        }

        wh_client_set_pos(&new_pos, client);
    }
}

static void handle_interactive_pointer_button(
    u32 button, enum wl_pointer_button_state state
)
{
    /* I guess the client *could* request a resize/move in some other way
    that is not a left-button click in which case this is bad UX... oh well.
    */
    bool released_left_button =
        button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_RELEASED;

    if (released_left_button)
        wh_pointer_drop_interactive();
}

static void handle_pointer_motion(u32 ev_time_ms)
{
    WhalePosition2D cursor_pos;
    wh_pointer_get_pos(&cursor_pos);

    if (g_pointer.drag_icon_tree)
    {
        wlr_scene_node_set_position(
            &g_pointer.drag_icon_tree->node,
            CAST_DBL_TO_INT(cursor_pos.x),
            CAST_DBL_TO_INT(cursor_pos.y)
        );
    }

    WhaleSurface* surf =
        wh_pointer_update_focus(g_config_focus_follows_pointer);
    if (!surf)
        return;

    WhalePosition2D surface_coords;
    wh_surface_layout_to_surface_coords(surf, &cursor_pos, &surface_coords);

    wlr_seat_pointer_notify_motion(
        g_seat, ev_time_ms, surface_coords.x, surface_coords.y
    );
}

static void handle_pointer_button(
    u32 button, enum wl_pointer_button_state state, u32 time_ms
)
{
    if (state != WL_POINTER_BUTTON_STATE_PRESSED)
        goto notify;

    if (wh_keyboard_is_modifier_active(WH_KEYBOARD_MOD_NORMAL) &&
        button == BTN_LEFT && g_pointer.focused_surface)
    {
        wh_pointer_start_interactive_move(g_pointer.focused_surface);
        return;
    }
    else if (button == BTN_LEFT || button == BTN_RIGHT || button == BTN_MIDDLE)
    {
        WhalePosition2D cursor_pos;
        wh_pointer_get_pos(&cursor_pos);

        WhaleSurface* surface = wh_scene_get_topmost_surface_at(&cursor_pos);

        if (surface)
        {
            WhaleClient* client = wh_client_from_surface(surface);
            if (client->layer == WH_LAYER_FLOATING)
                wh_client_raise_to_top(client);

            if (!g_config_focus_follows_pointer)
                wh_keyboard_focus_surface(surface);
        }
        else if (wh_keyboard_get_focused_surface())
        {
            wh_keyboard_unfocus_unchecked();
        }
    }

notify:
    wlr_seat_pointer_notify_button(g_seat, time_ms, button, state);
}

WH_CALLBACK(pointer_motion_absolute, struct wl_listener*, void* data)
{
    struct wlr_pointer_motion_absolute_event* ev = data;

    wlr_cursor_warp_absolute(
        g_pointer.wlr_cursor, &ev->pointer->base, ev->x, ev->y
    );

    if (UNLIKELY(POINTER_IS_INTERACTIVE()))
        handle_interactive_pointer_motion();
    else
        handle_pointer_motion(ev->time_msec);
}

WH_CALLBACK(pointer_motion, struct wl_listener*, void* data)
{
    struct wlr_pointer_motion_event* ev = data;

    wlr_cursor_move(
        g_pointer.wlr_cursor, &ev->pointer->base, ev->delta_x, ev->delta_y
    );

    if (UNLIKELY(POINTER_IS_INTERACTIVE()))
        handle_interactive_pointer_motion();
    else
        handle_pointer_motion(ev->time_msec);
}

WH_CALLBACK(pointer_button, struct wl_listener*, void* data)
{
    struct wlr_pointer_button_event* ev = data;

    if (UNLIKELY(POINTER_IS_INTERACTIVE()))
        handle_interactive_pointer_button(ev->button, ev->state);
    else
        handle_pointer_button(ev->button, ev->state, ev->time_msec);
}

WH_CALLBACK(pointer_axis, struct wl_listener*, void* data)
{
    struct wlr_pointer_axis_event* ev = data;

    wlr_seat_pointer_notify_axis(
        g_seat,
        ev->time_msec,
        ev->orientation,
        ev->delta,
        ev->delta_discrete,
        ev->source,
        ev->relative_direction
    );
}

WH_CALLBACK(pointer_frame, struct wl_listener*, void*)
{
    /* A "frame" is a logical grouping of related events that should be
    processed atomically. Frame events are sent after one or more pointer events
    and signal that those events can be processed. */
    wlr_seat_pointer_notify_frame(g_seat);
}

WH_CALLBACK(seat_request_set_cursor, struct wl_listener*, void* data)
{
    struct wlr_seat_pointer_request_set_cursor_event* ev = data;

    if (ev->seat_client != g_seat->pointer_state.focused_client ||
        POINTER_IS_INTERACTIVE())
        return;

    wlr_cursor_set_surface(
        g_pointer.wlr_cursor, ev->surface, ev->hotspot_x, ev->hotspot_y
    );
}

WH_CALLBACK(seat_request_start_drag, struct wl_listener*, void* data)
{
    struct wlr_seat_request_start_drag_event* ev = data;

    if (wlr_seat_validate_pointer_grab_serial(g_seat, ev->origin, ev->serial))
        wlr_seat_start_pointer_drag(g_seat, ev->drag, ev->serial);
    else
        wlr_data_source_destroy(ev->drag->source);
}

WH_CALLBACK(seat_destroy_drag, struct wl_listener*, void*)
{
    wh_pointer_update_focus(false);

    wlr_scene_node_destroy(&g_pointer.drag_icon_tree->node);
    g_pointer.drag_icon_tree = nullptr;

    WH_UNLISTEN(seat_destroy_drag);
}

WH_CALLBACK(seat_start_drag, struct wl_listener*, void* data)
{
    struct wlr_drag* drag = data;
    if (!drag->icon)
        return;

    if (g_pointer.drag_icon_tree)
        wh_log(WARN, "Started a drag, but the previous one hasn't been freed.");

    wh_pointer_update_focus(false);

    g_pointer.drag_icon_tree = wh_scene_attach_drag_icon(drag->icon);

    WH_LISTEN(&drag->events.destroy, seat_destroy_drag);
}

int wh_pointer_init(struct wlr_seat* seat)
{
    g_seat = seat;

    g_pointer.wlr_cursor = wlr_cursor_create();
    if (!g_pointer.wlr_cursor)
    {
        wh_log(ERR, "pointer: Failed to create pointer.");
        return -1;
    }

    wh_output_layout_attach_pointer(g_pointer.wlr_cursor);

    g_xcursor_manager = wlr_xcursor_manager_create(NULL, 24);
    if (!g_xcursor_manager)
    {
        wh_log(ERR, "pointer: Failed to create xcursor manager.");
        return -1;
    }

    setenv("XCURSOR_SIZE", "24", 1);

    WH_LISTEN(&g_pointer.wlr_cursor->events.motion, pointer_motion);
    WH_LISTEN(
        &g_pointer.wlr_cursor->events.motion_absolute, pointer_motion_absolute
    );
    WH_LISTEN(&g_pointer.wlr_cursor->events.button, pointer_button);
    WH_LISTEN(&g_pointer.wlr_cursor->events.axis, pointer_axis);
    WH_LISTEN(&g_pointer.wlr_cursor->events.frame, pointer_frame);

    WH_LISTEN(&seat->events.request_set_cursor, seat_request_set_cursor);
    WH_LISTEN(&seat->events.request_start_drag, seat_request_start_drag);
    WH_LISTEN(&seat->events.start_drag, seat_start_drag);

    wlr_cursor_warp_closest(g_pointer.wlr_cursor, NULL, 0, 0);

    wh_pointer_set_texture("default");

    return 0;
}

void wh_pointer_destroy()
{
    WH_UNLISTEN(seat_start_drag);
    WH_UNLISTEN(seat_request_start_drag);
    WH_UNLISTEN(seat_request_set_cursor);
    WH_UNLISTEN(pointer_frame);
    WH_UNLISTEN(pointer_axis);
    WH_UNLISTEN(pointer_button);
    WH_UNLISTEN(pointer_motion_absolute);
    WH_UNLISTEN(pointer_motion);

    // TODO MAYBE: reset XCURSOR_SIZE env if it was already set before we
    // started?

    wlr_xcursor_manager_destroy(g_xcursor_manager);
    g_xcursor_manager = nullptr;

    wlr_cursor_destroy(g_pointer.wlr_cursor);
    g_pointer.wlr_cursor = nullptr;

    g_seat = nullptr;
}

int wh_pointer_attach_device(struct wlr_pointer* pointer)
{
    wlr_cursor_attach_input_device(g_pointer.wlr_cursor, &pointer->base);
    return 0;
}

WhaleSurface* wh_pointer_update_focus(bool allow_keyboard)
{
    WhalePosition2D pointer_pos;
    wh_pointer_get_pos(&pointer_pos);

    bool focus_keyboard = g_config_focus_follows_pointer && allow_keyboard;

    WhaleSurface* surface = wh_scene_get_topmost_surface_at(&pointer_pos);
    if (!surface)
    {
        if (g_pointer.focused_surface)
        {
            pointer_unfocus_unchecked();
            wh_pointer_set_texture("default");
        }

        if (focus_keyboard && wh_keyboard_get_focused_surface())
            wh_keyboard_unfocus_unchecked();

        return nullptr;
    }

    WhalePosition2D surface_coords;
    wh_surface_layout_to_surface_coords(surface, &pointer_pos, &surface_coords);

    pointer_focus_surface(&surface_coords, surface);

    if (focus_keyboard)
        wh_keyboard_focus_surface(surface);

    return surface;
}

void wh_pointer_start_interactive_move(WhaleSurface* surface)
{
    /* Only the keyboard focused (active) surface can request a
    pointer mode. */
    if (surface != wh_keyboard_get_focused_surface() ||
        surface != g_pointer.focused_surface || POINTER_IS_INTERACTIVE())
        return;

    g_pointer.mode = WHALE_POINTER_MODE_INTERACTIVE_MOVE;
    wh_pointer_set_texture("fleur");
    wh_pointer_get_pos(&g_pointer.interactive.pointer_start_pos);

    /* The client will be floating while being moved. It's previous layer will
     * be restored once the move is over. */
    WhaleClient* client = wh_client_from_surface(surface);
    client->is_being_moved_interactively = true;
    wh_client_set_layer(WH_LAYER_FLOATING, client);

    WhaleGeometry2D client_geom;
    wh_client_get_geometry(&client_geom, client);
    g_pointer.interactive.client_start_pos = client_geom.pos;
}

void wh_pointer_start_interactive_resize(u32 edge, WhaleSurface* surface)
{
    if (surface != wh_keyboard_get_focused_surface() ||
        surface != g_pointer.focused_surface || POINTER_IS_INTERACTIVE())
        return;

    g_pointer.mode = WHALE_POINTER_MODE_INTERACTIVE_RESIZE;
    wh_pointer_get_pos(&g_pointer.interactive.pointer_start_pos);

    WhaleGeometry2D client_geom;
    wh_client_get_geometry(&client_geom, wh_client_from_surface(surface));
    g_pointer.interactive.client_start_pos = client_geom.pos;
    g_pointer.interactive.resize_edge = edge;
}

void wh_pointer_drop_interactive()
{
    if (!POINTER_IS_INTERACTIVE())
        return;

    g_pointer.mode = WHALE_POINTER_MODE_PASSTHROUGH;
    wh_pointer_set_texture("default");

    WhaleSurface* surface = g_pointer.focused_surface;
    if (!surface)
        return;

    WhaleClient* client = wh_client_from_surface(surface);
    client->is_being_moved_interactively = false;
    wh_client_restore_prev_layer(client);
    if (client->layer == WH_LAYER_TILING)
        wh_workspace_arrange(client->workspace);

    WhalePosition2D cursor_pos;
    wh_pointer_get_pos(&cursor_pos);

    WhalePosition2D surface_coords;
    wh_surface_layout_to_surface_coords(surface, &cursor_pos, &surface_coords);

    /* If we don't force a pointer refocus on the same surface firefox can't
     * be moved again until the surface is refocused by the user. ??? */
    pointer_unfocus_unchecked();
    pointer_focus_surface(&surface_coords, surface);
}

void wh_pointer_set_texture(const char* name)
{
    wlr_cursor_set_xcursor(g_pointer.wlr_cursor, g_xcursor_manager, name);
}

void wh_pointer_get_pos(WhalePosition2D* out_pos)
{
    out_pos->x = g_pointer.wlr_cursor->x;
    out_pos->y = g_pointer.wlr_cursor->y;
}
