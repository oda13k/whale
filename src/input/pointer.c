
#include <linux/input-event-codes.h>
#include <whale/client/client.h>
#include <whale/input/keyboard.h>
#include <whale/input/pointer.h>
#include <whale/input/seat.h>
#include <whale/log.h>
#include <whale/output/scene.h>
#include <whale/types.h>
#include <whale/utils/proc.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_xcursor_manager.h>

#define POINTER_IS_INTERACTIVE()                                               \
    (g_pointer_mode.mode != WHALE_POINTER_MODE_PASSTHROUGH)

typedef enum
{
    WHALE_POINTER_MODE_PASSTHROUGH,
    WHALE_POINTER_MODE_INTERACTIVE_MOVE,
    WHALE_POINTER_MODE_INTERACTIVE_RESIZE,
} WhalePointerMode;

static struct wlr_seat* g_seat;
static struct wlr_cursor* g_pointer;
static WhaleSurface* g_pointer_focus_surface;
static struct
{
    WhalePointerMode mode;
    WhalePosition2D cursor_start_pos;
    WhalePosition2D client_start_pos;
    u32 resize_edge;
} g_pointer_mode;

static struct wlr_xcursor_manager* g_xcursor_manager;

static bool g_config_focus_follows_pointer = true;

static u32 u32_2max(u32 a, u32 b)
{
    if (a > b)
        return a;
    else
        return b;
}

static void handle_interactive_pointer_motion()
{
    // CHECK: was g_keyboard_focus_surface, make sure it still works!!!
    WhaleClient* client = wh_client_from_surface(g_pointer_focus_surface);

    if (g_pointer_mode.mode == WHALE_POINTER_MODE_INTERACTIVE_RESIZE)
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
        //     g_pointer_mode.resize_edge != WLR_EDGE_RIGHT &&
        //     g_pointer_mode.resize_edge != WLR_EDGE_BOTTOM &&
        //     g_pointer_mode.resize_edge != (WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM);

        bool horizontal_edge = g_pointer_mode.resize_edge == WLR_EDGE_RIGHT ||
                               g_pointer_mode.resize_edge == WLR_EDGE_LEFT;

        bool vertical_edge = g_pointer_mode.resize_edge == WLR_EDGE_TOP ||
                             g_pointer_mode.resize_edge == WLR_EDGE_BOTTOM;

        s32 delta_x = cursor_pos.x - client_geom.pos.x;
        s32 delta_y = cursor_pos.y - client_geom.pos.y;

        WhaleSize2D new_size = {
            .w = vertical_edge
                     ? client_geom.size.w
                     : u32_2max(
                           delta_x < 0 ? 0 : (u32)delta_x, min_client_size.w
                       ),
            .h = horizontal_edge
                     ? client_geom.size.h
                     : u32_2max(
                           delta_y < 0 ? 0 : (u32)delta_y, min_client_size.h
                       )
        };

        client->surface->driver.set_size(&new_size, client->surface);
    }
    else if (g_pointer_mode.mode == WHALE_POINTER_MODE_INTERACTIVE_MOVE)
    {
        WhalePosition2D cursor_pos;
        wh_pointer_get_pos(&cursor_pos);

        wh_coord_t dx = cursor_pos.x - g_pointer_mode.cursor_start_pos.x;
        wh_coord_t dy = cursor_pos.y - g_pointer_mode.cursor_start_pos.y;

        WhalePosition2D new_pos = {
            .x = g_pointer_mode.client_start_pos.x + dx,
            .y = g_pointer_mode.client_start_pos.y + dy
        };

        wh_client_set_pos(&new_pos, client);
    }
}

static void handle_pointer_motion(u32 ev_time_ms)
{
    if (POINTER_IS_INTERACTIVE())
    {
        handle_interactive_pointer_motion();
        return;
    }

    wh_seat_refocus_input(false);

    if (g_pointer_focus_surface)
    {
        WhalePosition2D cursor_pos;
        wh_pointer_get_pos(&cursor_pos);

        WhalePosition2D surface_coords;
        wh_surface_layout_to_surface_coords(
            g_pointer_focus_surface, &cursor_pos, &surface_coords
        );

        wlr_seat_pointer_notify_motion(
            g_seat, ev_time_ms, surface_coords.x, surface_coords.y
        );
    }
}

WH_CALLBACK(pointer_motion_absolute, struct wl_listener*, void* data)
{
    struct wlr_pointer_motion_absolute_event* ev = data;

    /* Move the cursor image to the same position */
    wlr_cursor_warp_absolute(g_pointer, &ev->pointer->base, ev->x, ev->y);

    handle_pointer_motion(ev->time_msec);
}

WH_CALLBACK(pointer_motion, struct wl_listener*, void* data)
{
    struct wlr_pointer_motion_event* ev = data;

    /* Move the cursor image to the same position */
    wlr_cursor_move(g_pointer, &ev->pointer->base, ev->delta_x, ev->delta_y);

    handle_pointer_motion(ev->time_msec);
}

WH_CALLBACK(pointer_button, struct wl_listener*, void* data)
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
        WhalePosition2D cursor_pos;
        wh_pointer_get_pos(&cursor_pos);

        WhaleSurface* surface = wh_scene_get_topmost_surface_at(&cursor_pos);

        if (!g_config_focus_follows_pointer &&
            surface != wh_keyboard_get_focused_surface())
        {
            if (surface)
                wh_keyboard_focus_surface(surface);
            else
                wh_keyboard_unfocus_unchecked();
        }

        if (surface)
        {
            WhaleClient* client = wh_client_from_surface(surface);
            if (client->layer == WH_CLIENT_LAYER_FLOATING)
                wh_client_raise_to_top(client);
        }
    }

    wlr_seat_pointer_notify_button(
        g_seat, ev->time_msec, ev->button, ev->state
    );
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

    if (ev->seat_client != g_seat->pointer_state.focused_client)
        return;

    wlr_cursor_set_surface(
        g_pointer, ev->surface, ev->hotspot_x, ev->hotspot_y
    );
}

int wh_pointer_init(struct wlr_seat* seat)
{
    g_seat = seat;

    g_pointer = wlr_cursor_create();
    if (!g_pointer)
    {
        wh_log(ERR, "pointer: Failed to create pointer.");
        return -1;
    }

    wh_scene_attach_pointer(g_pointer);

    g_xcursor_manager = wlr_xcursor_manager_create(NULL, 24);
    if (!g_xcursor_manager)
    {
        wh_log(ERR, "pointer: Failed to create xcursor manager.");
        return -1;
    }

    setenv("XCURSOR_SIZE", "24", 1);

    WH_LISTEN(&g_pointer->events.motion, pointer_motion);
    WH_LISTEN(&g_pointer->events.motion_absolute, pointer_motion_absolute);
    WH_LISTEN(&g_pointer->events.button, pointer_button);
    WH_LISTEN(&g_pointer->events.axis, pointer_axis);
    WH_LISTEN(&g_pointer->events.frame, pointer_frame);

    WH_LISTEN(&seat->events.request_set_cursor, seat_request_set_cursor);

    wlr_cursor_warp_closest(g_pointer, NULL, g_pointer->x, g_pointer->y);
    wh_pointer_set_texture("default");

    return 0;
}

void wh_pointer_destroy()
{
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

    wlr_cursor_destroy(g_pointer);
    g_pointer = nullptr;

    g_seat = nullptr;
}

int wh_pointer_attach_device(struct wlr_pointer* pointer)
{
    wlr_cursor_attach_input_device(g_pointer, &pointer->base);
    return 0;
}

void wh_pointer_focus_surface(
    const WhalePosition2D* enter_coords, WhaleSurface* surface
)
{
    if (surface == g_pointer_focus_surface)
        return;

    wlr_seat_pointer_notify_enter(
        g_seat, surface->wlr_surface, enter_coords->x, enter_coords->y
    );

    g_pointer_focus_surface = surface;
}

void wh_pointer_unfocus_unchecked()
{
    wlr_seat_pointer_notify_clear_focus(g_seat);
    g_pointer_focus_surface = nullptr;
}

void wh_pointer_start_interactive_move(WhaleSurface* surface)
{
    /* Only the keyboard focused (active) surface can request a
    pointer mode. */
    if (surface != wh_keyboard_get_focused_surface() ||
        POINTER_IS_INTERACTIVE())
        return;

    g_pointer_mode.mode = WHALE_POINTER_MODE_INTERACTIVE_MOVE;
    wh_pointer_get_pos(&g_pointer_mode.cursor_start_pos);

    WhaleGeometry2D client_geom;
    wh_client_get_geometry(&client_geom, wh_client_from_surface(surface));
    g_pointer_mode.client_start_pos = client_geom.pos;
}

void wh_pointer_start_interactive_resize(u32 edge, WhaleSurface* surface)
{
    if (surface != wh_keyboard_get_focused_surface() ||
        POINTER_IS_INTERACTIVE())
        return;

    g_pointer_mode.mode = WHALE_POINTER_MODE_INTERACTIVE_RESIZE;
    wh_pointer_get_pos(&g_pointer_mode.cursor_start_pos);

    WhaleGeometry2D client_geom;
    wh_client_get_geometry(&client_geom, wh_client_from_surface(surface));
    g_pointer_mode.client_start_pos = client_geom.pos;

    g_pointer_mode.resize_edge = edge;
}

void wh_pointer_drop_interactive()
{
    g_pointer_mode.mode = WHALE_POINTER_MODE_PASSTHROUGH;
}

void wh_pointer_set_texture(const char* name)
{
    wlr_cursor_set_xcursor(g_pointer, g_xcursor_manager, name);
}

WhaleSurface* wh_pointer_get_focused_surface()
{
    return g_pointer_focus_surface;
}

void wh_pointer_get_pos(WhalePosition2D* out_pos)
{
    out_pos->x = (s32)g_pointer->x;
    out_pos->y = (s32)g_pointer->y;
}

bool wh_pointer_focus_follows_pointer()
{
    return g_config_focus_follows_pointer;
}
