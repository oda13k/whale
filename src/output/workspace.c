
#include <stdlib.h>
#include <whale/client/client.h>
#include <whale/debug.h>
#include <whale/input/pointer.h>
#include <whale/log.h>
#include <whale/output/output.h>
#include <whale/output/workspace.h>
#include <whale/utils/math.h>
#include <whale/utils/vector.h>

typedef struct
{
    float primary_split;
    float primary_min_split;
    float sec_min_split;
    size_t primary_client_count;
    size_t client_count;
    WhaleTilingOrientation orientation;
    const WhaleGeometry2D* bounds;
} TilingPassContext;

int wh_workspace_init(WhaleOutput* parent_output, WhaleWorkspace* ws)
{
    if (VEC_INIT(&ws->clients) < 0)
        return -1;

    ws->parent_output = parent_output;

    ws->default_layer = WH_LAYER_TILING;

    /* Defaults */
    ws->tiling_config.primary_client_count = 1;
    ws->tiling_config.primary_split =
        1.f - 1.f / (ws->tiling_config.primary_client_count + 1);
    ws->tiling_config.primary_min_split = 0.15;
    ws->tiling_config.primary_max_split = 0.85;
    ws->tiling_config.orientation = HORIZONTAL;
    ws->tiling_config.dynamic_orientation = true;
    ws->tiling_config.respect_client_min_size = true;

    return 0;
}

void wh_workspace_destroy(WhaleWorkspace* ws)
{
    VEC_DESTROY(&ws->clients);
}

int wh_workspace_bind_client(WhaleClient* client, WhaleWorkspace* ws)
{
    WH_ASSERT_SANITY(!client->workspace);
    WH_ASSERT_SANITY(!VEC_INCLUDES(client, &ws->clients));

    if (VEC_PUSH(client, &ws->clients) < 0)
        return -1;

    client->workspace = ws;

    if (client->layer == WH_LAYER_UNDEFINED)
        wh_client_set_layer(ws->default_layer, client);

    if (wh_client_is_mapped(client) && WH_LAYER_NEEDS_REARRANGE(client->layer))
        wh_workspace_arrange(ws);

    return 0;
}

WhaleWorkspace* wh_workspace_unbind_client(WhaleClient* client)
{
    WhaleWorkspace* ws = client->workspace;
    if (!ws)
        return nullptr;

    WH_ASSERT_SANITY(VEC_INCLUDES(client, &ws->clients));

    /* Remove the client from the bound workspace */
    VEC_REMOVE(client, &ws->clients);
    client->workspace = nullptr;

    if (wh_client_is_mapped(client) && WH_LAYER_NEEDS_REARRANGE(client->layer))
        wh_workspace_arrange(ws);

    return ws;
}

void wh_workspace_activate(WhaleWorkspace* workspace)
{
    VEC_FOR_EACH (client, &workspace->clients)
        wh_client_map(*client);

    wh_pointer_focus_update();
}

void wh_workspace_deactivate(WhaleWorkspace* workspace)
{
    bool refocus = false;

    VEC_FOR_EACH (client, &workspace->clients)
    {
        WhaleLayer layer = (*client)->layer;

        bool ignore = layer == WH_LAYER_BG || layer == WH_LAYER_NOTIFICATION ||
                      layer == WH_LAYER_POINTER || layer == WH_LAYER_OVERLAY;

        if (ignore)
            continue;

        wh_client_unmap(*client);

        if ((*client)->mappable)
            refocus =
                refocus || wh_pointer_focus_lost_surface((*client)->surface);
    }

    if (refocus)
        wh_pointer_focus_update();
}

static void client_arrange_tiled(
    TilingPassContext* ctx,
    WhaleClient* client,
    size_t client_idx
)
{
    const WhaleGeometry2D* bounds = ctx->bounds;
    WhaleGeometry2D geom;
    bool flip = ctx->orientation == HORIZONTAL;

    /* Why is this written like this? Because I just wanted to do some
     * branchless programming for no real reason. This might just be slower than
     * if did normally. */
    if (client_idx < ctx->primary_client_count)
    {
        bool have_sec = ctx->client_count > ctx->primary_client_count;
        float size_factor = (ctx->primary_split * have_sec + !have_sec) /
                            (ctx->primary_client_count * have_sec +
                             ctx->client_count * !have_sec);

        geom.size.w = roundf(bounds->size.w * (size_factor * flip + !flip));
        geom.size.h = roundf(bounds->size.h * (size_factor * !flip + flip));
        geom.pos.x = bounds->pos.x + flip * (geom.size.w * client_idx);
        geom.pos.y = bounds->pos.y + !flip * (geom.size.h * client_idx);
    }
    else
    {
        u8 sec_order = client_idx - ctx->primary_client_count;
        float splitf = (ctx->primary_client_count > 0) * ctx->primary_split;
        float splitf_comp = (1.f - splitf);
        float sec_clients_inv =
            1.f / (ctx->client_count - ctx->primary_client_count);

        geom.size.w = roundf(
            bounds->size.w * (splitf_comp * flip + sec_clients_inv * !flip)
        );
        geom.size.h = roundf(
            bounds->size.h * (sec_clients_inv * flip + splitf_comp * !flip)
        );
        geom.pos.x = bounds->pos.x + roundf(bounds->size.w * splitf) * flip +
                     geom.size.w * sec_order * !flip;
        geom.pos.y = bounds->pos.y + (geom.size.h * sec_order) * flip +
                     roundf(bounds->size.h * splitf) * !flip;
    }

    wh_client_set_pos(&geom.pos, client);
    wh_client_set_size(&geom.size, client);
    wh_client_lower_to_bottom(client);
}

static void client_arrange_fullscreen(WhaleClient* client)
{
    WhaleGeometry2D bounds;
    wh_output_get_geometry(&bounds, client->workspace->parent_output);

    wh_client_set_pos(&bounds.pos, client);
    wh_client_set_size(&bounds.size, client);
}

static void
workspace_compute_tiling_ctx(WhaleWorkspace* ws, TilingPassContext* ctx)
{
    const WhaleWorkspaceTilingConfig* conf = &ws->tiling_config;
    const WhaleGeometry2D* bounds = ctx->bounds;

    WhaleTilingOrientation orientation;
    if (!conf->dynamic_orientation)
        orientation = conf->orientation;
    else
        orientation = bounds->size.h > bounds->size.w ? VERTICAL : HORIZONTAL;

    size_t client_count = 0;
    float primary_min_split;
    float sec_min_split;

    if (conf->respect_client_min_size)
    {
        wh_dim_t primary_min_size = 0;
        wh_dim_t sec_min_size = 0;

        VEC_FOR_EACH_REVERSE(client, &ws->clients)
        {
            if ((*client)->layer != WH_LAYER_TILING || !(*client)->mappable)
                continue;

            WhaleSize2D minsize;
            wh_client_get_minmax_size(&minsize, nullptr, *client);

            wh_dim_t size = orientation == VERTICAL ? minsize.h : minsize.w;
            if (client_count < conf->primary_client_count)
                primary_min_size = MAX2(primary_min_size, size);
            else
                sec_min_size = MAX2(sec_min_size, size);

            ++client_count;
        }

        wh_dim_t tmp =
            orientation == VERTICAL ? bounds->size.h : bounds->size.w;

        primary_min_split = (float)primary_min_size / tmp;
        sec_min_split = (float)sec_min_size / tmp;
        primary_min_split = MIN2(primary_min_split, conf->primary_max_split);
        sec_min_split = MIN2(sec_min_split, 1 - conf->primary_min_split);

        /* The primary section has min-size priority */
        if (primary_min_split + sec_min_split > 1)
            sec_min_split = 1 - primary_min_split;
    }
    else
    {
        VEC_FOR_EACH_REVERSE(client, &ws->clients)
        {
            if ((*client)->layer == WH_LAYER_TILING && (*client)->mappable)
                ++client_count;
        }

        primary_min_split = conf->primary_min_split;
        sec_min_split = 1 - conf->primary_max_split;
    }

    ctx->primary_split =
        CLAMP(primary_min_split, conf->primary_split, 1 - sec_min_split);
    ctx->primary_min_split = primary_min_split;
    ctx->sec_min_split = sec_min_split;
    ctx->primary_client_count = conf->primary_client_count;
    ctx->client_count = client_count;
    ctx->orientation = orientation;
}

void wh_workspace_arrange(WhaleWorkspace* ws)
{
    WhaleGeometry2D bounds;
    wh_output_get_geometry(&bounds, ws->parent_output);

    TilingPassContext tiling_ctx;
    tiling_ctx.bounds = &bounds;
    workspace_compute_tiling_ctx(ws, &tiling_ctx);

    size_t tile_idx = 0;
    VEC_FOR_EACH_REVERSE(client, &ws->clients)
    {
        if (!(*client)->mappable)
            continue;

        WhaleGeometry2D geom;
        if ((*client)->interactive)
            wh_client_get_geometry(&geom, *client);

        switch ((*client)->layer)
        {
        case WH_LAYER_FLOATING:
        case WH_LAYER_OVERLAY:
            break;

        case WH_LAYER_TILING:
            client_arrange_tiled(&tiling_ctx, *client, tile_idx++);
            break;

        case WH_LAYER_FULLSCREEN:
            client_arrange_fullscreen(*client);
            break;

        case WH_LAYER_BG:
            WH_ASSERT_SANITY(false);

        case WH_LAYER_COUNT:
        case WH_LAYER_UNDEFINED:
        default:
            WH_ASSERT_NOT_REACHED();
        }

        if ((*client)->interactive)
            wh_client_set_pos(&geom.pos, *client);
    }
}

void wh_workspace_step_tiling_primary_split(float step, WhaleWorkspace* ws)
{
    WhaleGeometry2D bounds;
    wh_output_get_geometry(&bounds, ws->parent_output);

    TilingPassContext tiling_ctx;
    tiling_ctx.bounds = &bounds;
    workspace_compute_tiling_ctx(ws, &tiling_ctx);

    float lb =
        MAX2(tiling_ctx.primary_min_split, ws->tiling_config.primary_min_split);
    float ub =
        MIN2(1 - tiling_ctx.sec_min_split, ws->tiling_config.primary_max_split);

    ws->tiling_config.primary_split =
        CLAMP(lb, tiling_ctx.primary_split + step, ub);

    wh_workspace_arrange(ws);
}

void wh_workspace_step_tiling_master_client_count(s8 step, WhaleWorkspace* ws)
{
    u8 old_client_count = ws->tiling_config.primary_client_count;
    u8 new_client_count = old_client_count + step;

    /* Over/underflow checks */
    if (step < 0 && new_client_count > old_client_count)
        new_client_count = 0;
    else if (step > 0 && new_client_count < old_client_count)
        new_client_count = 255;

    ws->tiling_config.primary_client_count = new_client_count;

    wh_workspace_arrange(ws);
}
