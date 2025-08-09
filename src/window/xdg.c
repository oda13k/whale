/**
 * Copyright Olaru Alexandru.
 * Distributed under the MIT license.
 *
 * This file implements the xdg protocols for clients, meaning xdg toplevel and
 * xdg decorations.
 */

#include <stdlib.h>
#include <wayland-server-core.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/types.h>
#include <whale/utils.h>
#include <whale/window/client.h>
#include <whale/window/xdg.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

/* Modified version wl_container_of that takes in a type instead of a variable
upon which we do a typeof() */
#define CONTAINER_OF(_ptr, _sample_type, _member)                              \
    ((_sample_type*)((char*)(_ptr) - offsetof(_sample_type, _member)))

#define WH_XDG_TOPLEVEL_DATA_FROM_LISTENER(_ptr, _listener_name)               \
    (CONTAINER_OF(_ptr, WhaleXDGToplevelData, listeners._listener_name))

#define XDG_DATA_FROM_SURFACE(_surf) (_surf->driver.ctx)

typedef struct
{
    WhaleClient* client;

    struct wlr_xdg_toplevel* toplevel;
    struct wlr_xdg_toplevel_decoration_v1* toplevel_decoration;

    struct
    {
        struct wl_listener destroy;
        struct wl_listener set_title;

        struct wl_listener decoration_request_mode;
        struct wl_listener decoration_destroy;
    } listeners;
} WhaleXDGToplevelData;

static WhaleCompositor* g_comp;
struct wlr_xdg_shell* g_xdg_shell;
struct wlr_xdg_decoration_manager_v1* g_xdg_decoration_manager;

static WhaleSurface*
surface_from_xdg_toplevel(struct wlr_xdg_toplevel* toplevel)
{
    WH_ASSERT(toplevel->base->data);
    return toplevel->base->data;
}

static void
xdg_toplevel_set_size(const wh_size2d_t* size, WhaleSurface* surface)
{
    WhaleXDGToplevelData* xdg_data = XDG_DATA_FROM_SURFACE(surface);
    WH_ASSERT_DEBUG(xdg_data);

    wlr_xdg_toplevel_set_size(xdg_data->toplevel, size->w, size->h);
}

static void xdg_toplevel_get_size(wh_size2d_t* out_size, WhaleSurface* surface)
{
    WhaleXDGToplevelData* xdg_data = XDG_DATA_FROM_SURFACE(surface);
    WH_ASSERT_DEBUG(xdg_data);

    out_size->w = xdg_data->toplevel->base->geometry.width;
    out_size->h = xdg_data->toplevel->base->geometry.height;
}

static void xdg_toplevel_get_minmax_size(
    wh_size2d_t* min_size, wh_size2d_t* max_size, WhaleSurface* surface
)
{
    WhaleXDGToplevelData* xdg_data = XDG_DATA_FROM_SURFACE(surface);
    WH_ASSERT_DEBUG(xdg_data);

    struct wlr_xdg_toplevel* toplevel = xdg_data->toplevel;
    struct wlr_xdg_toplevel_state* state = &toplevel->current;

    if (min_size)
    {
        min_size->w = state->min_width;
        min_size->h = state->min_height;
    }

    if (max_size)
    {
        max_size->w = state->max_width;
        max_size->h = state->max_height;
    }
}

static void xdg_toplevel_set_active(bool active, WhaleClient* client)
{
    struct wlr_xdg_toplevel* toplevel =
        wlr_xdg_toplevel_try_from_wlr_surface(client->surface->wlr_surface);

    WH_ASSERT_DEBUG(toplevel);

    wlr_xdg_toplevel_set_activated(toplevel, active);
}

static void xdg_set_decoration_mode(WhaleXDGToplevelData* xdg_data)
{
    if (!xdg_data->toplevel->base->initialized)
        return;

    wlr_xdg_toplevel_decoration_v1_set_mode(
        xdg_data->toplevel_decoration,
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
    );
}

static int xdg_surface_on_commit_initial(WhaleSurface* surface)
{
    WhaleXDGToplevelData* xdg_data = XDG_DATA_FROM_SURFACE(surface);

    if (xdg_data->toplevel->base->initial_commit)
    {
        if (xdg_data->toplevel_decoration)
            xdg_set_decoration_mode(xdg_data);

        wlr_xdg_surface_schedule_configure(xdg_data->toplevel->base);
    }

    return WHALE_SURFACE_CALLBACK_REMOVE_SELF;
}

static int xdg_surface_on_commit_offset_internal_geometry(WhaleSurface* surface)
{
    WhaleXDGToplevelData* xdg_data = XDG_DATA_FROM_SURFACE(surface);

    wlr_scene_node_set_position(
        &surface->scene_surface_tree->node,
        -xdg_data->toplevel->base->geometry.x,
        -xdg_data->toplevel->base->geometry.y
    );

    return WHALE_SURFACE_CALLBACK_OK;
}

/* Window events */
static void on_xdg_toplevel_set_title(struct wl_listener* listener, void*)
{
    WhaleXDGToplevelData* xdg_data =
        WH_XDG_TOPLEVEL_DATA_FROM_LISTENER(listener, set_title);

    WhaleClient* client = xdg_data->client;
    WH_ASSERT_DEBUG(client);

    wh_client_set_title(xdg_data->toplevel->title, client);
}

static void on_xdg_toplevel_destroy(struct wl_listener* listener, void*)
{
    WhaleXDGToplevelData* xdg_data =
        WH_XDG_TOPLEVEL_DATA_FROM_LISTENER(listener, destroy);

    WhaleClient* client = xdg_data->client;
    WH_ASSERT_DEBUG(client);

    UNLISTEN(&xdg_data->listeners.destroy);
    UNLISTEN(&xdg_data->listeners.set_title);

    free(xdg_data);

    wh_client_destroy(client);
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
        exit(1);
    }

    client->driver.set_active = xdg_toplevel_set_active;

    client->surface->driver.ctx = xdg_data;
    client->surface->driver.set_size = xdg_toplevel_set_size;
    client->surface->driver.get_size = xdg_toplevel_get_size;
    client->surface->driver.get_minmax_size = xdg_toplevel_get_minmax_size;

    wh_surface_register_commit_cb(
        xdg_surface_on_commit_initial, client->surface
    );

    wh_surface_register_commit_cb(
        xdg_surface_on_commit_offset_internal_geometry, client->surface
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
}

static int popup_commit(WhaleSurface* surface)
{
    struct wlr_xdg_popup* popup =
        wlr_xdg_popup_try_from_wlr_surface(surface->wlr_surface);

    if (popup->base->initial_commit)
        wlr_xdg_surface_schedule_configure(popup->base);

    wlr_scene_node_set_position(
        &surface->scene_surface_tree->node,
        popup->current.geometry.x,
        popup->current.geometry.y
    );

    return WHALE_SURFACE_CALLBACK_OK;
}

static void on_xdg_popup_new(struct wl_listener* listener, void* data)
{
    struct wlr_xdg_popup* xdg_popup = data;

    WhaleSurface* parent_surface = xdg_popup->parent->data;

    WhaleSurface* surface = wh_surface_new(
        xdg_popup->base->surface, parent_surface->scene_surface_tree
    );

    surface->type = SURFACE_TYPE_POPUP;
    surface->data = nullptr; /* no data for popups */

    wh_surface_register_commit_cb(popup_commit, surface);

    surface->parent = parent_surface;
    VEC_PUSH(surface, &parent_surface->children);
}

static void
on_xdg_toplevel_decoration_request_mode(struct wl_listener* listener, void*)
{
    WhaleXDGToplevelData* xdg_data =
        WH_XDG_TOPLEVEL_DATA_FROM_LISTENER(listener, decoration_request_mode);

    /* Ignore requested decoration modes, turn them off instead. */
    xdg_set_decoration_mode(xdg_data);
}

static void
on_xdg_toplevel_decoration_destroy(struct wl_listener* listener, void*)
{
    WhaleXDGToplevelData* xdg_data =
        WH_XDG_TOPLEVEL_DATA_FROM_LISTENER(listener, decoration_destroy);
    WH_ASSERT_DEBUG(xdg_data);

    xdg_data->toplevel_decoration = nullptr;
    UNLISTEN(&xdg_data->listeners.decoration_request_mode);
    UNLISTEN(&xdg_data->listeners.decoration_destroy);
}

static void on_xdg_toplevel_decoration_new(struct wl_listener*, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* decoration = data;

    WhaleSurface* surface = surface_from_xdg_toplevel(decoration->toplevel);

    WhaleXDGToplevelData* xdg_data = surface->driver.ctx;
    WH_ASSERT_DEBUG(xdg_data);

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

int wh_client_xdg_shell_init(WhaleCompositor* comp)
{
    g_comp = comp;

    g_xdg_shell = wlr_xdg_shell_create(comp->display, 6);
    LISTEN(
        &g_xdg_shell->events.new_toplevel,
        &comp->listeners.xdg_new_toplevel,
        on_xdg_toplevel_new
    );

    LISTEN(
        &g_xdg_shell->events.new_popup,
        &comp->listeners.xdg_new_popup,
        on_xdg_popup_new
    );

    /* This is the intended client decoration protocol. */
    g_xdg_decoration_manager =
        wlr_xdg_decoration_manager_v1_create(g_comp->display);

    LISTEN(
        &g_xdg_decoration_manager->events.new_toplevel_decoration,
        &comp->listeners.xdg_new_decoration,
        on_xdg_toplevel_decoration_new
    );

    return 0;
}

void wh_client_xdg_shell_destroy(WhaleCompositor* comp)
{
    g_xdg_shell = nullptr;
    UNLISTEN(&comp->listeners.xdg_new_toplevel);

    g_xdg_decoration_manager = nullptr;
    UNLISTEN(&comp->listeners.xdg_new_decoration);
}
