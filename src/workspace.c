
#include <stdlib.h>
#include <whale/client/client.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/output.h>
#include <whale/workspace.h>

static bool wh_client_is_implicit_floating(WhaleClient* client)
{
    wh_size2d_t min_size, max_size;
    wh_client_get_minmax_size(&min_size, &max_size, client);

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
    if (!client->bound_workspace)
    {
        client->layout = LAYOUT_UNDEFINED;
        return 0;
    }

    if (client->layout != LAYOUT_UNDEFINED)
        return 0;

    /* This is the only place where we check if a client is
    implcitely floating and adjust it accordingly. Can a client
    switch to being implicitely floating throughout it's lifetime? */
    wh_workspace_set_client_layout(
        wh_client_is_implicit_floating(client)
            ? LAYOUT_FLOATING
            : client->bound_workspace->default_layout,
        client
    );
    return 0;
}

int wh_workspace_set_client_layout(WhaleLayout new_layout, WhaleClient* client)
{
    if (!client->bound_workspace)
    {
        client->layout = LAYOUT_UNDEFINED;
        return -1;
    }

    if (client->layout == new_layout)
        return 0;

    client->layout = new_layout;
    return 0;
}

int wh_workspace_bind_client_auto(WhaleClient* client)
{
    WH_ASSERT_DEBUG(!client->bound_workspace);

    /* This is either the first bound set, or we had (maybe still have) no
     outputs connected. Either way we'll try to make the output under the
     cursor the bounding output. */
    wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
    WhaleOutput* output = wh_output_get_at(&cursor_pos);
    if (!output)
        output = wh_output_get_main();

    client->bound_workspace = wh_output_get_active_workspace(output);
    VEC_PUSH(client, &client->bound_workspace->clients);

    return 0;
}

int wh_workspace_bind_client(WhaleClient* client, WhaleWorkspace* workspace)
{
    /* The client should not be bound to any workspace */
    WH_ASSERT(!client->bound_workspace);

    /* Random sanity check: The new workspace should not already include this
     * client */
    bool includes = false;
    VEC_INCLUDES(client, includes, &workspace->clients);
    WH_ASSERT_DEBUG(!includes);

    client->bound_workspace = workspace;
    VEC_PUSH(client, &workspace->clients);
    return 0;
}

WhaleWorkspace* wh_workspace_unbind_client(WhaleClient* client)
{
    WhaleWorkspace* ws = client->bound_workspace;
    if (!ws)
        return nullptr;

    /* Sanity check: this bounding workspace must actually include this client
     */
    bool includes = false;
    VEC_INCLUDES(client, includes, &ws->clients);
    WH_ASSERT(includes);

    /* Sanity check: We should only be unbinding from a focused workspace. */
    WH_ASSERT(wh_output_get_active_workspace(ws->parent_output) == ws);

    /* Remove the client from the bound workspace */
    VEC_REMOVE(client, &ws->clients);
    client->bound_workspace = nullptr;

    // wh_client_unmap(client);
    TODO_LOG("unbding client from workspace");

    return ws;
}

static void wh_client_arrange_floating(WhaleClient* client)
{
    if (!client->bound_workspace)
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
        bound_geom =
            wh_output_get_geometry(client->bound_workspace->parent_output);
    }

    wh_pos2d_t cur_pos = wh_client_get_pos(client);
    wh_size2d_t cur_size = wh_surface_get_size(&client->surface);

    wh_pos2d_t target_pos = {
        .x = bound_geom.x + bound_geom.w / 2 - cur_size.w / 2,
        .y = bound_geom.y + bound_geom.h / 2 - cur_size.h / 2
    };

    if (cur_pos.x != target_pos.x || cur_pos.y != target_pos.y)
        wh_client_set_pos(&target_pos, client);
}

static void wh_client_arrange_tiled(
    size_t tile_order,
    size_t tiled_clients_on_output,
    WhaleLayoutTilingContext* ctx,
    WhaleClient* client
)
{
    if (!client->bound_workspace)
        return;

    WhaleGeometry2D bounds =
        wh_output_get_geometry(client->bound_workspace->parent_output);

    wh_pos2d_t pos;
    wh_size2d_t size;

    /* FIXME: horrible math, there mest be a cleaner way of doing this */
    if (tile_order < ctx->master_client_count)
    {
        size.w = roundf(
            bounds.w *
            (tiled_clients_on_output > ctx->master_client_count
                 ? ctx->master_split_factor
                 : 1) /
            (float)(tiled_clients_on_output > ctx->master_client_count
                        ? ctx->master_client_count
                        : tiled_clients_on_output)
        );
        size.h = bounds.h;

        pos.x = bounds.x + size.w * tile_order;
        pos.y = bounds.y;
    }
    else
    {
        const float secondary_clients =
            tiled_clients_on_output - ctx->master_client_count;

        const float split_factor =
            (ctx->master_client_count > 0) * ctx->master_split_factor;

        size.w = roundf(bounds.w * (1 - split_factor));
        size.h = roundf(bounds.h / secondary_clients);

        pos.x = bounds.x + roundf(bounds.w * split_factor);
        pos.y = bounds.y + size.h * (tile_order - ctx->master_client_count);
    }

    wh_pos2d_t cur_pos = wh_client_get_pos(client);
    wh_size2d_t cur_size = wh_surface_get_size(&client->surface);

    bool pos_changed = cur_pos.x != pos.x || cur_pos.y != pos.y;
    bool size_changed = cur_size.w != size.w || cur_size.h != size.h;

    if (pos_changed && size_changed)
        wh_client_set_pos_and_size_atomic(&pos, &size, client);
    else if (size_changed)
        wh_surface_set_size(&size, &client->surface);
    else if (pos_changed)
        wh_client_set_pos(&pos, client);
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
