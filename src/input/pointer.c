
#include <linux/input-event-codes.h>
#include <whale/client/client.h>
#include <whale/compositor.h>
#include <whale/input/keyboard.h>
#include <whale/input/pointer.h>
#include <whale/input/seat.h>
#include <whale/log.h>
#include <whale/output/output.h>
#include <whale/output/scene.h>
#include <whale/types.h>
#include <whale/utils/env.h>
#include <whale/utils/math.h>
#include <whale/utils/proc.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
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
    struct wlr_pointer_constraint_v1* constraint;
    struct wl_listener destroy_listener;
} WhalePointerConstraint;

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

    VEC(struct wlr_pointer_constraint_v1*) constraints;
    struct wlr_pointer_constraint_v1* active_constraint;
} WhalePointer;

static struct wlr_seat* g_seat;
static struct wlr_xcursor_manager* g_xcursor_manager;
static struct wlr_relative_pointer_manager_v1* g_relative_pointer_manager;
static struct wlr_pointer_constraints_v1* g_pointer_constraints;
static struct wlr_pointer_constraint_v1* g_active_constraint;

static WhalePointer g_pointer;

static bool g_config_focus_follows_pointer = true;

static void pointer_release_all_buttons()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    time_t time = now.tv_sec * 1000 + now.tv_nsec / 1000000;

    for (size_t i = 0; i < 8; ++i)
    {
        wlr_seat_pointer_notify_button(
            g_seat, time, i + BTN_MOUSE, WL_POINTER_BUTTON_STATE_RELEASED
        );
    }
}

static void pointer_unfocus_unchecked()
{
    wh_pointer_drop_interactive();
    pointer_release_all_buttons();

    wlr_seat_pointer_notify_clear_focus(g_seat);
    g_pointer.focused_surface = nullptr;
}

static void pointer_focus_surface(
    const WhalePosition2D* enter_coords, WhaleSurface* surface
)
{
    if (g_pointer.focused_surface && g_pointer.focused_surface != surface)
        pointer_unfocus_unchecked();

    wlr_seat_pointer_notify_enter(
        g_seat, surface->wlr_surface, enter_coords->x, enter_coords->y
    );

    g_pointer.focused_surface = surface;
}

static void handle_interactive_pointer_motion(const WhalePosition2D* delta)
{
    WhaleClient* client = wh_client_from_surface(g_pointer.focused_surface);

    wlr_cursor_move(g_pointer.wlr_cursor, nullptr, delta->x, delta->y);

    if (g_pointer.mode == WHALE_POINTER_MODE_INTERACTIVE_RESIZE)
    {
        WhalePosition2D cursor_pos;
        wh_pointer_get_pos(&cursor_pos);

        WhaleGeometry2D client_geom;
        wh_client_get_geometry(&client_geom, client);

        WhaleSize2D min_size;
        wh_client_get_minmax_size(&min_size, nullptr, client);

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
            .w = vertical_edge ? client_geom.size.w
                               : MAX2(dx < 0 ? 0 : (u32)dx, min_size.w),
            .h = horizontal_edge ? client_geom.size.h
                                 : MAX2(dy < 0 ? 0 : (u32)dy, min_size.h)
        };

        wh_client_set_size(&new_size, client);
    }
    else if (g_pointer.mode == WHALE_POINTER_MODE_INTERACTIVE_MOVE)
    {
        WhalePosition2D cursor_pos;
        wh_pointer_get_pos(&cursor_pos);

        wh_coord_t dx =
            cursor_pos.x - g_pointer.interactive.pointer_start_pos.x;
        wh_coord_t dy =
            cursor_pos.y - g_pointer.interactive.pointer_start_pos.y;

        WhalePosition2D pos = {
            .x = g_pointer.interactive.client_start_pos.x + dx,
            .y = g_pointer.interactive.client_start_pos.y + dy
        };

        wh_client_set_pos(&pos, client);
        wh_client_configure(client);

        WhaleOutput* output = wh_output_get_at(&cursor_pos);
        if (output && client->workspace &&
            output != client->workspace->parent_output)
        {
            wh_workspace_unbind_client(client);
            wh_workspace_bind_client(client, output->active_workspace);
        }
    }
    else
    {
        WH_ASSERT_NOT_REACHED();
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

static bool
constrain_pos_to_surface(const WhaleSurface* surface, WhalePosition2D* pos)
{
    struct wlr_pointer_constraint_v1* constraint;
    wl_list_for_each(constraint, &g_pointer_constraints->constraints, link)
    {
        if (constraint->surface != surface->wlr_surface)
            continue;

        if (constraint == g_active_constraint)
            goto constrain;

        if (g_active_constraint)
        {
            struct wlr_pointer_constraint_v1* tmp;
            wl_list_for_each(tmp, &g_pointer_constraints->constraints, link)
            {
                if (tmp == g_active_constraint)
                {
                    wlr_pointer_constraint_v1_send_deactivated(
                        g_active_constraint
                    );
                    break;
                }
            }

            g_active_constraint = nullptr;
        }

        g_active_constraint = constraint;
        wlr_pointer_constraint_v1_send_activated(constraint);

    constrain:
        if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
            return true;

        WhaleClient* client = wh_client_from_surface(surface->data);

        WhaleGeometry2D client_geom;
        wh_client_get_geometry(&client_geom, client);

        pos->x = CLAMP(
            client_geom.pos.x + constraint->region.extents.x1,
            pos->x,
            client_geom.pos.x + constraint->region.extents.x2
        );

        pos->y = CLAMP(
            client_geom.pos.y + constraint->region.extents.y1,
            pos->y,
            client_geom.pos.y + constraint->region.extents.y2
        );
    }

    return false;
}

static void handle_pointer_motion(
    const WhalePosition2D* delta,
    const WhalePosition2D* delta_unaccel,
    struct wlr_input_device* device,
    u32 time_ms
)
{
    WhalePosition2D pointer_pos;
    wh_pointer_get_pos(&pointer_pos);
    pointer_pos.x += delta->x;
    pointer_pos.y += delta->y;

    const bool organic = !!device;
    if (organic)
    {
        // MAYBE FIXME: Should we send 0s if the motion is not organic?
        wlr_relative_pointer_manager_v1_send_relative_motion(
            g_relative_pointer_manager,
            g_seat,
            (u64)time_ms * 1000,
            delta->x,
            delta->y,
            delta_unaccel->x,
            delta_unaccel->y
        );

        if (g_pointer.focused_surface &&
            constrain_pos_to_surface(g_pointer.focused_surface, &pointer_pos))
            return;
    }

    /* Warp and get the positon again in case we got restrained by the
     * layout */
    wlr_cursor_warp_closest(
        g_pointer.wlr_cursor, device, pointer_pos.x, pointer_pos.y
    );
    wh_pointer_get_pos(&pointer_pos);

    if (g_pointer.drag_icon_tree)
    {
        wlr_scene_node_set_position(
            &g_pointer.drag_icon_tree->node,
            CAST_COORD_TO_INT(pointer_pos.x),
            CAST_COORD_TO_INT(pointer_pos.y)
        );
    }

    WhaleSurface* surface =
        wh_pointer_update_focus(g_config_focus_follows_pointer);
    if (!surface)
        return;

    WhalePosition2D surface_coords = {
        .x = pointer_pos.x - surface->layout_pos.x,
        .y = pointer_pos.y - surface->layout_pos.y
    };

    wlr_seat_pointer_notify_motion(
        g_seat, time_ms, surface_coords.x, surface_coords.y
    );
}

static void handle_pointer_button(
    u32 button, enum wl_pointer_button_state state, u32 time_ms
)
{
    if (state != WL_POINTER_BUTTON_STATE_PRESSED)
        goto notify;

    if (wh_keyboard_is_modifier_active(WH_KEYBOARD_MOD_NORMAL) &&
        g_pointer.focused_surface && button == BTN_LEFT)
    {
        if (wh_keyboard_is_modifier_active(WLR_MODIFIER_SHIFT))
        {
            WhaleClient* client =
                wh_client_from_surface(g_pointer.focused_surface);

            WhaleSize2D size = {.w = 1024, .h = 720};
            wh_client_set_layer(WH_LAYER_FLOATING, client);
            wh_client_set_size(&size, client);
        }

        wh_pointer_start_interactive_move(g_pointer.focused_surface);
        return;
    }

    if (button == BTN_LEFT || button == BTN_RIGHT || button == BTN_MIDDLE)
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

    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(
        g_pointer.wlr_cursor, &ev->pointer->base, ev->x, ev->y, &lx, &ly
    );

    WhalePosition2D delta = {
        .x = lx - g_pointer.wlr_cursor->x, .y = ly - g_pointer.wlr_cursor->y
    };

    if (UNLIKELY(POINTER_IS_INTERACTIVE()))
        handle_interactive_pointer_motion(&delta);
    else
        handle_pointer_motion(
            &delta, &delta, &ev->pointer->base, ev->time_msec
        );
}

WH_CALLBACK(pointer_motion, struct wl_listener*, void* data)
{
    struct wlr_pointer_motion_event* ev = data;

    WhalePosition2D delta = {.x = ev->delta_x, .y = ev->delta_y};
    WhalePosition2D delta_unaccel = {.x = ev->unaccel_dx, .y = ev->unaccel_dy};

    if (UNLIKELY(POINTER_IS_INTERACTIVE()))
        handle_interactive_pointer_motion(&delta);
    else
        handle_pointer_motion(
            &delta, &delta_unaccel, &ev->pointer->base, ev->time_msec
        );
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
    processed atomically. Frame events are sent after one or more pointer
    events and signal that those events can be processed. */
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

    if (VEC_INIT(&g_pointer.constraints) < 0)
    {
        wh_log(ERR, "pointer: Failed to allocate constraints vector.");
        return -1;
    }

    wh_output_layout_attach_pointer(g_pointer.wlr_cursor);

    g_xcursor_manager = wlr_xcursor_manager_create(NULL, 24);
    if (!g_xcursor_manager)
    {
        wh_log(ERR, "pointer: Failed to create xcursor manager.");
        return -1;
    }

    wh_setenv("XCURSOR_SIZE", "24", 1);

    g_relative_pointer_manager =
        wlr_relative_pointer_manager_v1_create(wh_compositor_get_wl_display());
    if (!g_relative_pointer_manager)
    {
        wh_log(ERR, "pointer: Failed to create relative pointer manager");
        return -1;
    }

    g_pointer_constraints =
        wlr_pointer_constraints_v1_create(wh_compositor_get_wl_display());
    if (!g_pointer_constraints)
    {
        wh_log(ERR, "pointer: Failed to create pointer constraints.");
        return -1;
    }

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

    // FIXME: put this check back in after we add transations.
    // if (surface != g_pointer.focused_surface)
    // {
    // }

    WhalePosition2D surface_coords = {
        .x = pointer_pos.x - surface->layout_pos.x,
        .y = pointer_pos.y - surface->layout_pos.y
    };

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

    WhaleClient* client = wh_client_from_surface(surface);

    wh_client_start_interactive(client);

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

    wh_client_drop_interactive(client);

    /* If we don't force a pointer refocus firefox can't be moved again until
     * the surface is refocused by the user. ??? */
    pointer_unfocus_unchecked();
    wh_pointer_update_focus(false);
}

void wh_pointer_set_texture(const char* name)
{
    wlr_cursor_set_xcursor(g_pointer.wlr_cursor, g_xcursor_manager, name);
}

struct wlr_xcursor* wh_pointer_get_texture(const char* name)
{
    return wlr_xcursor_manager_get_xcursor(g_xcursor_manager, name, 1);
}

void wh_pointer_set_pos(const WhalePosition2D* pos)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    time_t time_ms = now.tv_sec * 1000 + now.tv_nsec / 1000000;

    WhalePosition2D delta = {
        .x = pos->x - g_pointer.wlr_cursor->x,
        .y = pos->y - g_pointer.wlr_cursor->y
    };

    handle_pointer_motion(&delta, &delta, nullptr, time_ms);
}

void wh_pointer_get_pos(WhalePosition2D* out_pos)
{
    out_pos->x = g_pointer.wlr_cursor->x;
    out_pos->y = g_pointer.wlr_cursor->y;
}
