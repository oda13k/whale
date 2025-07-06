
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

WhaleCompositor* g_comp;

static WhaleClient* wh_client_from_scene_node(const struct wlr_scene_node* node)
{
    return node->data;
}

static void wh_client_set_client_on_scene_nodes(
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
            wh_client_set_client_on_scene_nodes(
                wlr_scene_tree_from_node(node), client
            );
    }
}

WhaleGeometry2D wh_client_get_external_geometry(WhaleClient* client);

static void wh_client_arrange_floating(WhaleClient* client)
{
    if (!client->bound_output)
        return;

    WhaleGeometry2D bound_geom;

    WhaleClient* parent = wh_client_get_parent(client);
    if (parent)
    {
        /* If the client has a parent, we position it center
        relative to it's parent. */
        bound_geom = wh_client_get_external_geometry(parent);
    }
    else
    {
        /* If the client has no parent, we position it center
        relative to the output. */
        bound_geom = wh_output_get_geometry(client->bound_output);
    }

    wh_pos2d_t client_pos = wh_client_get_pos(client);
    wh_size2d_t client_size = wh_client_get_size(client);

    wh_pos2d_t target_pos = {
        .x = bound_geom.x + bound_geom.w / 2 - client_size.w / 2,
        .y = bound_geom.y + bound_geom.h / 2 - client_size.h / 2
    };

    if (client_pos.x != target_pos.x || client_pos.y != target_pos.y)
        wh_client_set_pos(&target_pos, client);
}

static float g_split_factor = 0.5;

static void wh_client_arrange_tiled(
    WhaleClient* client, size_t tile_order, size_t tiled_clients_on_output
)
{
    if (!client->bound_output)
        return;

    WhaleGeometry2D bounds = wh_output_get_geometry(client->bound_output);

    bool trig = (tile_order >= 1 && tiled_clients_on_output >= 3);

    wh_size2d_t size = {
        .w =
            (bounds.w * ((tile_order == 0 && tiled_clients_on_output == 1) +
                         (tiled_clients_on_output > 1) * g_split_factor)),
        .h = bounds.h / (1 + trig * (tiled_clients_on_output - 2))
    };

    wh_pos2d_t pos = {
        .x = bounds.x + (tile_order > 0) * size.w,
        .y = bounds.y + trig * size.h * (tile_order - 1)
    };

    wh_pos2d_t client_pos = wh_client_get_pos(client);
    wh_size2d_t client_size = wh_client_get_size(client);

    bool pos_changed = client_pos.x != pos.x || client_pos.y != pos.y;
    bool size_changed = client_size.w != size.w || client_size.h != size.h;

    if (pos_changed && size_changed)
        wh_client_set_pos_and_size_atomic(&pos, &size, client);
    else if (size_changed)
        wh_client_set_size(&size, client);
    else if (pos_changed)
        wh_client_set_pos(&pos, client);
}

void wh_client_arrange_clients_on_output(WhaleOutput* output)
{
    size_t tiled_clients_on_output = 0;
    VEC_FOR_EACH (client, &output->clients)
        tiled_clients_on_output +=
            ((*client)->arrangement == ARRANGE_TILED &&
             wh_client_is_mapped(*client));

    size_t i = 0;
    VEC_FOR_EACH_REVERSE(client, &output->clients)
    {
        if (!wh_client_is_mapped(*client))
            continue;

        switch ((*client)->arrangement)
        {
        case ARRANGE_TILED:
            wh_client_arrange_tiled(*client, i++, tiled_clients_on_output);
            break;

        case ARRANGE_FLOATING:
            wh_client_arrange_floating(*client);
            break;

        default:
            unreachable();
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

WhaleClient* wh_client_new(struct wlr_scene_tree* scene_tree)
{
    WhaleClient* client = calloc(1, sizeof(WhaleClient));
    if (!client)
        return nullptr;

    client->scene_tree = scene_tree;

    /* Unmap the client by default */
    wh_client_unmap(client);

    /* Make it so that the client is accessible from anywhere
    in the scene tree. */
    wh_client_set_client_on_scene_nodes(client->scene_tree, client);

    /* Bind the client to an output (if any) */
    wh_client_refresh_bounds(client);

    return client;
}

void wh_client_destroy(WhaleClient* client)
{
    /* Remove the client from it's bound output */
    VEC_REMOVE(client, &client->bound_output->clients);

    /* Remove the client from the scene */
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

int wh_client_refresh_bounds(WhaleClient* client)
{
    if (!client->bound_output)
    {
        /* This is either the first bound set, or we had (maybe still have) no
         outputs connected. Either way we'll try to make the output under the
         cursor the bounding output. */
        client->bound_output =
            wh_output_get_at(g_comp->cursor->x, g_comp->cursor->y, g_comp);

        if (!client->bound_output)
            client->bound_output = wh_output_get_default(g_comp);

        if (client->bound_output)
            VEC_PUSH(client, &client->bound_output->clients);
    }

    return 0;
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

bool wh_client_is_mapped(const WhaleClient* client)
{
    return client->scene_tree->node.enabled;
}

int wh_client_layout_to_client_coords(
    WhaleClient* client,
    const wh_pos2d_t* layout_coords,
    wh_pos2d_t* client_coords
)
{
    WhaleGeometry2D internal_geom = wh_client_get_internal_geometry(client);
    wh_pos2d_t pos = wh_client_get_pos(client);

    client_coords->x = layout_coords->x - (pos.x - internal_geom.x);
    client_coords->y = layout_coords->y - (pos.y - internal_geom.y);
    return 0;
}

void wh_client_set_pos(const wh_pos2d_t* pos, WhaleClient* client)
{
    wlr_scene_node_set_position(&client->scene_tree->node, pos->x, pos->y);
}

wh_pos2d_t wh_client_get_pos(WhaleClient* client)
{
    return (wh_pos2d_t){.x = client->scene_tree->node.x,
                        .y = client->scene_tree->node.y};
}

void wh_client_set_size(const wh_size2d_t* size, WhaleClient* client)
{
    WH_ASSERT_DEBUG(client->methods.set_size);
    client->methods.set_size(size, client);
}

wh_size2d_t wh_client_get_size(WhaleClient* client)
{
    WH_ASSERT_DEBUG(client->methods.get_size);
    return client->methods.get_size(client);
}

WhaleClient* wh_client_get_parent(WhaleClient* client)
{
    WH_ASSERT_DEBUG(client->methods.get_parent);
    return client->methods.get_parent(client);
}

void wh_client_send_close(WhaleClient* client)
{
    WH_ASSERT_DEBUG(client->methods.send_close);
    return client->methods.send_close(client);
}

struct wlr_surface* wh_client_get_wlr_surface(WhaleClient* client)
{
    WH_ASSERT_DEBUG(client->methods.get_wlr_surface);
    return client->methods.get_wlr_surface(client);
}

WhaleGeometry2D wh_client_get_internal_geometry(WhaleClient* client)
{
    WH_ASSERT_DEBUG(client->methods.get_internal_geometry);
    return client->methods.get_internal_geometry(client);
}

void wh_client_set_pos_and_size_atomic(
    const wh_pos2d_t* pos, const wh_size2d_t* size, WhaleClient* client
)
{
    TODO_LOG("implement set pos & size atomic");
    wh_client_set_pos(pos, client);
    wh_client_set_size(size, client);
}

WhaleGeometry2D wh_client_get_external_geometry(WhaleClient* client)
{
    wh_pos2d_t pos = wh_client_get_pos(client);
    wh_size2d_t size = wh_client_get_size(client);
    return (WhaleGeometry2D){.x = pos.x, .y = pos.y, .w = size.w, .h = size.h};
}
