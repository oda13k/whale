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
#include <wlr/types/wlr_xdg_shell.h>

/* Modified version wl_container_of that takes in a type instead of a variable
upon which we do a typeof() */
#define CONTAINER_OF(_ptr, _sample_type, _member)                              \
    ((_sample_type*)((char*)(_ptr) - offsetof(_sample_type, _member)))

#define WH_CLIENT_FROM_XDG_LISTENER(_ptr, _listener_name)                      \
    (CONTAINER_OF(_ptr, WhaleXDGClientData, listeners._listener_name)->base)

typedef struct
{
    WhaleClient* base;

    struct wlr_xdg_toplevel* toplevel;
    struct wlr_xdg_toplevel_decoration_v1* toplevel_decoration;

    struct
    {
        struct wl_listener map;
        struct wl_listener unmap;
        struct wl_listener commit;

        struct wl_listener destroy;
        struct wl_listener set_title;

        struct wl_listener decoration_request_mode;
        struct wl_listener decoration_destroy;
    } listeners;
} WhaleXDGClientData;

struct wlr_xdg_shell* g_xdg_shell;
struct wlr_xdg_decoration_manager_v1* g_xdg_decoration_manager;

static WhaleClient*
wh_client_from_xdg_toplevel(struct wlr_xdg_toplevel* toplevel)
{
    return toplevel->base->data;
}

static bool wh_client_is_implicit_floating(WhaleClient* client)
{
    WhaleXDGClientData* xdg_data = client->data;

    struct wlr_xdg_toplevel* toplevel = xdg_data->toplevel;
    struct wlr_xdg_toplevel_state* state = &toplevel->current;

    /* If the client has a parent or if the client demands it's own
    size exactly, we treat it as floating. */
    bool demands_size = state->min_width && state->max_height &&
                        (state->min_width == state->max_width ||
                         state->min_height == state->max_height);

    return toplevel->parent || demands_size;
}

/* Client function implementations */
static void xdg_set_size(const wh_size2d_t* size, WhaleClient* client)
{
    WhaleXDGClientData* xdg_data = client->data;
    WH_ASSERT_DEBUG(xdg_data);

    wlr_xdg_toplevel_set_size(xdg_data->toplevel, size->w, size->h);

    /* stinky hack: wlr_xdg_toplevel_set_size is an async
    request, whereas position setting (for example) is done immediately. This
    means that when we do a move+resize combo, the move will be done immediately
    but the resize will take a *bit* more. This leads to a slight flicker where
    the moved window will display it's contents on an adjecent screen for a
    split second before the resize request goes through. We fix this by clipping
    it's contents so it can't display them outside it's new size. What can not
    be fixed this way is how a window will sometimes spawn with it's initial
    (client selected) size for a split second before the size it *should* have
    actually gets set. */
    struct wlr_box clip = {
        .x = xdg_data->toplevel->base->geometry.x,
        .y = xdg_data->toplevel->base->geometry.x,
        .width = size->w,
        .height = size->h
    };
    wlr_scene_subsurface_tree_set_clip(&client->scene_tree->node, &clip);
}

static wh_size2d_t xdg_get_size(WhaleClient* client)
{
    WhaleXDGClientData* xdg_data = client->data;
    WH_ASSERT_DEBUG(xdg_data);

    return (wh_size2d_t){.w = xdg_data->toplevel->base->geometry.width,
                         .h = xdg_data->toplevel->base->geometry.height};
}

static WhaleClient* xdg_get_parent(WhaleClient* client)
{
    WhaleXDGClientData* xdg_data = client->data;
    WH_ASSERT_DEBUG(xdg_data);

    if (!xdg_data->toplevel->parent)
        return nullptr;

    return wh_client_from_xdg_toplevel(xdg_data->toplevel->parent);
}

static void xdg_send_close(WhaleClient* client)
{
    WhaleXDGClientData* xdg_data = client->data;
    WH_ASSERT_DEBUG(xdg_data);

    wlr_xdg_toplevel_send_close(xdg_data->toplevel);
}

static struct wlr_surface* xdg_get_wlr_surface(const WhaleClient* client)
{
    WhaleXDGClientData* xdg_data = client->data;
    WH_ASSERT_DEBUG(xdg_data);

    return xdg_data->toplevel->base->surface;
}

static WhaleGeometry2D xdg_get_internal_geometry(const WhaleClient* client)
{
    WhaleXDGClientData* xdg_data = client->data;
    WH_ASSERT_DEBUG(xdg_data);

    struct wlr_box* geom = &xdg_data->toplevel->base->geometry;

    return (WhaleGeometry2D){
        .x = geom->x, .y = geom->y, .w = geom->width, .h = geom->height
    };
}

static void xdg_set_decoration_mode(WhaleClient* client)
{
    WhaleXDGClientData* xdg_data = client->data;
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
    [[maybe_unused]] WhaleClient* client =
        WH_CLIENT_FROM_XDG_LISTENER(listener, set_title);
}

static void on_xdg_toplevel_commit(struct wl_listener* listener, void*)
{
    WhaleClient* client = WH_CLIENT_FROM_XDG_LISTENER(listener, commit);

    WhaleXDGClientData* xdg_data = client->data;
    WH_ASSERT_DEBUG(xdg_data);

    if (xdg_data->toplevel->base->initial_commit)
    {
        if (xdg_data->toplevel_decoration)
            xdg_set_decoration_mode(client);

        /* The surface needs to receive a configure in order to work. */
        wlr_xdg_surface_schedule_configure(xdg_data->toplevel->base);
    }

    // WhaleClientArrangement default_arrangement = ARRANGE_TILED;
    // if (wh_client_is_implicit_floating(client))
    // {
    //     client->arrangement = ARRANGE_FLOATING;
    //     wh_client_arrange_clients_on_output(client->bound_output);
    // }
    // else
    // {
    //     // FIXME: keep old arrangement
    //     client->arrangement = default_arrangement;
    // }

    // wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
    // wh_input_focus_client_under(&cursor_pos);
}

static void on_xdg_toplevel_map(struct wl_listener* listener, void*)
{
    WhaleClient* client = WH_CLIENT_FROM_XDG_LISTENER(listener, map);

    if (wh_client_is_implicit_floating(client))
        client->arrangement = ARRANGE_FLOATING;

    wh_client_map(client);
    wh_client_arrange_clients_on_output(client->bound_output);

    wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
    wh_input_focus_client_under(&cursor_pos);
}

static void on_xdg_toplevel_unmap(struct wl_listener* listener, void*)
{
    WhaleClient* client = WH_CLIENT_FROM_XDG_LISTENER(listener, unmap);

    wh_client_unmap(client);
    wh_client_arrange_clients_on_output(client->bound_output);

    wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
    wh_input_focus_client_under(&cursor_pos);
}

static void on_xdg_toplevel_destroy(struct wl_listener* listener, void*)
{
    WhaleClient* client = WH_CLIENT_FROM_XDG_LISTENER(listener, destroy);
    WhaleXDGClientData* xdg_data = client->data;

    UNLISTEN(&xdg_data->listeners.map);
    UNLISTEN(&xdg_data->listeners.unmap);
    UNLISTEN(&xdg_data->listeners.commit);
    UNLISTEN(&xdg_data->listeners.destroy);
    UNLISTEN(&xdg_data->listeners.set_title);

    free(xdg_data);

    wh_client_destroy(client);
}

static void on_xdg_toplevel_new(struct wl_listener* listener, void* data)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.xdg_new_toplevel);

    struct wlr_xdg_toplevel* toplevel = data;

    /* A window's contents are represented using a scene tree. This is the thing
    that actually puts the window's contents on the root scene tree which in
    turn gets put on an output. */
    struct wlr_scene_tree* tree =
        wlr_scene_xdg_surface_create(&comp->root_scene->tree, toplevel->base);
    if (!tree)
    {
        wh_log(FATAL, "Failed to create scene tree.");
        exit(1);
    }

    WhaleClient* client = wh_client_new(tree);
    if (!client)
    {
        wlr_scene_node_destroy(&tree->node);
        wh_log(FATAL, "Failed to allocate whale client.");
        exit(1);
    }

    WhaleXDGClientData* xdg_data = calloc(1, sizeof(WhaleXDGClientData));
    if (!xdg_data)
    {
        wh_client_destroy(client);
        wh_log(FATAL, "Failed to allocate xdg data.");
        exit(1);
    }

    client->data = xdg_data;

    xdg_data->base = client;
    xdg_data->toplevel = toplevel;
    /* The xdg surface can point back to the client */
    xdg_data->toplevel->base->data = client;

    client->methods.set_size = xdg_set_size;
    client->methods.get_size = xdg_get_size;
    client->methods.get_parent = xdg_get_parent;
    client->methods.send_close = xdg_send_close;
    client->methods.get_wlr_surface = xdg_get_wlr_surface;
    client->methods.get_internal_geometry = xdg_get_internal_geometry;

    LISTEN(
        &xdg_data->toplevel->base->surface->events.map,
        &xdg_data->listeners.map,
        on_xdg_toplevel_map
    );

    LISTEN(
        &xdg_data->toplevel->base->surface->events.unmap,
        &xdg_data->listeners.unmap,
        on_xdg_toplevel_unmap
    );

    LISTEN(
        &xdg_data->toplevel->base->surface->events.commit,
        &xdg_data->listeners.commit,
        on_xdg_toplevel_commit
    );

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

static void
on_xdg_toplevel_decoration_request_mode(struct wl_listener* listener, void*)
{
    WhaleClient* client =
        WH_CLIENT_FROM_XDG_LISTENER(listener, decoration_request_mode);

    /* Ignore requested decoration modes, turn them off instead. */
    xdg_set_decoration_mode(client);
}

static void
on_xdg_toplevel_decoration_destroy(struct wl_listener* listener, void*)
{
    WhaleClient* client =
        WH_CLIENT_FROM_XDG_LISTENER(listener, decoration_destroy);

    WhaleXDGClientData* xdg_data = client->data;
    WH_ASSERT_DEBUG(xdg_data);

    xdg_data->toplevel_decoration = nullptr;
    UNLISTEN(&xdg_data->listeners.decoration_request_mode);
    UNLISTEN(&xdg_data->listeners.decoration_destroy);
}

static void on_xdg_toplevel_decoration_new(struct wl_listener*, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* decoration = data;

    WhaleClient* client = wh_client_from_xdg_toplevel(decoration->toplevel);

    WhaleXDGClientData* xdg_data = client->data;
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
}

int wh_client_xdg_shell_init(WhaleCompositor* comp)
{
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