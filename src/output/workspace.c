
#include <stdlib.h>
#include <whale/client/client.h>
#include <whale/debug.h>
#include <whale/log.h>
#include <whale/output/output.h>
#include <whale/output/workspace.h>
#include <whale/utils/vector.h>

static bool wh_client_is_implicit_floating(WhaleClient* client)
{
    WhaleSize2D min_size, max_size;
    wh_surface_get_minmax_size(&min_size, &max_size, client->surface);

    bool demands_size = min_size.w && max_size.h &&
                        (min_size.w == max_size.w || min_size.h == max_size.h);

    return wh_client_get_parent(client) || demands_size;
}

int wh_workspace_init(WhaleOutput* parent_output, WhaleWorkspace* ws)
{
    if (VEC_INIT(&ws->clients) < 0)
        return -1;

    ws->default_layer = WH_CLIENT_LAYER_TILING;

    ws->parent_output = parent_output;

    /* Defaults for tiling */
    ws->tiling_ctx.master_client_count = 2;
    ws->tiling_ctx.master_split_factor =
        1.f - 1.f / (ws->tiling_ctx.master_client_count + 1);

    return 0;
}

void wh_workspace_destroy(WhaleWorkspace* ws)
{
    VEC_DESTROY(&ws->clients);
}

int wh_workspace_bind_client(WhaleClient* client, WhaleWorkspace* workspace)
{
    /* The client should not be bound to any workspace */
    WH_ASSERT_SANITY(!client->workspace);

    /* The new workspace should not already include this client */
    WH_ASSERT_SANITY(!VEC_INCLUDES(client, &workspace->clients));

    if (VEC_PUSH(client, &workspace->clients) < 0)
        return -1;

    client->workspace = workspace;

    /* If it's the first bind we'll also set the client's layout. */
    if (client->layer == WH_CLIENT_LAYER_UNDEFINED)
    {
        WhaleClientLayer layout;
        if (wh_client_is_implicit_floating(client))
            layout = WH_CLIENT_LAYER_FLOATING;
        else
            layout = workspace->default_layer;

        wh_client_set_layer(layout, client);
    }

    return 0;
}

WhaleWorkspace* wh_workspace_unbind_client(WhaleClient* client)
{
    WhaleWorkspace* ws = client->workspace;
    if (!ws)
        return nullptr;

    /* Sanity check: this bounding workspace must actually include this client
     */
    WH_ASSERT_SANITY(VEC_INCLUDES(client, &ws->clients));

    /* Remove the client from the bound workspace */
    VEC_REMOVE(client, &ws->clients);
    client->workspace = nullptr;

    return ws;
}

static void client_arrange_floating(WhaleClient* client)
{
    if (!client->workspace)
        return;

    WhaleClient* parent = wh_client_get_parent(client);
    WhaleGeometry2D bound_geom;
    if (parent)
    {
        /* If the client has a parent, we position it center
        relative to it's parent. */
        wh_client_get_geometry(&bound_geom, parent);
    }
    else
    {
        /* If the client has no parent, we position it center
        relative to the output. */
        wh_output_get_geometry(&bound_geom, client->workspace->parent_output);
    }

    WhaleGeometry2D cur_geom;
    wh_client_get_geometry(&cur_geom, client);

    WhalePosition2D new_pos = {
        .x = bound_geom.pos.x + bound_geom.size.w / 2 - cur_geom.size.w / 2,
        .y = bound_geom.pos.y + bound_geom.size.h / 2 - cur_geom.size.h / 2
    };

    if (cur_geom.pos.x != new_pos.x || cur_geom.pos.y != new_pos.y)
        wh_client_set_pos(&new_pos, client);

    // TODO: set the client to it's min size if is floating to fix the weird
    // gimp bug for the color picker where the size is correct but the elements
    // inside the window are arragned as if the window had another size.
}

static void client_arrange_tiled(
    size_t tile_order,
    size_t tiled_clients_on_output,
    WhaleWorkspaceTilingContext* ctx,
    WhaleClient* client
)
{
    WhaleGeometry2D bounds;
    wh_output_get_geometry(&bounds, client->workspace->parent_output);

    WhaleGeometry2D new_geom;

    /* FIXME: horrible math, there mest be a cleaner way of doing this */
    if (tile_order < ctx->master_client_count)
    {
        new_geom.size.w = roundf(
            bounds.size.w *
            (tiled_clients_on_output > ctx->master_client_count
                 ? ctx->master_split_factor
                 : 1) /
            (float)(tiled_clients_on_output > ctx->master_client_count
                        ? ctx->master_client_count
                        : tiled_clients_on_output)
        );
        new_geom.size.h = bounds.size.h;

        new_geom.pos.x = bounds.pos.x + new_geom.size.w * tile_order;
        new_geom.pos.y = bounds.pos.y;
    }
    else
    {
        const float secondary_clients =
            tiled_clients_on_output - ctx->master_client_count;

        const float split_factor =
            (ctx->master_client_count > 0) * ctx->master_split_factor;

        new_geom.size.w = roundf(bounds.size.w * (1 - split_factor));
        new_geom.size.h = roundf(bounds.size.h / secondary_clients);

        new_geom.pos.x = bounds.pos.x + roundf(bounds.size.w * split_factor);
        new_geom.pos.y =
            bounds.pos.y +
            new_geom.size.h * (tile_order - ctx->master_client_count);
    }

    WhaleGeometry2D cur_geom;
    wh_client_get_geometry(&cur_geom, client);

    bool pos_changed =
        cur_geom.pos.x != new_geom.pos.x || cur_geom.pos.y != new_geom.pos.y;
    bool size_changed = cur_geom.size.w != new_geom.size.w ||
                        cur_geom.size.h != new_geom.size.h;

    if (size_changed)
        wh_surface_set_size(&new_geom.size, client->surface);

    if (pos_changed)
        wh_client_set_pos(&new_geom.pos, client);
}

static void client_arrange_fullscreen(WhaleClient* client)
{
    WhaleGeometry2D bounds;
    wh_output_get_geometry(&bounds, client->workspace->parent_output);

    WhaleGeometry2D client_geom;
    wh_client_get_geometry(&client_geom, client);

    bool pos_changed =
        bounds.pos.x != client_geom.pos.x || bounds.pos.y != client_geom.pos.y;
    bool size_changed = bounds.size.w != client_geom.size.w ||
                        bounds.size.h != client_geom.size.h;

    if (pos_changed)
        wh_client_set_pos(&bounds.pos, client);

    if (size_changed)
        wh_client_set_size(&bounds.size, client);
}

void wh_workspace_arrange(WhaleWorkspace* ws)
{
    size_t tiled_clients_on_ws = 0;

    VEC_FOR_EACH (client, &ws->clients)
    {
        tiled_clients_on_ws +=
            ((*client)->layer == WH_CLIENT_LAYER_TILING &&
             (*client)->requested_map);
    }

    size_t tile_order = 0;
    VEC_FOR_EACH_REVERSE(client, &ws->clients)
    {
        if (!(*client)->requested_map)
            continue;

        switch ((*client)->layer)
        {
        case WH_CLIENT_LAYER_TILING:
            client_arrange_tiled(
                tile_order++, tiled_clients_on_ws, &ws->tiling_ctx, *client
            );
            break;

        case WH_CLIENT_LAYER_FLOATING:
            client_arrange_floating(*client);
            break;

        case WH_CLIENT_LAYER_FULLSCREEN:
            client_arrange_fullscreen(*client);
            break;

        case WH_CLIENT_LAYER_BG:
        case WH_CLIENT_LAYER_OVERLAY:
            WH_ASSERT_SANITY(false);

        case WH_CLIENT_LAYER_COUNT:
        case WH_CLIENT_LAYER_UNDEFINED:
        default:
            WH_ASSERT_NOT_REACHED();
        }
    }
}

void wh_workspace_step_tiling_master_split_factor(
    float step, WhaleWorkspace* ws
)
{
    float factor = ws->tiling_ctx.master_split_factor + step;

    if (factor > 0.8f)
        factor = 0.8f;
    else if (factor < 0.2f)
        factor = 0.2f;

    ws->tiling_ctx.master_split_factor = factor;
}

void wh_workspace_step_tiling_master_client_count(s8 step, WhaleWorkspace* ws)
{
    u8 old_client_count = ws->tiling_ctx.master_client_count;
    u8 new_client_count = old_client_count + step;

    /* Over/underflow checks */
    if (step < 0 && new_client_count > old_client_count)
        new_client_count = 0;
    else if (step > 0 && new_client_count < old_client_count)
        new_client_count = 255;

    ws->tiling_ctx.master_client_count = new_client_count;
}
