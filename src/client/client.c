
#include "xdg_shell.h"
#include "xwayland.h"
#include <stdlib.h>
#include <whale/client/client.h>
#include <whale/compositor.h>
#include <whale/debug.h>
#include <whale/input/pointer.h>
#include <whale/input/seat.h>
#include <whale/log.h>
#include <whale/output/output.h>
#include <whale/output/scene.h>
#include <whale/output/workspace.h>
#include <whale/types.h>
#include <wlr/types/wlr_server_decoration.h>

// TODO: maybe move this into scene.c
static VEC(WhaleClient*) g_clients;

int wh_client_ss_init()
{
    VEC_INIT(&g_clients);

    /* This protocol is obsolete, but until it is removed,
    we'll support it. */
    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(wh_compositor_get_wl_display()),
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER
    );

    wh_client_xdg_shell_init();

    wh_xwayland_init();

    return 0;
}

WH_SURFACE_CALLBACK(client_on_map, surface)
{
    WhaleClient* client = wh_client_from_surface(surface);

    client->requested_map = true;

    if (!client->workspace)
    {
        /* We bind the client to a workspace on map. When the client gets
         * unmapped however, we don't unbind it from the workspace. Unbinding is
         * only done when the client is destroyed (or if it's workspace is
         * destroyed because the workspace's output was disconnected.) */
        WhaleOutput* output = wh_output_get_focused();
        if (output)
        {
            wh_workspace_bind_client(
                client, wh_output_get_active_workspace(output)
            );
        }
    }

    if (client->workspace)
        wh_workspace_arrange(client->workspace);

    wh_client_map(client);
    wh_seat_refocus_input(false);
    return WHALE_SURFACE_CALLBACK_OK;
}

WH_SURFACE_CALLBACK(client_on_unmap, surface)
{
    WhaleClient* client = wh_client_from_surface(surface);

    wh_client_unmap(client);
    client->requested_map = false;

    if (client->workspace)
        wh_workspace_arrange(client->workspace);

    wh_seat_refocus_input(true);
    return WHALE_SURFACE_CALLBACK_OK;
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
    client->scene_tree = wlr_scene_tree_create(wh_scene_get_root_scene_tree());
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

    wh_surface_register_map_cb(client_on_map, client->surface);
    wh_surface_register_unmap_cb(client_on_unmap, client->surface);

    /* Unmap the client by default */
    wh_client_unmap(client);

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

void wh_client_set_pos(const WhalePosition2D* pos, WhaleClient* client)
{
    wlr_scene_node_set_position(&client->scene_tree->node, pos->x, pos->y);
}

void wh_client_set_size(const WhaleSize2D* size, WhaleClient* client)
{
    wh_surface_set_size(size, client->surface);
}

void wh_client_set_active(bool active, WhaleClient* client)
{
    WH_ASSERT_SANITY(client->driver.set_active);
    client->driver.set_active(active, client);
}

void wh_client_set_layout(WhaleLayout layout, WhaleClient* client)
{
    client->prev_layout = client->layout;
    client->layout = layout;
}

void wh_client_raise_to_top(WhaleClient* client)
{
    wlr_scene_node_raise_to_top(&client->scene_tree->node);
}

void wh_client_get_geometry(WhaleGeometry2D* out_geom, WhaleClient* client)
{
    client->surface->driver.get_size(&out_geom->size, client->surface);
    out_geom->pos.x = client->scene_tree->node.x;
    out_geom->pos.y = client->scene_tree->node.y;
}

WhaleClient* wh_client_get_parent(WhaleClient* client)
{
    WH_ASSERT_SANITY(client->driver.get_parent);
    return client->driver.get_parent(client);
}

void wh_client_close(WhaleClient* client)
{
    WH_ASSERT_SANITY(client->driver.close);
    client->driver.close(client);
}

WhaleClient* wh_client_from_surface(WhaleSurface* surface)
{
    while (surface->parent)
        surface = surface->parent;

    WH_ASSERT_SANITY(surface->type == SURFACE_TYPE_CLIENT);
    WH_ASSERT_SANITY(surface->data);
    return surface->data;
}
