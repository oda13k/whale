/**
 * Copyright Olaru Alexandru.
 * Distributed under the MIT license.
 *
 * This file implements the xdg protocols for clients, meaning xdg toplevel and
 * xdg decorations.
 */

#include "xdg_shell.h"
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <whale/client/client.h>
#include <whale/compositor.h>
#include <whale/debug.h>
#include <whale/input/pointer.h>
#include <whale/log.h>
#include <whale/output/workspace.h>
#include <whale/types.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

static struct wlr_xdg_shell* g_xdg_shell;
static struct wlr_xdg_decoration_manager_v1* g_xdg_decoration_manager;
static struct wl_listener g_xdg_shell_new_toplevel_listener;
static struct wl_listener g_xdg_shell_new_popup_listener;
static struct wl_listener g_xdg_shell_new_toplevel_decoration_listener;

/* Modified version wl_container_of that takes in a type instead of a variable
upon which we do a typeof() */
#define CONTAINER_OF(_ptr, _sample_type, _member)                              \
    ((_sample_type*)((void*)(_ptr) - offsetof(_sample_type, _member)))

/* No point in doing a sanity assert on this since it's an offset into a struct
 */
#define XDG_TOPLEVEL_DATA_FROM_LISTENER(_ptr, _listener_name)                  \
    (CONTAINER_OF(_ptr, WhaleXDGToplevelData, listeners._listener_name))

#define XDG_TOPLEVEL_DATA_FROM_SURFACE(_surf)                                  \
    ({                                                                         \
        WH_ASSERT_SANITY(_surf->driver.ctx);                                   \
        _surf->driver.ctx;                                                     \
    })

#define SURFACE_FROM_XDG_TOPLEVEL(_toplevel)                                   \
    ({                                                                         \
        WH_ASSERT_SANITY(_toplevel->base->data);                               \
        _toplevel->base->data;                                                 \
    })

typedef struct
{
    WhaleClient* client;
    struct wlr_xdg_toplevel* toplevel;
    struct wlr_xdg_toplevel_decoration_v1* toplevel_decoration;

    struct
    {
        struct wl_listener destroy;
        struct wl_listener set_title;
        struct wl_listener request_move;
        struct wl_listener request_resize;
        struct wl_listener decoration_request_mode;
        struct wl_listener decoration_destroy;
        struct wl_listener request_fullscreen;
        struct wl_listener request_maximize;
    } listeners;
} WhaleXDGToplevelData;

/* === XDG Decorations === */
static void xdg_set_decoration_mode(WhaleXDGToplevelData* xdg_data)
{
    if (!xdg_data->toplevel->base->initialized)
        return;

    wlr_xdg_toplevel_decoration_v1_set_mode(
        xdg_data->toplevel_decoration,
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
    );
}

static void
on_xdg_toplevel_decoration_request_mode(struct wl_listener* listener, void*)
{
    WhaleXDGToplevelData* xdg_data =
        XDG_TOPLEVEL_DATA_FROM_LISTENER(listener, decoration_request_mode);

    /* Ignore requested decoration modes, turn them off instead. */
    xdg_set_decoration_mode(xdg_data);
}

static void
on_xdg_toplevel_decoration_destroy(struct wl_listener* listener, void*)
{
    WhaleXDGToplevelData* xdg_data =
        XDG_TOPLEVEL_DATA_FROM_LISTENER(listener, decoration_destroy);

    xdg_data->toplevel_decoration = nullptr;
    UNLISTEN(&xdg_data->listeners.decoration_request_mode);
    UNLISTEN(&xdg_data->listeners.decoration_destroy);
}

static void on_xdg_toplevel_decoration_new(struct wl_listener*, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* decoration = data;
    WhaleSurface* surface = SURFACE_FROM_XDG_TOPLEVEL(decoration->toplevel);
    WhaleXDGToplevelData* xdg_data = XDG_TOPLEVEL_DATA_FROM_SURFACE(surface);

    xdg_data->toplevel_decoration = decoration;

    LISTEN(
        &decoration->events.request_mode,
        &xdg_data->listeners.decoration_request_mode,
        on_xdg_toplevel_decoration_request_mode
    );

    LISTEN(
        &decoration->events.destroy,
        &xdg_data->listeners.decoration_destroy,
        on_xdg_toplevel_decoration_destroy
    );

    xdg_set_decoration_mode(xdg_data);
}

WH_SURFACE_CALLBACK(xdg_popup_on_initial_commit, surface)
{
    struct wlr_xdg_popup* popup = surface->data;
    if (popup->base->initial_commit)
        wlr_xdg_surface_schedule_configure(popup->base);

    return WHALE_SURFACE_CALLBACK_REMOVE_SELF;
}

WH_SURFACE_CALLBACK(xdg_popup_on_commit_update_position, surface)
{
    struct wlr_xdg_popup* popup = surface->data;

    WhalePosition2D offset = {0};

    struct wlr_xdg_surface* xdg_surf =
        wlr_xdg_surface_try_from_wlr_surface(popup->parent);

    if (xdg_surf)
    {
        offset.x = xdg_surf->geometry.x;
        offset.y = xdg_surf->geometry.y;
    }

    wlr_scene_node_set_position(
        &surface->scene_surface_tree->node,
        popup->current.geometry.x + offset.x,
        popup->current.geometry.y + offset.y
    );

    return WHALE_SURFACE_CALLBACK_OK;
}

static void on_xdg_popup_new(struct wl_listener*, void* data)
{
    struct wlr_xdg_popup* xdg_popup = data;

    WhaleSurface* parent_surface =
        wh_surface_from_wlr_surface(xdg_popup->parent);

    WhaleSurface* surface = wh_surface_new(
        xdg_popup->base->surface, parent_surface->scene_surface_tree
    );

    surface->type = SURFACE_TYPE_POPUP;
    surface->data = xdg_popup; /* no data for popups */

    wh_surface_register_commit_cb(xdg_popup_on_initial_commit, surface);
    wh_surface_register_commit_cb(xdg_popup_on_commit_update_position, surface);

    surface->parent = parent_surface;
    VEC_PUSH(surface, &parent_surface->children);
}

static void
xdg_toplevel_set_size(const WhaleSize2D* size, WhaleSurface* surface)
{
    WhaleXDGToplevelData* xdg_data = XDG_TOPLEVEL_DATA_FROM_SURFACE(surface);

    WH_ASSERT(size->w <= S32_MAX_VALUE);
    WH_ASSERT(size->h <= S32_MAX_VALUE);

    wlr_xdg_toplevel_set_size(xdg_data->toplevel, (s32)size->w, (s32)size->h);
}

static void xdg_toplevel_get_size(WhaleSize2D* out_size, WhaleSurface* surface)
{
    WhaleXDGToplevelData* xdg_data = XDG_TOPLEVEL_DATA_FROM_SURFACE(surface);

    WH_ASSERT(xdg_data->toplevel->base->geometry.width >= 0);
    WH_ASSERT(xdg_data->toplevel->base->geometry.height >= 0);

    out_size->w = (wh_dim_t)xdg_data->toplevel->base->geometry.width;
    out_size->h = (wh_dim_t)xdg_data->toplevel->base->geometry.height;
}

static void xdg_toplevel_get_minmax_size(
    WhaleSize2D* min_size, WhaleSize2D* max_size, WhaleSurface* surface
)
{
    WhaleXDGToplevelData* xdg_data = XDG_TOPLEVEL_DATA_FROM_SURFACE(surface);

    struct wlr_xdg_toplevel* toplevel = xdg_data->toplevel;
    struct wlr_xdg_toplevel_state* state = &toplevel->current;

    if (min_size)
    {
        WH_ASSERT(state->min_width >= 0);
        WH_ASSERT(state->min_height >= 0);

        min_size->w = (wh_dim_t)state->min_width;
        min_size->h = (wh_dim_t)state->min_height;
    }

    if (max_size)
    {
        WH_ASSERT(state->max_width >= 0);
        WH_ASSERT(state->max_height >= 0);

        max_size->w = (wh_dim_t)state->max_width;
        max_size->h = (wh_dim_t)state->max_height;
    }
}

static void xdg_toplevel_set_active(bool active, WhaleClient* client)
{
    WhaleXDGToplevelData* xdg_data =
        XDG_TOPLEVEL_DATA_FROM_SURFACE(client->surface);

    wlr_xdg_toplevel_set_activated(xdg_data->toplevel, active);
}

static WhaleClient* xdg_toplevel_get_parent(WhaleClient* client)
{
    WhaleXDGToplevelData* xdg_data =
        XDG_TOPLEVEL_DATA_FROM_SURFACE(client->surface);

    if (!xdg_data->toplevel->parent)
        return nullptr;

    return wh_client_from_surface(
        wh_surface_from_wlr_surface(xdg_data->toplevel->parent->base->surface)
    );
}

static void xdg_toplevel_close(WhaleClient* client)
{
    WhaleXDGToplevelData* xdg_data =
        XDG_TOPLEVEL_DATA_FROM_SURFACE(client->surface);

    wlr_xdg_toplevel_send_close(xdg_data->toplevel);
}

WH_SURFACE_CALLBACK(xdg_toplevel_on_initial_commit, surface)
{
    WhaleXDGToplevelData* xdg_data = XDG_TOPLEVEL_DATA_FROM_SURFACE(surface);

    if (xdg_data->toplevel->base->initial_commit)
    {
        if (xdg_data->toplevel_decoration)
            xdg_set_decoration_mode(xdg_data);

        wlr_xdg_toplevel_set_wm_capabilities(
            xdg_data->toplevel, WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN
        );

        wlr_xdg_surface_schedule_configure(xdg_data->toplevel->base);
    }

    return WHALE_SURFACE_CALLBACK_REMOVE_SELF;
}

WH_SURFACE_CALLBACK(
    xdg_toplevel_on_commit_offset_position_by_internal_geom, surface
)
{
    WhaleXDGToplevelData* xdg_data = XDG_TOPLEVEL_DATA_FROM_SURFACE(surface);

    wlr_scene_node_set_position(
        &surface->scene_surface_tree->node,
        -xdg_data->toplevel->base->geometry.x,
        -xdg_data->toplevel->base->geometry.y
    );

    return WHALE_SURFACE_CALLBACK_OK;
}

static void on_xdg_toplevel_set_title(struct wl_listener*, void*) {}

static void on_xdg_toplevel_destroy(struct wl_listener* listener, void*)
{
    WhaleXDGToplevelData* xdg_data =
        XDG_TOPLEVEL_DATA_FROM_LISTENER(listener, destroy);

    WhaleClient* client = xdg_data->client;

    UNLISTEN(&xdg_data->listeners.destroy);
    UNLISTEN(&xdg_data->listeners.set_title);
    UNLISTEN(&xdg_data->listeners.request_move);
    UNLISTEN(&xdg_data->listeners.request_resize);
    UNLISTEN(&xdg_data->listeners.request_fullscreen);
    UNLISTEN(&xdg_data->listeners.request_maximize);

    free(xdg_data);

    wh_client_destroy(client);
}

static void
on_xdg_toplevel_request_move(struct wl_listener* listener, void* data)
{
    struct wlr_xdg_toplevel_move_event* ev = data;

    WhaleXDGToplevelData* xdg_data =
        XDG_TOPLEVEL_DATA_FROM_LISTENER(listener, request_move);

    for (size_t i = 0; i < ev->seat->seat->pointer_state.button_count; ++i)
    {
        if (ev->seat->seat->pointer_state.buttons[i].button == BTN_LEFT)
        {
            wh_pointer_start_interactive_move(xdg_data->client->surface);
            return;
        }
    }
}

static void
on_xdg_toplevel_request_resize(struct wl_listener* listener, void* data)
{
    struct wlr_xdg_toplevel_resize_event* ev = data;

    WhaleXDGToplevelData* xdg_data =
        XDG_TOPLEVEL_DATA_FROM_LISTENER(listener, request_resize);

    for (size_t i = 0; i < ev->seat->seat->pointer_state.button_count; ++i)
    {
        if (ev->seat->seat->pointer_state.buttons[i].button == BTN_LEFT)
        {
            wh_pointer_start_interactive_resize(
                ev->edges, xdg_data->client->surface
            );
            return;
        }
    }
}

static void
on_xdg_toplevel_request_fullscreen(struct wl_listener* listener, void*)
{
    WhaleXDGToplevelData* xdg_data =
        XDG_TOPLEVEL_DATA_FROM_LISTENER(listener, request_fullscreen);

    WhaleClient* client = xdg_data->client;
    bool fullscreen = xdg_data->toplevel->requested.fullscreen;

    if (fullscreen)
        wh_client_set_layer(WH_CLIENT_LAYER_FULLSCREEN, client);
    else if (client->prev_layer != WH_CLIENT_LAYER_UNDEFINED)
        wh_client_set_layer(client->prev_layer, client);

    if (xdg_data->toplevel->base->initialized)
    {
        /* Cool and funny note: If we don't actually honor this request
        (we can't just send a simple configure notify) firefox shits it's
        pants and it's UI stops working after requesting a fullscreen :^) */
        wlr_xdg_toplevel_set_fullscreen(xdg_data->toplevel, fullscreen);
        wh_workspace_arrange(client->workspace);
    }
}

static void
on_xdg_toplevel_request_maximize(struct wl_listener* listener, void*)
{
    WhaleXDGToplevelData* xdg_data =
        XDG_TOPLEVEL_DATA_FROM_LISTENER(listener, request_maximize);

    if (xdg_data->toplevel->base->initialized)
        wlr_xdg_surface_schedule_configure(xdg_data->toplevel->base);
}

static void on_xdg_toplevel_new(struct wl_listener*, void* data)
{
    struct wlr_xdg_toplevel* toplevel = data;

    WhaleXDGToplevelData* xdg_data = calloc(1, sizeof(WhaleXDGToplevelData));
    if (!xdg_data)
    {
        wh_log(FATAL, "xdg: Failed to allocate xdg toplevel data.");
        return;
    }

    WhaleClient* client = wh_client_new(toplevel->base->surface);
    if (!client)
    {
        wh_log(FATAL, "Failed to allocate client.");
        free(xdg_data);
        exit(1);
    }

    /* Client drivers, stuff that only makes sense to have on clients and not on
     * all surfaces. */
    client->driver.set_active = xdg_toplevel_set_active;
    client->driver.get_parent = xdg_toplevel_get_parent;
    client->driver.close = xdg_toplevel_close;

    /* Surface drivers, stuff you'd expect to be able to call on any type of
     * surface. */
    client->surface->driver.ctx = xdg_data;
    client->surface->driver.set_size = xdg_toplevel_set_size;
    client->surface->driver.get_size = xdg_toplevel_get_size;
    client->surface->driver.get_minmax_size = xdg_toplevel_get_minmax_size;

    wh_surface_register_commit_cb(
        xdg_toplevel_on_initial_commit, client->surface
    );

    wh_surface_register_commit_cb(
        xdg_toplevel_on_commit_offset_position_by_internal_geom, client->surface
    );

    xdg_data->client = client;
    xdg_data->toplevel = toplevel;
    /* The xdg surface can point back to the client */
    xdg_data->toplevel->base->data = client->surface;

    LISTEN(
        &xdg_data->toplevel->events.destroy,
        &xdg_data->listeners.destroy,
        on_xdg_toplevel_destroy
    );

    LISTEN(
        &xdg_data->toplevel->events.set_title,
        &xdg_data->listeners.set_title,
        on_xdg_toplevel_set_title
    );

    LISTEN(
        &xdg_data->toplevel->events.request_move,
        &xdg_data->listeners.request_move,
        on_xdg_toplevel_request_move
    );

    LISTEN(
        &xdg_data->toplevel->events.request_resize,
        &xdg_data->listeners.request_resize,
        on_xdg_toplevel_request_resize
    );

    LISTEN(
        &xdg_data->toplevel->events.request_fullscreen,
        &xdg_data->listeners.request_fullscreen,
        on_xdg_toplevel_request_fullscreen
    );

    LISTEN(
        &xdg_data->toplevel->events.request_maximize,
        &xdg_data->listeners.request_maximize,
        on_xdg_toplevel_request_maximize
    );
}

int wh_client_xdg_shell_init()
{
    g_xdg_shell = wlr_xdg_shell_create(wh_compositor_get_wl_display(), 6);
    if (!g_xdg_shell)
    {
        wh_log(ERR, "xdg-shell: Failed to create xdg shell global!");
        return -1;
    }

    LISTEN(
        &g_xdg_shell->events.new_toplevel,
        &g_xdg_shell_new_toplevel_listener,
        on_xdg_toplevel_new
    );

    LISTEN(
        &g_xdg_shell->events.new_popup,
        &g_xdg_shell_new_popup_listener,
        on_xdg_popup_new
    );

    /* This is the intended client decoration protocol. */
    g_xdg_decoration_manager =
        wlr_xdg_decoration_manager_v1_create(wh_compositor_get_wl_display());
    if (!g_xdg_decoration_manager)
    {
        /* wlroots doesn't have a way to unregister globals :^) */
        wh_client_xdg_shell_destroy();
        wh_log(
            ERR, "xdg-shell: Failed to create xdg decoration manager global!"
        );
        return -1;
    }

    LISTEN(
        &g_xdg_decoration_manager->events.new_toplevel_decoration,
        &g_xdg_shell_new_toplevel_decoration_listener,
        on_xdg_toplevel_decoration_new
    );

    return 0;
}

void wh_client_xdg_shell_destroy()
{
    if (g_xdg_decoration_manager)
    {
        g_xdg_decoration_manager = nullptr;
        UNLISTEN(&g_xdg_shell_new_toplevel_decoration_listener);
    }

    if (g_xdg_shell)
    {
        g_xdg_shell = nullptr;
        UNLISTEN(&g_xdg_shell_new_popup_listener);
        UNLISTEN(&g_xdg_shell_new_toplevel_listener);
    }
}
