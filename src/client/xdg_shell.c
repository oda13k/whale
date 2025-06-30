
#include <stdlib.h>
#include <whale/client/client.h>
#include <whale/client/xdg_shell.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/types.h>
#include <whale/utils.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_shell.h>

static WhaleClient*
wh_client_from_xdg_toplevel(struct wlr_xdg_toplevel* toplevel)
{
    return toplevel->base->data;
}

static bool wh_client_is_implicit_floating(WhaleClient* client)
{
    struct wlr_xdg_toplevel* toplevel = client->xdg_toplevel;
    struct wlr_xdg_toplevel_state* state = &toplevel->current;

    /* If the client has a parent or if the client demands it's own
    size exactly, we treat it as floating. */
    bool demands_size = state->min_width && state->max_height &&
                        (state->min_width == state->max_width ||
                         state->min_height == state->max_height);

    return toplevel->parent || demands_size;
}

static void wh_client_set_decorations_server_side(WhaleClient* client)
{
    if (!client->xdg_toplevel->base->initialized)
        return;

    wlr_xdg_toplevel_decoration_v1_set_mode(
        client->xdg_decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
    );
}

static void on_xdg_toplevel_set_title(struct wl_listener* listener, void*)
{
    WhaleClient* client =
        wl_container_of(listener, client, listeners.set_title);

    wh_log(DEBUG, "client: title \"%s\"", client->xdg_toplevel->title);
}

static void on_xdg_toplevel_commit(struct wl_listener* listener, void*)
{
    WhaleClient* client = wl_container_of(listener, client, listeners.commit);

    if (client->xdg_toplevel->base->initial_commit)
    {
        if (client->xdg_decoration)
            wh_client_set_decorations_server_side(client);

        /* The surface needs to receive a configure in order to work. This
        will probably happen when it gets arranged, but on the off chance it
        doesn't, we force one here. */
        wlr_xdg_surface_schedule_configure(client->xdg_toplevel->base);
    }

    WhaleClientArrangement default_arrangement = ARRANGE_TILED;
    if (wh_client_is_implicit_floating(client))
    {
        client->arrangement = ARRANGE_FLOATING;
        wh_client_arrange_clients_on_output(client->bound_output);
    }
    else
    {
        // FIXME: keep old arrangement
        client->arrangement = default_arrangement;
    }

    wh_input_refocus_all_inputs_on_topmost_client_under_cursor(
        client->comp, NULL, NULL
    );
}

static void on_xdg_toplevel_map(struct wl_listener* listener, void*)
{
    WhaleClient* client = wl_container_of(listener, client, listeners.map);

    wlr_scene_node_set_enabled(&client->scene_tree->node, 1);
    wh_client_arrange_clients_on_output(client->bound_output);
    wh_input_refocus_all_inputs_on_topmost_client_under_cursor(
        client->comp, NULL, NULL
    );
}

static void on_xdg_toplevel_unmap(struct wl_listener* listener, void*)
{
    WhaleClient* client = wl_container_of(listener, client, listeners.unmap);

    wlr_scene_node_set_enabled(&client->scene_tree->node, 0);
    wh_client_arrange_clients_on_output(client->bound_output);
    wh_input_refocus_all_inputs_on_topmost_client_under_cursor(
        client->comp, NULL, NULL
    );
}

static void wh_tree_walk_and_set_client_data(
    struct wlr_scene_tree* tree, WhaleClient* client
)
{
    tree->node.data = client;

    struct wlr_scene_node* node;
    wl_list_for_each(node, &tree->children, link)
    {
        if (!node)
            continue;

        node->data = client;
        if (node->type == WLR_SCENE_NODE_TREE)
            wh_tree_walk_and_set_client_data(
                wlr_scene_tree_from_node(node), client
            );
    }
}

static void on_xdg_toplevel_destroy(struct wl_listener* listener, void*)
{
    WhaleClient* client = wl_container_of(listener, client, listeners.destroy);

    // wl_list_remove(&client->output_link);

    wlr_scene_node_destroy(&client->scene_tree->node);
    client->scene_tree = nullptr;

    UNLISTEN(&client->listeners.map);
    UNLISTEN(&client->listeners.unmap);
    UNLISTEN(&client->listeners.commit);
    UNLISTEN(&client->listeners.destroy);
    UNLISTEN(&client->listeners.set_title);

    free(client);
}

static void on_xdg_toplevel_new(struct wl_listener* listener, void* data)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.xdg_new_toplevel);

    struct wlr_xdg_toplevel* toplevel = data;

    WhaleClient* client = calloc(1, sizeof(WhaleClient));

    client->xdg_toplevel = toplevel;
    /* The client can point back to the compositor */
    client->comp = comp;
    /* The xdg surface can point back to the client */
    client->xdg_toplevel->base->data = client;
    /* The wlroots surface can point back to the client */
    client->xdg_toplevel->base->surface->data = client;

    /* A client is represented by a tree */
    client->scene_tree =
        wlr_scene_xdg_surface_create(&comp->root_scene->tree, toplevel->base);

    wh_tree_walk_and_set_client_data(client->scene_tree, client);

    wh_client_refresh_bounds(client);

    LISTEN(
        &toplevel->base->surface->events.map,
        &client->listeners.map,
        on_xdg_toplevel_map
    );

    LISTEN(
        &toplevel->base->surface->events.unmap,
        &client->listeners.unmap,
        on_xdg_toplevel_unmap
    );

    LISTEN(
        &toplevel->base->surface->events.commit,
        &client->listeners.commit,
        on_xdg_toplevel_commit
    );

    LISTEN(
        &toplevel->events.destroy,
        &client->listeners.destroy,
        on_xdg_toplevel_destroy
    );

    LISTEN(
        &toplevel->events.set_title,
        &client->listeners.set_title,
        on_xdg_toplevel_set_title
    );
}

static void
on_xdg_toplevel_decoration_request_mode(struct wl_listener* listener, void*)
{
    WhaleClient* client =
        wl_container_of(listener, client, listeners.decoration_request_mode);

    /* Ignore requested decoration modes, turn them off instead. */
    wh_client_set_decorations_server_side(client);
}

static void
on_xdg_toplevel_decoration_destroy(struct wl_listener* listener, void*)
{
    WhaleClient* client =
        wl_container_of(listener, client, listeners.decoration_destroy);

    client->xdg_decoration = nullptr;
    UNLISTEN(&client->listeners.decoration_request_mode);
    UNLISTEN(&client->listeners.decoration_destroy);
}

static void on_xdg_toplevel_decoration_new(struct wl_listener*, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* decoration = data;
    WhaleClient* client = wh_client_from_xdg_toplevel(decoration->toplevel);

    client->xdg_decoration = decoration;

    LISTEN(
        &decoration->events.request_mode,
        &client->listeners.decoration_request_mode,
        on_xdg_toplevel_decoration_request_mode
    );

    LISTEN(
        &decoration->events.destroy,
        &client->listeners.decoration_destroy,
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
     * The difference between popups and toplevels that we treat like popups is
     * the way they are positioned. While popups specify their own position
     * relative to their parent toplevel, popup-like toplevels' behavior can be
     * seen in wh_client_arrange_floating().
     */
    comp->xdg_shell = wlr_xdg_shell_create(comp->display, 6);
    LISTEN(
        &comp->xdg_shell->events.new_toplevel,
        &comp->listeners.xdg_new_toplevel,
        on_xdg_toplevel_new
    );

    /* This is the intended client decoration protocol. */
    comp->xdg_decoration_manager =
        wlr_xdg_decoration_manager_v1_create(comp->display);

    LISTEN(
        &comp->xdg_decoration_manager->events.new_toplevel_decoration,
        &comp->listeners.xdg_new_decoration,
        on_xdg_toplevel_decoration_new
    );

    return 0;
}

void wh_client_xdg_shell_destroy(WhaleCompositor* comp)
{
    comp->xdg_shell = nullptr;
    UNLISTEN(&comp->listeners.xdg_new_toplevel);

    comp->xdg_decoration_manager = nullptr;
    UNLISTEN(&comp->listeners.xdg_new_decoration);
}

bool wh_client_has_parent(WhaleClient* client)
{
    return client->xdg_toplevel->parent;
}

WhaleClient* wh_client_get_parent(WhaleClient* client)
{
    return wh_client_from_xdg_toplevel(client->xdg_toplevel->parent);
}

int wh_client_set_size(wh_size_t w, wh_size_t h, WhaleClient* client)
{
    wlr_xdg_toplevel_set_size(client->xdg_toplevel, w, h);
    return 0;
}

struct wlr_box* wh_client_get_geometry(WhaleClient* client)
{
    return &client->xdg_toplevel->base->geometry;
}

wh_size2d_t wh_client_get_size(WhaleClient* client)
{
    return (wh_size2d_t){.w = client->xdg_toplevel->base->geometry.width,
                         .h = client->xdg_toplevel->base->geometry.height};
}
