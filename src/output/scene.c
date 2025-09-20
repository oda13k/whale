
#include <whale/compositor.h>
#include <whale/log.h>
#include <whale/output/scene.h>
#include <wlr/types/wlr_scene.h>

static struct wlr_scene* g_root_scene;
static struct wlr_scene_tree* g_scene_layer_trees[WH_CLIENT_LAYER_COUNT];
static struct wlr_output_layout* g_output_layout;

WH_CALLBACK(output_layout_change, struct wl_listener*, void*)
{
    struct wlr_output_layout_output* output_layout_output;
    wl_list_for_each(output_layout_output, &g_output_layout->outputs, link)
    {
        WhaleOutput* output = output_layout_output->output->data;
        wh_workspace_arrange(output->active_workspace);
    }
}

int wh_scene_init()
{
    g_root_scene = wlr_scene_create();
    if (!g_root_scene)
    {
        wh_log(ERR, "scene: Failed to create root scene.");
        return -1;
    }

    g_output_layout = wlr_output_layout_create(wh_compositor_get_wl_display());
    if (!g_output_layout)
    {
        wh_log(ERR, "scene: Failed to create output layout.");
        TODO_LOG("scene: How do you destroy a wlr_scene????");
        return -1;
    }

    for (size_t i = 0; i < WH_CLIENT_LAYER_COUNT; ++i)
    {
        g_scene_layer_trees[i] = wlr_scene_tree_create(&g_root_scene->tree);
        if (!g_scene_layer_trees[i])
        {
            // FIXME?: Memory leaks
            wh_log(ERR, "scene: Failed to allocate a scene layer tree.");
            return -1;
        }
    }

    wlr_scene_node_set_enabled(
        &g_scene_layer_trees[WH_CLIENT_LAYER_UNDEFINED]->node, false
    );

    WH_LISTEN(&g_output_layout->events.change, output_layout_change);

    return 0;
}

int wh_scene_attach_output(WhaleOutput* output, u32 x, u32 y)
{
    output->scene_output =
        wlr_scene_output_create(g_root_scene, output->wlr_output);

    if (!output->scene_output)
        return -1;

    wlr_output_layout_add(g_output_layout, output->wlr_output, x, y);
    wlr_scene_output_set_position(output->scene_output, x, y);
    return 0;
}

int wh_scene_detach_output(WhaleOutput* output)
{
    wlr_output_layout_remove(g_output_layout, output->wlr_output);

    wlr_scene_output_destroy(output->scene_output);
    output->scene_output = nullptr;

    return 0;
}

struct wlr_scene_tree* wh_scene_tree_new()
{
    return wlr_scene_tree_create(
        g_scene_layer_trees[WH_CLIENT_LAYER_UNDEFINED]
    );
}

void wh_scene_tree_set_layer(
    struct wlr_scene_tree* tree, WhaleClientLayer layer
)
{
    wlr_scene_node_reparent(&tree->node, g_scene_layer_trees[layer]);
}

int wh_scene_get_output_position(
    const WhaleOutput* output, WhalePosition2D* out_pos
)
{
    struct wlr_box output_geom = {0};
    wlr_output_layout_get_box(
        g_output_layout, output->wlr_output, &output_geom
    );

    if (output_geom.width == 0 && output_geom.height == 0 &&
        output_geom.x == 0 && output_geom.y == 0)
        return -1;

    out_pos->x = output_geom.x;
    out_pos->y = output_geom.y;
    return 0;
}

WhaleOutput* wh_scene_get_output_at(WhalePosition2D* pos)
{
    struct wlr_output* wlr_output =
        wlr_output_layout_output_at(g_output_layout, pos->x, pos->y);

    if (!wlr_output)
        return NULL;

    return wlr_output->data;
}

int wh_scene_attach_pointer(struct wlr_cursor* pointer)
{
    wlr_cursor_attach_output_layout(pointer, g_output_layout);
    return 0;
}

WhaleSurface* wh_scene_get_topmost_surface_at(const WhalePosition2D* pos)
{
    struct wlr_scene_node* node =
        wlr_scene_node_at(&g_root_scene->tree.node, pos->x, pos->y, NULL, NULL);

    if (!node || node->type != WLR_SCENE_NODE_BUFFER)
        return nullptr;

    return node->data;
}
