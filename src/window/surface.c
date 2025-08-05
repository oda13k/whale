
#define WLR_USE_UNSTABLE
#include <stdlib.h>
#include <whale/compositor.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/window/surface.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>

#define CONTAINER_OF(_ptr, _sample_type, _member)                              \
    ((_sample_type*)((char*)(_ptr) - offsetof(_sample_type, _member)))

#define WH_SURFACE_FROM_LISTENER(_ptr, _listener_name)                         \
    (CONTAINER_OF(_ptr, WhaleSurface, listeners._listener_name))

extern WhaleCompositor g_comp;

static WhaleSurface*
wh_surface_from_scene_node(const struct wlr_scene_node* node)
{
    return node->data;
}

static void wh_surface_set_on_scene_nodes(
    struct wlr_scene_tree* tree, WhaleSurface* surface
)
{
    tree->node.data = surface;

    struct wlr_scene_node* node;
    wl_list_for_each(node, &tree->children, link)
    {
        if (!node)
            continue;

        node->data = surface;
        if (node->type == WLR_SCENE_NODE_TREE)
        {
            wh_surface_set_on_scene_nodes(
                wlr_scene_tree_from_node(node), surface
            );
        }
    }
}

static void on_surface_commit(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_LISTENER(listener, commit);

    VEC_FOR_EACH (cb, &surface->callbacks.commit_callbacks)
    {
        if ((*cb)(surface) == WHALE_SURFACE_CALLBACK_REMOVE_SELF)
        {
            VEC_REMOVE(*cb, &surface->callbacks.commit_callbacks);
            --cb;
        }
    }
}

static void on_surface_map(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_LISTENER(listener, map);

    wh_surface_map(surface);

    // wh_workspace_init_client_layout(surface->parent_client);
    // wh_workspace_arrange(surface->parent_client->bound_workspace);

    // wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
    // wh_input_focus_surface_at_coords(&cursor_pos);
}

static void on_surface_unmap(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_LISTENER(listener, unmap);

    wh_surface_unmap(surface);

    wh_pos2d_t cursor_pos = wh_input_get_cursor_pos();
    wh_input_focus_surface_at_coords(&cursor_pos);
}

static void on_surface_new_subsurface(struct wl_listener*, void* data)
{
    struct wlr_subsurface* subsurface = data;

    WhaleSurface* parent_surface = subsurface->parent->data;

    WhaleSurface* surface =
        wh_surface_new(subsurface->surface, parent_surface->scene_surface_tree);

    surface->focus_type =
        SURFACE_FOCUS_POINTER | SURFACE_FOCUS_KEYBOARD_TOPMOST;

    wlr_scene_node_set_position(
        &surface->scene_surface_tree->node,
        subsurface->current.x,
        subsurface->current.y
    );

    surface->parent = parent_surface;
    VEC_PUSH(surface, &parent_surface->children);
}

void wh_surface_destroy(WhaleSurface* surface)
{
    VEC_FOR_EACH (child, &surface->children)
    {
        (*child)->parent = nullptr;
        wh_surface_destroy(*child);
        VEC_REMOVE(*child, &surface->children);
        --child;
    }

    VEC_DESTROY(&surface->children);

    if (surface->parent)
    {
        VEC_REMOVE(surface, &surface->parent->children);
    }

    UNLISTEN(&surface->listeners.commit);
    UNLISTEN(&surface->listeners.map);
    UNLISTEN(&surface->listeners.unmap);
    UNLISTEN(&surface->listeners.new_subsurface);
    UNLISTEN(&surface->listeners.destroy);

    /* FIXME: recurse through children and destroy them as well. */
    /* FIXME: unlisten listeners */
    // wlr_scene_node_destroy(&surface->scene_surface_tree->node);

    VEC_DESTROY(&surface->callbacks.commit_callbacks);
    VEC_DESTROY(&surface->callbacks.map_callbacks);
    VEC_DESTROY(&surface->callbacks.unmap_callbacks);

    free(surface);
}

void on_surface_destroy(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_LISTENER(listener, destroy);
    wh_surface_destroy(surface);
}

WhaleSurface* wh_surface_new(
    struct wlr_surface* wlr_surface, struct wlr_scene_tree* parent_tree
)
{
    WhaleSurface* surface = calloc(1, sizeof(WhaleSurface));
    if (!surface)
        return nullptr;

    surface->wlr_surface = wlr_surface;
    surface->wlr_surface->data = surface;

    surface->scene_surface_tree = wlr_scene_tree_create(parent_tree);
    wlr_scene_surface_create(surface->scene_surface_tree, surface->wlr_surface);

    /* Make it so that the surface is accessible from anywhere
    in the scene tree. */
    wh_surface_set_on_scene_nodes(surface->scene_surface_tree, surface);

    VEC_INIT(&surface->children);

    VEC_INIT(&surface->callbacks.commit_callbacks);
    VEC_INIT(&surface->callbacks.map_callbacks);
    VEC_INIT(&surface->callbacks.unmap_callbacks);

    LISTEN(
        &surface->wlr_surface->events.commit,
        &surface->listeners.commit,
        on_surface_commit
    );

    LISTEN(
        &surface->wlr_surface->events.map,
        &surface->listeners.map,
        on_surface_map
    );

    LISTEN(
        &surface->wlr_surface->events.unmap,
        &surface->listeners.unmap,
        on_surface_unmap
    );

    LISTEN(
        &surface->wlr_surface->events.new_subsurface,
        &surface->listeners.new_subsurface,
        on_surface_new_subsurface
    );

    LISTEN(
        &surface->wlr_surface->events.destroy,
        &surface->listeners.destroy,
        on_surface_destroy
    );

    return surface;
}

void wh_surface_map(WhaleSurface* surface)
{
    wlr_scene_node_set_enabled(&surface->scene_surface_tree->node, true);
}

void wh_surface_unmap(WhaleSurface* surface)
{
    wlr_scene_node_set_enabled(&surface->scene_surface_tree->node, false);
}

WhaleSurface* wh_surface_get_focusable_at(wh_coord_t x, wh_coord_t y)
{
    struct wlr_scene_node* node =
        wlr_scene_node_at(&g_comp.root_scene->tree.node, x, y, NULL, NULL);

    if (!node || node->type != WLR_SCENE_NODE_BUFFER)
        return nullptr;

    return wh_surface_from_scene_node(node);
}

int wh_surface_layout_to_surface_coords(
    WhaleSurface* surface,
    const wh_pos2d_t* layout_coords,
    wh_pos2d_t* surface_coords
)
{
    wh_pos2d_t surface_pos = {
        .x = surface->scene_surface_tree->node.x,
        .y = surface->scene_surface_tree->node.y,
    };

    /* Follow the parent tree and taking into account each parent's position */
    WhaleSurface* running_surf = surface;
    while (running_surf->parent)
    {
        running_surf = running_surf->parent;
        surface_pos.x += running_surf->scene_surface_tree->node.x;
        surface_pos.y += running_surf->scene_surface_tree->node.y;
    }

    surface_coords->x = layout_coords->x - surface_pos.x;
    surface_coords->y = layout_coords->y - surface_pos.y;
    return 0;
}

void wh_surface_register_commit_cb(
    whale_surface_callback_t cb, WhaleSurface* surface
)
{
    bool includes = false;
    VEC_INCLUDES(cb, includes, &surface->callbacks.commit_callbacks);
    if (includes)
    {
        wh_log(
            DEBUG,
            "surface: Tried to register the same commit callback multime times."
        );
        return;
    }

    VEC_PUSH(cb, &surface->callbacks.commit_callbacks);
}

WhaleSurface* wh_surface_get_topmost_parent(WhaleSurface* surface)
{
    WhaleSurface* topmost_surface = surface;
    while (topmost_surface->parent)
        topmost_surface = topmost_surface->parent;

    return topmost_surface;
}
