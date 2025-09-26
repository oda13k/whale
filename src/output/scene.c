
#include <whale/compositor.h>
#include <whale/log.h>
#include <whale/output/scene.h>
#include <wlr/types/wlr_scene.h>

static struct wlr_scene* g_root_scene;
static struct wlr_scene_tree* g_scene_layer_trees[WH_LAYER_COUNT];

int wh_scene_init()
{
    g_root_scene = wlr_scene_create();
    if (!g_root_scene)
    {
        wh_log(ERR, "scene: Failed to create root scene.");
        return -1;
    }

    for (size_t i = 0; i < WH_LAYER_COUNT; ++i)
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
        &g_scene_layer_trees[WH_LAYER_UNDEFINED]->node, false
    );

    return 0;
}

int wh_scene_attach_output(WhaleOutput* output)
{
    output->scene_output =
        wlr_scene_output_create(g_root_scene, output->wlr_output);

    if (!output->scene_output)
        return -1;

    return 0;
}

int wh_scene_detach_output(WhaleOutput* output)
{
    wlr_scene_output_destroy(output->scene_output);
    output->scene_output = nullptr;

    return 0;
}

struct wlr_scene_tree* wh_scene_tree_new()
{
    return wlr_scene_tree_create(g_scene_layer_trees[WH_LAYER_UNDEFINED]);
}

void wh_scene_tree_set_layer(struct wlr_scene_tree* tree, WhaleLayer layer)
{
    wlr_scene_node_reparent(&tree->node, g_scene_layer_trees[layer]);
}

WhaleSurface* wh_scene_get_topmost_surface_at(const WhalePosition2D* pos)
{
    struct wlr_scene_node* node =
        wlr_scene_node_at(&g_root_scene->tree.node, pos->x, pos->y, NULL, NULL);

    if (!node || node->type != WLR_SCENE_NODE_BUFFER)
        return nullptr;

    return node->data;
}
