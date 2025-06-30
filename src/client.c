
#define WLR_USE_UNSTABLE
#include <signal.h>
#include <stdlib.h>
#include <whale/client/client.h>
#include <whale/client/xdg_shell.h>
#include <whale/compositor.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/types.h>
#include <whale/utils.h>
#include <wlr/types/wlr_server_decoration.h>

static WhaleClient* wh_client_from_scene_node(const struct wlr_scene_node* node)
{
    return node->data;
}

static void wh_client_arrange_floating(WhaleClient* client)
{
    if (!client->bound_output)
        return;

    struct wlr_box bound_geom;
    if (wh_client_has_parent(client))
    {
        /* If the client has a parent, we position it center
        relative to it's parent. */
        bound_geom = *wh_client_get_geometry(wh_client_get_parent(client));
    }
    else
    {
        /* If the client has no parent, we position it center
        relative to the output. */
        bound_geom = wh_output_get_geometry(client->bound_output);
    }

    wh_pos2d_t client_pos = wh_client_get_pos(client);
    wh_size2d_t client_size = wh_client_get_size(client);

    wh_coord_t target_x =
        (bound_geom.x + bound_geom.width) / 2 - (client_size.w / 2);
    wh_coord_t target_y =
        (bound_geom.y + bound_geom.height) / 2 - (client_size.h / 2);

    if (client_pos.x != target_x || client_pos.y != target_y)
        wh_client_set_pos(target_x, target_y, client);
}

static float g_split_factor = 0.5;

static void wh_client_arrange_tiled(
    WhaleClient* client, size_t tile_order, size_t tiled_clients_on_output
)
{
    if (!client->bound_output)
        return;

    struct wlr_box bounds = wh_output_get_geometry(client->bound_output);

    bool trig = (tile_order >= 1 && tiled_clients_on_output >= 3);

    wh_coord_t w =
        (bounds.width * ((tile_order == 0 && tiled_clients_on_output == 1) +
                         (tiled_clients_on_output > 1) * g_split_factor));

    wh_coord_t x = bounds.x + (tile_order > 0) * w;

    wh_coord_t h = bounds.height / (1 + trig * (tiled_clients_on_output - 2));
    wh_coord_t y = bounds.y + trig * h * (tile_order - 1);

    wh_pos2d_t client_pos = wh_client_get_pos(client);
    if (client_pos.x != x || client_pos.y != y)
        wh_client_set_pos(x, y, client);

    wh_size2d_t client_size = wh_client_get_size(client);
    if (client_size.w != w || client_size.h != h)
        wh_client_set_size(w, h, client);
}

void wh_client_arrange_clients_on_output(WhaleOutput* output)
{
    size_t tiled_clients_on_output = 0;
    VEC_FOR_EACH(client, &output->clients)
        tiled_clients_on_output +=
        ((*client)->arrangement == ARRANGE_TILED &&
        wh_client_is_mapped(*client));

    size_t i = 0;
    VEC_FOR_EACH(client, &output->clients)
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

int wh_client_subsystem_init(WhaleCompositor* comp)
{
    /* This protocol is obsolete, but untill it is removed,
    we'll support it. */
    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(comp->display),
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER
    );

    wh_client_xdg_shell_init(comp);

    return 0;
}

void wh_client_ss_destroy(WhaleCompositor* comp)
{
    wh_client_xdg_shell_destroy(comp);
}

int wh_client_refresh_bounds(WhaleClient* client)
{
    WhaleCompositor* comp = client->comp;

    if (!client->bound_output)
    {
        /* This is either the first bound set, or we had (maybe still have) no
         outputs connected. Either way we'll try to make the output under the
         cursor the bounding output. */
        client->bound_output =
            wh_output_get_at(comp->cursor->x, comp->cursor->y, comp);

        if (!client->bound_output)
            client->bound_output = wh_output_get_default(comp);

        if (client->bound_output)
            VEC_PUSH(client, &client->bound_output->clients);
    }

    return 0;
}

int wh_client_refresh_all_client_bounds(WhaleCompositor*)
{
    // WhaleClient* client;
    // wl_list_for_each(client, &comp->clients, clients_link)
    // {
    //     wh_client_refresh_bounds(client);
    //     wh_client_arrange(client);
    // }

    return 0;
}

void wh_client_set_pos(wh_coord_t x, wh_coord_t y, WhaleClient* client)
{
    wlr_scene_node_set_position(&client->scene_tree->node, x, y);
}

wh_pos2d_t wh_client_get_pos(WhaleClient* client)
{
    return (wh_pos2d_t){.x = client->scene_tree->node.x,
                        .y = client->scene_tree->node.y};
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

WhaleClient* wh_client_from_wlr_surface(struct wlr_surface* surface)
{
    return surface->data;
}

int wh_client_sigterm(WhaleClient* client)
{
    wlr_xdg_toplevel_send_close(client->xdg_toplevel);
    return 0;
}
