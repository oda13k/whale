
#define WLR_USE_UNSTABLE
#include <signal.h>
#include <stdlib.h>
#include <whale/client/client.h>
#include <whale/client/xdg.h>
#include <whale/compositor.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/types.h>
#include <whale/utils.h>
#include <wlr/types/wlr_server_decoration.h>

#define CONTAINER_OF(_ptr, _sample_type, _member)                              \
    ((_sample_type*)((char*)(_ptr) - offsetof(_sample_type, _member)))

#define WH_SURFACE_FROM_SURFACE_LISTENER(_ptr, _listener_name)                 \
    (CONTAINER_OF(_ptr, WhaleSurface, listeners._listener_name))

static WhaleCompositor* g_comp;

static WhaleSurface*
wh_surface_from_scene_node(const struct wlr_scene_node* node)
{
    return node->data;
}

static void wh_surface_set_on_scene_nodes(
    struct wlr_scene_tree* tree, WhaleSurface* surface
)
{
    tree->node.data = surface;

    struct wlr_scene_node* node;
    wl_list_for_each(node, &tree->children, link)
    {
        if (!node)
            continue;

        node->data = surface;
        if (node->type == WLR_SCENE_NODE_TREE)
        {
            wh_surface_set_on_scene_nodes(
                wlr_scene_tree_from_node(node), surface
            );
        }
    }
}

int wh_client_ss_init(WhaleCompositor* comp)
{
    g_comp = comp;
    /* This protocol is obsolete, but untill it is removed,
    we'll support it. */
    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(g_comp->display),
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER
    );

    wh_client_xdg_shell_init(comp);

    return 0;
}

void wh_client_ss_destroy(WhaleCompositor* comp)
{
    wh_client_xdg_shell_destroy(comp);
}

static void on_surface_commit(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_SURFACE_LISTENER(listener, commit);

    surface->impl.surface_init(surface);
}

static void on_surface_map(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_SURFACE_LISTENER(listener, map);

    wh_surface_map(surface);

    wh_workspace_init_client_layout(surface->parent_client);
    wh_workspace_arrange(surface->parent_client->bound_workspace);

    wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
    wh_input_focus_surface_at_coords(&cursor_pos);
}

static void on_surface_unmap(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_SURFACE_LISTENER(listener, unmap);

    wh_surface_unmap(surface);

    wh_workspace_arrange(surface->parent_client->bound_workspace);

    wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
    wh_input_focus_surface_at_coords(&cursor_pos);
}

static void on_surface_new_subsurface(struct wl_listener* listener, void*) {}

static int wh_surface_init(
    struct wlr_surface* wlr_surface,
    const WhaleSurfaceImplementation* impl,
    struct wlr_scene_tree* parent_scene_tree,
    WhaleSurface* parent_surface,
    WhaleSurface* surface
)
{
    surface->wlr_surface = wlr_surface;
    surface->parent = parent_surface;

    /* Sanity checks */
    WH_ASSERT_DEBUG(impl->set_size);
    WH_ASSERT_DEBUG(impl->get_size);
    WH_ASSERT_DEBUG(impl->get_parent);
    WH_ASSERT_DEBUG(impl->send_close);
    WH_ASSERT_DEBUG(impl->get_internal_geometry);
    WH_ASSERT_DEBUG(impl->get_minmax_size);
    WH_ASSERT_DEBUG(impl->surface_init);
    surface->impl = *impl;

    surface->scene_surface_tree = wlr_scene_tree_create(parent_scene_tree);
    wlr_scene_surface_create(surface->scene_surface_tree, surface->wlr_surface);

    /* Make it so that the client is accessible from anywhere
    in the scene tree. */
    wh_surface_set_on_scene_nodes(surface->scene_surface_tree, surface);

    LISTEN(
        &surface->wlr_surface->events.commit,
        &surface->listeners.commit,
        on_surface_commit
    );

    LISTEN(
        &surface->wlr_surface->events.map,
        &surface->listeners.map,
        on_surface_map
    );

    LISTEN(
        &surface->wlr_surface->events.unmap,
        &surface->listeners.unmap,
        on_surface_unmap
    );

    LISTEN(
        &surface->wlr_surface->events.new_subsurface,
        &surface->listeners.new_subsurface,
        on_surface_new_subsurface
    );

    return 0;
}

static void wh_surface_destroy(WhaleSurface* surface)
{
    UNLISTEN(&surface->listeners.commit);
    UNLISTEN(&surface->listeners.map);
    UNLISTEN(&surface->listeners.unmap);
    UNLISTEN(&surface->listeners.new_subsurface);

    /* FIXME: recurse through children and destroy them as well. */
    /* FIXME: unlisten listeners */
    wlr_scene_node_destroy(&surface->scene_surface_tree->node);
}

WhaleClient* wh_client_new(
    struct wlr_surface* wlr_surface, const WhaleSurfaceImplementation* impl
)
{
    WhaleClient* client = calloc(1, sizeof(WhaleClient));
    if (!client)
        return nullptr;

    client->scene_tree = wlr_scene_tree_create(&g_comp->root_scene->tree);

    wh_surface_init(
        wlr_surface, impl, client->scene_tree, nullptr, &client->surface
    );

    client->surface.parent_client = client;

    /* Unmap the client by default */
    wh_surface_unmap(&client->surface);

    /* Bind the client to an output (if any) */
    wh_workspace_bind_client_auto(client);

    return client;
}

void wh_client_destroy(WhaleClient* client)
{
    /* We don't arrange the workspace here, it was already re-arranged when the
     * client was unmapped before getting destoryed. */
    wh_workspace_unbind_client(client);

    /* Remove the client from the scene */
    wh_surface_destroy(&client->surface);
    wlr_scene_node_destroy(&client->scene_tree->node);

    free(client);
}

void wh_surface_map(WhaleSurface* surface)
{
    wlr_scene_node_set_enabled(&surface->scene_surface_tree->node, true);
}

void wh_surface_unmap(WhaleSurface* surface)
{
    wlr_scene_node_set_enabled(&surface->scene_surface_tree->node, false);
}

WhaleSurface* wh_surface_get_at_coords(wh_coord_t x, wh_coord_t y)
{
    struct wlr_scene_node* node =
        wlr_scene_node_at(&g_comp->root_scene->tree.node, x, y, NULL, NULL);

    /* Not a client */
    if (!node || node->type != WLR_SCENE_NODE_BUFFER)
        return NULL;

    return wh_surface_from_scene_node(node);
}

bool wh_client_is_mapped(const WhaleClient* client)
{
    return client->scene_tree->node.enabled;
}

int wh_surface_layout_to_surface_coords(
    WhaleSurface* surface,
    const wh_pos2d_t* layout_coords,
    wh_pos2d_t* surface_coords
)
{
    // FIXME:
    WhaleGeometry2D internal_geom =
        wh_client_get_internal_geometry(surface->parent_client);
    wh_pos2d_t pos = wh_client_get_pos(surface->parent_client);

    surface_coords->x = layout_coords->x - (pos.x - internal_geom.x);
    surface_coords->y = layout_coords->y - (pos.y - internal_geom.y);
    return 0;
}

/* Utilities */
void wh_client_set_pos(const wh_pos2d_t* pos, WhaleClient* client)
{
    wlr_scene_node_set_position(&client->scene_tree->node, pos->x, pos->y);
}

wh_pos2d_t wh_client_get_pos(WhaleClient* client)
{
    return (wh_pos2d_t){.x = client->scene_tree->node.x,
                        .y = client->scene_tree->node.y};
}

void wh_surface_set_size(const wh_size2d_t* size, WhaleSurface* surface)
{
    surface->impl.set_size(size, surface);
}

wh_size2d_t wh_surface_get_size(WhaleSurface* surface)
{
    return surface->impl.get_size(surface);
}

WhaleClient* wh_client_get_parent(WhaleClient* client)
{
    return client->surface.impl.get_parent(client);
}

void wh_client_send_close(WhaleClient* client)
{
    return client->surface.impl.send_close(client);
}

WhaleGeometry2D wh_client_get_internal_geometry(WhaleClient* client)
{
    return client->surface.impl.get_internal_geometry(client);
}

void wh_client_set_pos_and_size_atomic(
    const wh_pos2d_t* pos, const wh_size2d_t* size, WhaleClient* client
)
{
    TODO_LOG("implement set pos & size atomic");
    wh_client_set_pos(pos, client);
    wh_surface_set_size(size, &client->surface);
}

WhaleGeometry2D wh_client_get_external_geometry(WhaleClient* client)
{
    wh_pos2d_t pos = wh_client_get_pos(client);
    wh_size2d_t size = wh_surface_get_size(&client->surface);
    return (WhaleGeometry2D){.x = pos.x, .y = pos.y, .w = size.w, .h = size.h};
}

void wh_client_get_minmax_size(
    wh_size2d_t* min_size, wh_size2d_t* max_size, WhaleClient* client
)
{
    client->surface.impl.get_minmax_size(min_size, max_size, client);
}
