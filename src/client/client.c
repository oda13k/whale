
#include "xdg_shell.h"
#include "xwayland.h"
#include <stdlib.h>
#include <whale/client/client.h>
#include <whale/compositor.h>
#include <whale/debug.h>
#include <whale/input/keyboard.h>
#include <whale/input/pointer.h>
#include <whale/log.h>
#include <whale/output/output.h>
#include <whale/output/scene.h>
#include <whale/output/workspace.h>
#include <whale/types.h>
#include <whale/utils/math.h>
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

static void client_on_map(WhaleSurface* surface)
{
    WhaleClient* client = wh_client_from_surface(surface);

    if (!client->workspace)
    {
        /* We bind the client to a workspace on map. When the client gets
         * unmapped however, we don't unbind it from the workspace. Unbinding is
         * only done when the client is destroyed (or if it's workspace is
         * destroyed because the workspace's output was disconnected.) */
        WhaleOutput* output = wh_output_get_focused();
        if (output)
            wh_workspace_bind_client(client, output->active_workspace);
    }

    if (client->driver->map)
        client->driver->map(client);

    client->mappable = true;

    if (WH_LAYER_NEEDS_REARRANGE(client->layer) && client->workspace)
        wh_workspace_arrange(client->workspace);

    wh_client_map(client);
    wh_client_raise_to_top(client);
}

static void client_on_unmap(WhaleSurface* surface)
{
    WhaleClient* client = wh_client_from_surface(surface);

    client->mappable = false;

    if (WH_LAYER_NEEDS_REARRANGE(client->layer) && client->workspace)
        wh_workspace_arrange(client->workspace);

    wh_client_unmap(client);
}

static void client_on_commit(WhaleSurface* surface)
{
    WhaleClient* client = wh_client_from_surface(surface);

    if (client->driver->commit)
        client->driver->commit(client);
}

WhaleClient* wh_client_new(const WhaleClientDriver* driver, void* driver_ctx)
{
    WhaleClient* client = calloc(1, sizeof(WhaleClient));
    if (!client)
    {
        wh_log(ERR, "client: Failed to allocate client.");
        return nullptr;
    }

    /* Attach the client to the root scene tree. */
    client->scene_tree = wh_scene_tree_new();
    if (!client->scene_tree)
    {
        wh_log(ERR, "client: Failed to create scene tree.");
        free(client);
        return nullptr;
    }

    WH_ASSERT_SANITY(driver->set_tiled);
    WH_ASSERT_SANITY(driver->set_active);
    WH_ASSERT_SANITY(driver->set_size);
    WH_ASSERT_SANITY(driver->get_minmax_size);
    WH_ASSERT_SANITY(driver->close);
    client->driver = driver;
    client->driver_ctx = driver_ctx;

    /* Keep track of this client */
    VEC_PUSH(client, &g_clients);

    return client;
}

int wh_client_attach_surface(
    struct wlr_surface* wlr_surface, WhaleClient* client
)
{
    WH_ASSERT_SANITY(!client->surface);

    client->surface = wh_surface_new(wlr_surface, client->scene_tree);
    if (!client->surface)
    {
        wh_log(ERR, "client: Failed to allocate surface.");
        return -1;
    }

    client->surface->type = SURFACE_TYPE_CLIENT;
    client->surface->data = client;

    client->surface->map = client_on_map;
    client->surface->unmap = client_on_unmap;
    client->surface->commit = client_on_commit;

    return 0;
}

void wh_client_detach_surface(WhaleClient* client)
{
    WH_ASSERT_SANITY(client->surface);

    /* FIXME: callbacks registered with wh_surface_register_* are destroy here.
     * Maybe have them be explicitely destroyed? */
    wh_surface_destroy(client->surface);
    client->surface = nullptr;
}

void wh_client_destroy(WhaleClient* client)
{
    VEC_REMOVE(client, &g_clients);

    wh_workspace_unbind_client(client);

    if (client->surface)
        wh_client_detach_surface(client);

    wlr_scene_node_destroy(&client->scene_tree->node);

    free(client);
}

void wh_client_map(WhaleClient* client)
{
    if (!client->mappable)
        return;

    wlr_scene_node_set_enabled(&client->scene_tree->node, true);
}

void wh_client_unmap(WhaleClient* client)
{
    wlr_scene_node_set_enabled(&client->scene_tree->node, false);
}

bool wh_client_is_mapped(WhaleClient* client)
{
    return client->scene_tree->node.enabled;
}

void wh_client_set_pos(const WhalePosition2D* pos, WhaleClient* client)
{
    if (pos->x == client->scene_tree->node.x &&
        pos->y == client->scene_tree->node.y)
        return;

    wlr_scene_node_set_position(
        &client->scene_tree->node,
        CAST_COORD_TO_INT(pos->x),
        CAST_COORD_TO_INT(pos->y)
    );

    if (client->surface)
        wh_surface_invalidate_position(client->surface);
}

void wh_client_set_size(const WhaleSize2D* size, WhaleClient* client)
{
    client->size = *size;
    client->driver->set_size(size, client);
}

void wh_client_set_active(bool active, WhaleClient* client)
{
    client->driver->set_active(active, client);
}

void wh_client_configure(WhaleClient* client)
{
    if (client->driver->configure)
        client->driver->configure(client);
}

void wh_client_get_minmax_size(
    WhaleSize2D* min, WhaleSize2D* max, WhaleClient* client
)
{
    client->driver->get_minmax_size(min, max, client);

    if (min)
    {
        min->w = MAX2(min->w, 384);
        min->h = MAX2(min->h, 216);
    }
}

void wh_client_start_interactive(WhaleClient* client)
{
    client->interactive = true;

    wh_scene_tree_set_layer(client->scene_tree, WH_LAYER_INTERACTIVE);
}

void wh_client_drop_interactive(WhaleClient* client)
{
    client->interactive = false;

    wh_scene_tree_set_layer(client->scene_tree, client->layer);

    if (WH_LAYER_NEEDS_REARRANGE(client->layer) && client->workspace)
        wh_workspace_arrange(client->workspace);
}

void wh_client_set_layer(WhaleLayer layer, WhaleClient* client)
{
    if (layer == client->layer)
        return;

    client->prev_layer = client->layer;
    client->layer = layer;

    wh_scene_tree_set_layer(client->scene_tree, client->layer);

    client->driver->set_tiled(client->layer == WH_LAYER_TILING, client);

    if ((WH_LAYER_NEEDS_REARRANGE(client->prev_layer) ||
         WH_LAYER_NEEDS_REARRANGE(client->layer)) &&
        client->workspace && client->mappable)
        wh_workspace_arrange(client->workspace);
}

void wh_client_restore_prev_layer(WhaleClient* client)
{
    if (client->prev_layer != WH_LAYER_UNDEFINED)
        wh_client_set_layer(client->prev_layer, client);
    else if (client->workspace)
        wh_client_set_layer(client->workspace->default_layer, client);
}

void wh_client_raise_to_top(WhaleClient* client)
{
    wlr_scene_node_raise_to_top(&client->scene_tree->node);
}

void wh_client_lower_to_bottom(WhaleClient* client)
{
    wlr_scene_node_lower_to_bottom(&client->scene_tree->node);
}

void wh_client_get_geometry(WhaleGeometry2D* out_geom, WhaleClient* client)
{
    out_geom->size = client->size;
    out_geom->pos.x = client->scene_tree->node.x;
    out_geom->pos.y = client->scene_tree->node.y;
}

void wh_client_close(WhaleClient* client)
{
    client->driver->close(client);
}

WhaleClient* wh_client_from_surface(WhaleSurface* surface)
{
    while (surface->parent)
        surface = surface->parent;

    WH_ASSERT_SANITY(surface->type == SURFACE_TYPE_CLIENT);
    WH_ASSERT_SANITY(surface->data);
    return surface->data;
}
