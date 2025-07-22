/**
 * Copyright Olaru Alexandru.
 * Distributed under the MIT license.
 *
 * This file implements the xdg protocols for clients, meaning xdg toplevel and
 * xdg decorations.
 */

#include <stdlib.h>
#include <wayland-server-core.h>
#include <whale/client/client.h>
#include <whale/client/xdg.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/types.h>
#include <whale/utils.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>

/* Modified version wl_container_of that takes in a type instead of a variable
upon which we do a typeof() */
#define CONTAINER_OF(_ptr, _sample_type, _member)                              \
    ((_sample_type*)((char*)(_ptr) - offsetof(_sample_type, _member)))

#define WH_SURFACE_FROM_XDG_LISTENER(_ptr, _listener_name)                     \
    (CONTAINER_OF(_ptr, WhaleXDGClientData, listeners._listener_name)->surface)

#define XDG_DATA_FROM_SURFACE(_surf) (_surf->impl.data)

typedef struct
{
    WhaleSurface* surface;

    struct wlr_xdg_toplevel* toplevel;
    struct wlr_xdg_toplevel_decoration_v1* toplevel_decoration;

    struct
    {
        struct wl_listener destroy;
        struct wl_listener set_title;

        struct wl_listener decoration_request_mode;
        struct wl_listener decoration_destroy;
    } listeners;
} WhaleXDGClientData;

static WhaleCompositor* g_comp;
struct wlr_xdg_shell* g_xdg_shell;
struct wlr_xdg_decoration_manager_v1* g_xdg_decoration_manager;

static WhaleSurface*
wh_surface_from_xdg_toplevel(struct wlr_xdg_toplevel* toplevel)
{
    WH_ASSERT(toplevel->base->data);
    WhaleClient* client = toplevel->base->data;
    return &client->surface;
}

/* Client function implementations */
static void xdg_set_size(const wh_size2d_t* size, WhaleSurface* surface)
{
    WhaleXDGClientData* xdg_data = XDG_DATA_FROM_SURFACE(surface);
    WH_ASSERT_DEBUG(xdg_data);

    wlr_xdg_toplevel_set_size(xdg_data->toplevel, size->w, size->h);
}

static wh_size2d_t xdg_get_size(WhaleSurface* surface)
{
    WhaleXDGClientData* xdg_data = XDG_DATA_FROM_SURFACE(surface);
    WH_ASSERT_DEBUG(xdg_data);

    return (wh_size2d_t){.w = xdg_data->toplevel->base->geometry.width,
                         .h = xdg_data->toplevel->base->geometry.height};
}

static WhaleClient* xdg_get_parent(WhaleClient* client)
{
    WhaleXDGClientData* xdg_data = client->surface.impl.data;
    WH_ASSERT_DEBUG(xdg_data);

    if (!xdg_data->toplevel->parent)
        return nullptr;

    return wh_surface_from_xdg_toplevel(xdg_data->toplevel->parent);
}

static void xdg_send_close(WhaleClient* client)
{
    WhaleXDGClientData* xdg_data = client->surface.impl.data;
    WH_ASSERT_DEBUG(xdg_data);

    wlr_xdg_toplevel_send_close(xdg_data->toplevel);
}

static WhaleGeometry2D xdg_get_internal_geometry(const WhaleClient* client)
{
    WhaleXDGClientData* xdg_data = client->surface.impl.data;
    WH_ASSERT_DEBUG(xdg_data);

    struct wlr_box* geom = &xdg_data->toplevel->base->geometry;

    return (WhaleGeometry2D){
        .x = geom->x, .y = geom->y, .w = geom->width, .h = geom->height
    };
}

static void xdg_get_minmax_size(
    wh_size2d_t* min_size, wh_size2d_t* max_size, WhaleClient* client
)
{
    WhaleXDGClientData* xdg_data = client->surface.impl.data;
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

static void xdg_set_decoration_mode(WhaleSurface* surface)
{
    WhaleXDGClientData* xdg_data = XDG_DATA_FROM_SURFACE(surface);
    WH_ASSERT_DEBUG(xdg_data);

    if (!xdg_data->toplevel->base->initialized)
        return;

    wlr_xdg_toplevel_decoration_v1_set_mode(
        xdg_data->toplevel_decoration,
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
    );
}

/* Window events */
static void on_xdg_toplevel_set_title(struct wl_listener* listener, void*)
{
    [[maybe_unused]] WhaleSurface* surface =
        WH_SURFACE_FROM_XDG_LISTENER(listener, set_title);
}

// static void on_xdg_toplevel_commit(struct wl_listener* listener, void*)
// {
//     WhaleSurface* surface = WH_SURFACE_FROM_XDG_LISTENER(listener, commit);

//     WhaleXDGClientData* xdg_data = client->data;
//     WH_ASSERT_DEBUG(xdg_data);

//     if (xdg_data->toplevel->base->initial_commit)
//     {
//         if (xdg_data->toplevel_decoration)
//             xdg_set_decoration_mode(client);

//         /* The surface needs to receive a configure in order to work. */
//         wlr_xdg_surface_schedule_configure(xdg_data->toplevel->base);
//     }

//     wlr_scene_node_set_position(
//         &client->scene_surface_tree->node,
//         -xdg_data->toplevel->base->geometry.x,
//         -xdg_data->toplevel->base->geometry.y
//     );
// }

// static void on_xdg_toplevel_map(struct wl_listener* listener, void*)
// {
//     WhaleSurface* surface = WH_SURFACE_FROM_XDG_LISTENER(listener, map);

//     wh_client_map(client);
//     wh_workspace_init_client_layout(client);
//     wh_workspace_arrange(client->bound_workspace);

//     wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
//     wh_input_focus_client_under(&cursor_pos);
// }

// static void on_xdg_toplevel_unmap(struct wl_listener* listener, void*)
// {
//     WhaleSurface* surface = WH_SURFACE_FROM_XDG_LISTENER(listener, unmap);

//     wh_client_unmap(client);
//     wh_workspace_arrange(client->bound_workspace);

//     wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
//     wh_input_focus_client_under(&cursor_pos);
// }

static void on_xdg_toplevel_destroy(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_XDG_LISTENER(listener, destroy);

    /* Since this is a toplevel destroy, we must have a parent client */
    WhaleClient* client = surface->parent_client;
    WH_ASSERT(client);

    WhaleXDGClientData* xdg_data = surface->impl.data;

    UNLISTEN(&xdg_data->listeners.destroy);
    UNLISTEN(&xdg_data->listeners.set_title);

    free(xdg_data);

    wh_client_destroy(client);
}

static void xdg_surface_init(WhaleSurface* surface)
{
    WhaleXDGClientData* xdg_data = surface->impl.data;

    wlr_xdg_surface_schedule_configure(xdg_data->toplevel->base);
}

static void on_xdg_toplevel_new(struct wl_listener* listener, void* data)
{
    struct wlr_xdg_toplevel* toplevel = data;

    WhaleXDGClientData* xdg_data = calloc(1, sizeof(WhaleXDGClientData));
    if (!xdg_data)
    {
        wh_log(FATAL, "Failed to allocate xdg data.");
        exit(1);
    }

    WhaleSurfaceImplementation impl = {
        .data = xdg_data,
        .set_size = xdg_set_size,
        .get_size = xdg_get_size,
        .get_parent = xdg_get_parent,
        .send_close = xdg_send_close,
        .get_internal_geometry = xdg_get_internal_geometry,
        .get_minmax_size = xdg_get_minmax_size,
        .surface_init = xdg_surface_init
    };

    WhaleClient* client = wh_client_new(toplevel->base->surface, &impl);
    if (!client)
    {
        wh_log(FATAL, "Failed to allocate whale client.");
        exit(1);
    }

    xdg_data->surface = &client->surface;
    xdg_data->toplevel = toplevel;
    /* The xdg surface can point back to the client */
    xdg_data->toplevel->base->data = client;

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

static void on_xdg_popup_destroy(struct wl_listener* listener, void*)
{
    // WhaleXDGPopupData* popup =
    //     CONTAINER_OF(listener, WhaleXDGPopupData, listeners.destroy);

    // wlr_scene_node_destroy(&popup->scene_tree->node);
    // popup->scene_tree = nullptr;

    // UNLISTEN(&popup->listeners.commit);
    // UNLISTEN(&popup->listeners.destroy);

    // free(popup);
}

static void on_xdg_popup_commit(struct wl_listener* listener, void*)
{
    // WhaleXDGPopupData* popup =
    //     CONTAINER_OF(listener, WhaleXDGPopupData, listeners.commit);

    // if (popup->xdg_popup->base->initial_commit)
    //     wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
}

static void on_xdg_popup_new(struct wl_listener*, void* data)
{
    struct wlr_xdg_popup* xdg_popup = data;

    // WhaleXDGPopupData* popup = calloc(1, sizeof(WhaleXDGPopupData));
    // if (!popup)
    // {
    //     wlr_xdg_popup_destroy(xdg_popup);
    //     return;
    // }

    // WH_ASSERT(xdg_popup->parent);
    // struct wlr_xdg_toplevel* parent_xdg_toplevel =
    //     wlr_xdg_toplevel_try_from_wlr_surface(xdg_popup->parent);
    // WH_ASSERT(parent_xdg_toplevel);

    // WhaleClient* parent = wh_surface_from_xdg_toplevel(parent_xdg_toplevel);

    // popup->xdg_popup = xdg_popup;
    // popup->scene_tree =
    //     wlr_scene_xdg_surface_create(parent->scene_tree, xdg_popup->base);

    // LISTEN(
    //     &xdg_popup->base->surface->events.commit,
    //     &popup->listeners.commit,
    //     &on_xdg_popup_commit
    // );

    // LISTEN(
    //     &xdg_popup->events.destroy,
    //     &popup->listeners.destroy,
    //     &on_xdg_popup_destroy
    // );
}

static void
on_xdg_toplevel_decoration_request_mode(struct wl_listener* listener, void*)
{
    WhaleSurface* surface =
        WH_SURFACE_FROM_XDG_LISTENER(listener, decoration_request_mode);

    /* Ignore requested decoration modes, turn them off instead. */
    xdg_set_decoration_mode(surface);
}

static void
on_xdg_toplevel_decoration_destroy(struct wl_listener* listener, void*)
{
    WhaleSurface* surface =
        WH_SURFACE_FROM_XDG_LISTENER(listener, decoration_destroy);

    WhaleXDGClientData* xdg_data = surface->impl.data;
    WH_ASSERT_DEBUG(xdg_data);

    xdg_data->toplevel_decoration = nullptr;
    UNLISTEN(&xdg_data->listeners.decoration_request_mode);
    UNLISTEN(&xdg_data->listeners.decoration_destroy);
}

static void on_xdg_toplevel_decoration_new(struct wl_listener*, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* decoration = data;

    WhaleSurface* surface = wh_surface_from_xdg_toplevel(decoration->toplevel);

    WhaleXDGClientData* xdg_data = surface->impl.data;
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

    xdg_set_decoration_mode(surface);
}

int wh_client_xdg_shell_init(WhaleCompositor* comp)
{
    g_comp = comp;
    /**
     * The xdg shell is a protocol through which clients can create
     * toplevel windows and popups.
     *
     * A toplevel window is just a window.
     * A popup is well, a popup. Popups can only exist as children of a
     * toplevel.
     *
     * A toplevel can be a child of another toplevel, in which case we
     * treat it almost like a popup. An example of such a toplevel would be
     * the window that pops up when pressing ctrl+n in gimp.
     * Toplevels can also specify that they have fixed dimensions (i.e. can't be
     * resized by us), but are not necessarily the child of another toplevel, in
     * which case, again we treat them almost like popups. An example of such a
     * toplevel would be the discord or steam startup thingy.
     *
     * The difference between popups and toplevels that we treat like popups
     * is the way they are positioned. While popups specify their own position
     * relative to their parent toplevel, popup-like toplevels' behavior can be
     * seen in wh_client_arrange_floating().
     */
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
        wlr_xdg_decoration_manager_v1_create(comp->display);

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
