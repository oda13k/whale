
#define WLR_USE_UNSTABLE
#include "xdg_shell.h"
#include <signal.h>
#include <stdlib.h>
#include <whale/client/client.h>
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

static VEC(WhaleClient*) g_clients;

int wh_client_ss_init(WhaleCompositor* comp)
{
    g_comp = comp;

    VEC_INIT(&g_clients);

    /* This protocol is obsolete, but until it is removed,
    we'll support it. */
    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(g_comp->display),
        WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT
    );

    wh_client_xdg_shell_init(comp);

    return 0;
}

void wh_client_ss_destroy()
{
    wh_client_xdg_shell_destroy();
}

WhaleClient* wh_client_new(struct wlr_surface* wlr_surface)
{
    WhaleClient* client = calloc(1, sizeof(WhaleClient));
    if (!client)
    {
        wh_log(ERR, "client: Failed to allocate client.");
        return nullptr;
    }

    /* Attach the client to the root scene tree. */
    client->scene_tree = wlr_scene_tree_create(&g_comp->root_scene->tree);
    if (!client->scene_tree)
    {
        wh_log(ERR, "client: Failed to allocate scene tree.");
        free(client);
        return nullptr;
    }

    client->surface = wh_surface_new(wlr_surface, client->scene_tree);
    if (!client->surface)
    {
        wh_log(ERR, "client: Failed to allocate surface.");
        wlr_scene_node_destroy(&client->scene_tree->node);
        free(client);
        return nullptr;
    }

    client->surface->type = SURFACE_TYPE_CLIENT;
    client->surface->data = client;

    /* Unmap the client by default */
    wh_client_unmap(client);

    /* Bind the client to an output (if any) */
    wh_workspace_bind_client_auto(client);

    /* Keep track of this client */
    VEC_PUSH(client, &g_clients);

    return client;
}

void wh_client_destroy(WhaleClient* client)
{
    VEC_REMOVE(client, &g_clients);

    /* We don't arrange the workspace here, it was already re-arranged when the
     * client was unmapped before getting destoryed. */
    wh_workspace_unbind_client(client);

    wh_surface_destroy(client->surface);

    wlr_scene_node_destroy(&client->scene_tree->node);

    free(client);
}

void wh_client_map(WhaleClient* client)
{
    wlr_scene_node_set_enabled(&client->scene_tree->node, true);
}

void wh_client_unmap(WhaleClient* client)
{
    wlr_scene_node_set_enabled(&client->scene_tree->node, false);
}

bool wh_client_is_mapped(const WhaleClient* client)
{
    return client->scene_tree->node.enabled;
}

/* Utilities */
void wh_client_set_pos(const WhalePosition2D* pos, WhaleClient* client)
{
    // FIXME: this is the client node, but we map/unmap the surface node.
    wlr_scene_node_set_position(&client->scene_tree->node, pos->x, pos->y);
}

void wh_client_get_pos(WhalePosition2D* out_pos, WhaleClient* client)
{
    out_pos->x = client->scene_tree->node.x;
    out_pos->y = client->scene_tree->node.y;
}

void wh_client_get_geometry(WhaleGeometry2D* out_geom, WhaleClient* client)
{
    client->surface->driver.get_size(&out_geom->size, client->surface);
    wh_client_get_pos(&out_geom->pos, client);
}

void wh_client_set_active(bool active, WhaleClient* client)
{
    WH_ASSERT_SANITY(client->driver.set_active);
    client->driver.set_active(active, client);
}

WhaleClient* wh_client_from_surface(WhaleSurface* surface)
{
    WhaleSurface* topmost_surface = wh_surface_get_topmost_parent(surface);
    WH_ASSERT_SANITY(topmost_surface->type == SURFACE_TYPE_CLIENT);
    WH_ASSERT_SANITY(topmost_surface->data);
    return topmost_surface->data;
}
