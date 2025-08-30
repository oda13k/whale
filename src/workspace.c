
#include <stdlib.h>
#include <whale/client/client.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/output.h>
#include <whale/workspace.h>

static bool wh_client_is_implicit_floating(WhaleClient* client)
{
    WhaleSize2D min_size, max_size;
    wh_surface_get_minmax_size(&min_size, &max_size, client->surface);

    bool demands_size = min_size.w && max_size.h &&
                        (min_size.w == max_size.w || min_size.h == max_size.h);

    return wh_client_get_parent(client) || demands_size;
}

void wh_workspace_init(WhaleOutput* parent_output, WhaleWorkspace* ws)
{
    VEC_INIT(&ws->clients);
    wh_workspace_set_layout(LAYOUT_TILING, ws);

    ws->parent_output = parent_output;

    /* Defaults for tiling */
    ws->tiling_ctx.master_client_count = 1;
    ws->tiling_ctx.master_split_factor = 0.5;
}

int wh_workspace_set_layout(WhaleLayout layout, WhaleWorkspace* ws)
{
    if (ws->default_layout == layout)
        return 0;

    ws->default_layout = layout;
    VEC_FOR_EACH (client, &ws->clients)
        wh_workspace_set_client_layout(layout, *client);

    return 0;
}

int wh_workspace_init_client_layout(WhaleClient* client)
{
    if (!client->workspace)
    {
        client->layout = LAYOUT_TILING;
        return 0;
    }

    /* This is the only place where we check if a client is
    implcitely floating and adjust it accordingly. Can a client
    switch to being implicitely floating throughout it's lifetime? */
    wh_workspace_set_client_layout(
        wh_client_is_implicit_floating(client)
            ? LAYOUT_FLOATING
            : client->workspace->default_layout,
        client
    );
    return 0;
}

int wh_workspace_set_client_layout(WhaleLayout new_layout, WhaleClient* client)
{
    if (!client->workspace)
    {
        client->layout = LAYOUT_TILING;
        return -1;
    }

    if (client->layout == new_layout)
        return 0;

    client->layout = new_layout;
    return 0;
}

int wh_workspace_bind_client(WhaleClient* client, WhaleWorkspace* workspace)
{
    /* The client should not be bound to any workspace */
    WH_ASSERT(!client->workspace);

    /* Random sanity check: The new workspace should not already include this
     * client */
    bool includes = false;
    VEC_INCLUDES(client, includes, &workspace->clients);
    WH_ASSERT_SANITY(!includes);

    client->workspace = workspace;
    VEC_PUSH(client, &workspace->clients);
    return 0;
}

WhaleWorkspace* wh_workspace_unbind_client(WhaleClient* client)
{
    WhaleWorkspace* ws = client->workspace;
    if (!ws)
        return nullptr;

    /* Sanity check: this bounding workspace must actually include this client
     */
    bool includes = false;
    VEC_INCLUDES(client, includes, &ws->clients);
    WH_ASSERT(includes);

    /* Remove the client from the bound workspace */
    VEC_REMOVE(client, &ws->clients);
    client->workspace = nullptr;

    return ws;
}

static void wh_client_arrange_floating(WhaleClient* client)
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

static void wh_client_arrange_tiled(
    size_t tile_order,
    size_t tiled_clients_on_output,
    WhaleLayoutTilingContext* ctx,
    WhaleClient* client
)
{
    if (!client->workspace)
        return;

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

void wh_workspace_arrange(WhaleWorkspace* ws)
{
    size_t tiled_clients_on_ws = 0;
    VEC_FOR_EACH (client, &ws->clients)
        tiled_clients_on_ws +=
            ((*client)->layout == LAYOUT_TILING &&
             wh_client_is_mapped(*client));

    size_t tile_order = 0;
    VEC_FOR_EACH_REVERSE(client, &ws->clients)
    {
        if (!wh_client_is_mapped(*client))
            continue;

        switch ((*client)->layout)
        {
        case LAYOUT_TILING:
            wh_client_arrange_tiled(
                tile_order++, tiled_clients_on_ws, &ws->tiling_ctx, *client
            );
            break;

        case LAYOUT_FLOATING:
            wh_client_arrange_floating(*client);
            break;

        default:
            unreachable();
        }
    }
}

void wh_workspace_tiling_increment_master_split(float step, WhaleWorkspace* ws)
{
    if ((ws->tiling_ctx.master_split_factor += step) > 0.8f)
        ws->tiling_ctx.master_split_factor = 0.8f;
}

void wh_workspace_tiling_decrement_master_split(float step, WhaleWorkspace* ws)
{
    if ((ws->tiling_ctx.master_split_factor -= step) < 0.2f)
        ws->tiling_ctx.master_split_factor = 0.2f;
}

void wh_workspace_tiling_increment_master_max_clients(
    u8 step, WhaleWorkspace* ws
)
{
    // FIXME: possible (unprobable) overflow.
    ws->tiling_ctx.master_client_count += step;
}

void wh_workspace_tiling_decrement_master_max_clients(
    u8 step, WhaleWorkspace* ws
)
{
    if (step > ws->tiling_ctx.master_client_count)
        return;

    ws->tiling_ctx.master_client_count -= step;
}
