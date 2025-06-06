
#define WLR_USE_UNSTABLE
#include <stdlib.h>
#include <whale/client.h>
#include <whale/compositor.h>
#include <whale/log.h>
#include <whale/types.h>
#include <wlr/types/wlr_xdg_shell.h>

static struct wlr_box* wh_client_get_geometry(WhaleClient* client)
{
    return &client->xdg_toplevel->base->geometry;
}

static WhaleClient*
wh_client_from_xdg_toplevel(struct wlr_xdg_toplevel* toplevel)
{
    return toplevel->base->data;
}

static WhaleClient* wh_client_from_scene_node(const struct wlr_scene_node* node)
{
    return node->data;
}

static void wh_client_set_decorations_server_side(WhaleClient* client)
{
    if (!client->xdg_toplevel->base->initialized)
        return;

    wlr_xdg_toplevel_decoration_v1_set_mode(
        client->xdg_decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
    );
}

static void wh_client_on_surface_map(struct wl_listener* listener, void*)
{
    WhaleClient* client = wl_container_of(listener, client, listeners.map);

    wlr_scene_node_set_enabled(&client->scene_tree->node, 1);
}

static void wh_client_on_surface_unmap(struct wl_listener* listener, void*)
{
    WhaleClient* client = wl_container_of(listener, client, listeners.unmap);

    wlr_scene_node_set_enabled(&client->scene_tree->node, 0);
}

static void wh_client_on_surface_commit(struct wl_listener* listener, void*)
{
    WhaleClient* client = wl_container_of(listener, client, listeners.commit);

    if (client->xdg_toplevel->base->initial_commit)
    {
        if (client->xdg_decoration)
            wh_client_set_decorations_server_side(client);

        wlr_scene_node_set_position(&client->scene_tree->node, 0, 0);
        wlr_xdg_toplevel_set_size(client->xdg_toplevel, 0, 0);
        return;
    }

    struct wlr_box* geom = wh_client_get_geometry(client);
    /* the client's geometry includes whatever decorations the client has.
    We turn decorations off, but clients still position themselves as if they
    had decorations. */
    wlr_scene_node_set_position(&client->scene_tree->node, geom->x, geom->y);

    struct wlr_output* output = wlr_output_layout_output_at(
        client->comp->output_layout, geom->x, geom->y
    );

    if (geom->width != output->width || geom->height != output->height)
    {
        wlr_xdg_toplevel_set_size(
            client->xdg_toplevel, output->width, output->height
        );
    }
}

static void wh_client_on_destroy(struct wl_listener* listener, void*)
{
    WhaleClient* client = wl_container_of(listener, client, listeners.destroy);

    wlr_scene_node_destroy(&client->scene_tree->node);

    UNLISTEN(&client->listeners.map);
    UNLISTEN(&client->listeners.unmap);
    UNLISTEN(&client->listeners.commit);
    UNLISTEN(&client->listeners.destroy);
    UNLISTEN(&client->listeners.set_title);

    free(client);
}

static void wh_client_on_set_title(struct wl_listener* listener, void*)
{
    WhaleClient* client =
        wl_container_of(listener, client, listeners.set_title);

    wh_log(DEBUG, "client: title \"%s\"", client->xdg_toplevel->title);
}

static void wh_tree_walk_and_set_client_data(struct wlr_scene_tree* tree, WhaleClient* client)
{
    tree->node.data = client;

    struct wlr_scene_node* node;
    wl_list_for_each(node, &tree->children, link)
    {
        if (!node)
            continue;

        node->data = client;
        if (node->type == WLR_SCENE_NODE_TREE)
            wh_tree_walk_and_set_client_data(wlr_scene_tree_from_node(node), client);
    }
}

void wh_client_on_new_client(struct wl_listener* listener, void* data)
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

    /* Create a new scene-tree for this client containing it and it's
    sub-surfaces and add it to the root scene. */
    client->scene_tree =
        wlr_scene_xdg_surface_create(&comp->root_scene->tree, toplevel->base);

    wh_tree_walk_and_set_client_data(client->scene_tree, client);

    LISTEN(
        &toplevel->base->surface->events.map,
        &client->listeners.map,
        wh_client_on_surface_map
    );

    LISTEN(
        &toplevel->base->surface->events.unmap,
        &client->listeners.unmap,
        wh_client_on_surface_unmap
    );

    LISTEN(
        &toplevel->base->surface->events.commit,
        &client->listeners.commit,
        wh_client_on_surface_commit
    );

    LISTEN(
        &toplevel->events.destroy,
        &client->listeners.destroy,
        wh_client_on_destroy
    );

    LISTEN(
        &toplevel->events.set_title,
        &client->listeners.set_title,
        wh_client_on_set_title
    );

    wh_log(DEBUG, "client: new");
}

static void on_decoration_request_mode(struct wl_listener* listener, void*)
{
    WhaleClient* client =
        wl_container_of(listener, client, listeners.decoration_request_mode);

    /* Ignore requested decoration modes, turn them off instead. */
    wh_client_set_decorations_server_side(client);
}

static void on_decoration_destroy(struct wl_listener* listener, void*)
{
    WhaleClient* client =
        wl_container_of(listener, client, listeners.decoration_destroy);

    wl_list_remove(&client->listeners.decoration_destroy.link);
    wl_list_remove(&client->listeners.decoration_request_mode.link);
    client->xdg_decoration = NULL;
}

void wh_client_on_new_xdg_decoration(struct wl_listener*, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* decoration = data;
    WhaleClient* client = wh_client_from_xdg_toplevel(decoration->toplevel);

    client->xdg_decoration = decoration;

    LISTEN(
        &decoration->events.request_mode,
        &client->listeners.decoration_request_mode,
        on_decoration_request_mode
    );

    LISTEN(
        &decoration->events.destroy,
        &client->listeners.decoration_destroy,
        on_decoration_destroy
    );
}

WhaleClient*
wh_client_get_at_coords(wh_coord_t x, wh_coord_t y, const WhaleCompositor* comp)
{
    struct wlr_scene_node* node =
        wlr_scene_node_at(&comp->root_scene->tree.node, x, y, NULL, NULL);

    /* Not a client */
    if (!node || node->type != WLR_SCENE_NODE_BUFFER)
        return NULL;

    return wh_client_from_scene_node(node);
}
