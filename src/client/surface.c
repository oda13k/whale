
#include <stdlib.h>
#include <whale/client/surface.h>
#include <whale/compositor.h>
#include <whale/debug.h>
#include <whale/log.h>
#include <whale/output/scene.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>

#define CONTAINER_OF(_ptr, _sample_type, _member)                              \
    ((_sample_type*)((void*)(_ptr) - offsetof(_sample_type, _member)))

#define WH_SURFACE_FROM_LISTENER(_ptr, _listener_name)                         \
    (CONTAINER_OF(_ptr, WhaleSurface, listeners._listener_name))

typedef struct
{
    WhaleSurface* surface;
    struct wlr_subsurface* wlr_subsurface;

    struct
    {
        struct wl_listener destroy;
    } listeners;
} WhaleSubsurface;

static void on_subsurface_surface_commit(WhaleSurface* surface)
{
    WhaleSubsurface* subsurface = surface->data;
    struct wlr_subsurface* wlr_subsurface = subsurface->wlr_subsurface;

    WhalePosition2D pos = {
        .x = wlr_subsurface->current.x, .y = wlr_subsurface->current.y
    };

    wh_surface_set_pos(&pos, surface);
}

static void on_subsurface_surface_destroy(WhaleSurface* surface)
{
    WhaleSubsurface* subsurface = surface->data;

    UNLISTEN(&subsurface->listeners.destroy);
    free(subsurface);
}

static void on_subsurface_destroy(struct wl_listener* l, void*)
{
    WhaleSubsurface* subsurface =
        CONTAINER_OF(l, WhaleSubsurface, listeners.destroy);

    wh_surface_destroy(subsurface->surface);
}

static void on_surface_new_subsurface(struct wl_listener*, void* data)
{
    struct wlr_subsurface* wlr_subsurface = data;

    WhaleSurface* parent = wh_surface_from_wlr_surface(wlr_subsurface->parent);

    WhaleSubsurface* subsurface = calloc(1, sizeof(WhaleSubsurface));
    if (!subsurface)
    {
        wh_log(ERR, "surface: Failed to allocate subsurface.");
        return;
    }

    WhaleSurface* surface =
        wh_surface_new_child(wlr_subsurface->surface, parent);
    if (!surface)
    {
        wh_log(ERR, "subsurface: Failed to create surface");
        free(subsurface);
        return;
    }

    subsurface->surface = surface;
    subsurface->wlr_subsurface = wlr_subsurface;

    surface->type = SURFACE_TYPE_SUBSURFACE;
    surface->ignore_keyboard_focus = true;
    surface->data = subsurface;

    surface->commit = on_subsurface_surface_commit;
    surface->destroy = on_subsurface_surface_destroy;
    surface->commit(surface);

    LISTEN(
        &wlr_subsurface->events.destroy,
        &subsurface->listeners.destroy,
        on_subsurface_destroy
    );
}

static void on_surface_commit(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_LISTENER(listener, commit);

    if (surface->commit)
        surface->commit(surface);
}

static void on_surface_map(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_LISTENER(listener, map);
    wh_surface_map(surface);

    if (surface->map)
        surface->map(surface);
}

static void on_surface_unmap(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_LISTENER(listener, unmap);
    wh_surface_unmap(surface);

    if (surface->unmap)
        surface->unmap(surface);
}

void wh_surface_destroy(WhaleSurface* surface)
{
    /* If the surface has chidren destroy them all. */
    VEC_FOR_EACH (child, &surface->children)
    {
        wh_surface_destroy(*child);
        --child;
    }

    VEC_DESTROY(&surface->children);

    if (surface->parent)
        VEC_REMOVE(surface, &surface->parent->children);

    UNLISTEN(&surface->listeners.commit);
    UNLISTEN(&surface->listeners.map);
    UNLISTEN(&surface->listeners.unmap);
    UNLISTEN(&surface->listeners.new_subsurface);

    wlr_scene_node_destroy(&surface->scene_tree->node);

    if (surface->destroy)
        surface->destroy(surface);

    free(surface);
}

WhaleSurface* wh_surface_new(
    struct wlr_surface* wlr_surface, struct wlr_scene_tree* parent_tree
)
{
    WhaleSurface* surface = calloc(1, sizeof(WhaleSurface));
    if (!surface)
        return nullptr;

    surface->wlr_surface = wlr_surface;
    /* The wlr_surface can point back to our surface. */
    surface->wlr_surface->data = surface;

    surface->scene_tree = wlr_scene_tree_create(parent_tree);
    surface->scene_surface =
        wlr_scene_surface_create(surface->scene_tree, surface->wlr_surface);

    wh_surface_invalidate_position(surface);

    /* Get the 2nd child of the scene_tree (the actual surface buffer)
     * and set the our surface as it's data. Thsi is used to retrieve our
     * surface from a node when we hover over it. */
    struct wlr_scene_node* node = CONTAINER_OF(
        surface->scene_tree->children.next, struct wlr_scene_node, link
    );
    WH_ASSERT(node && node->type == WLR_SCENE_NODE_BUFFER);
    node->data = surface;

    VEC_INIT(&surface->children);

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

    return surface;
}

void wh_surface_map(WhaleSurface* surface)
{
    wlr_scene_node_set_enabled(&surface->scene_tree->node, true);
}

void wh_surface_unmap(WhaleSurface* surface)
{
    wlr_scene_node_set_enabled(&surface->scene_tree->node, false);
}

WhaleSurface* wh_surface_new_child(
    struct wlr_surface* wlr_child_surface, WhaleSurface* surface
)
{
    WhaleSurface* child =
        wh_surface_new(wlr_child_surface, surface->scene_tree);
    if (!child)
    {
        wh_log(ERR, "surface: Failed to create child surface.");
        return nullptr;
    }

    child->parent = surface;
    VEC_PUSH(child, &surface->children);

    return child;
}

void wh_surface_set_pos(const WhalePosition2D* pos, WhaleSurface* surface)
{
    if (pos->x == surface->scene_tree->node.x &&
        pos->y == surface->scene_tree->node.y)
        return;

    wlr_scene_node_set_position(
        &surface->scene_tree->node,
        CAST_COORD_TO_INT(pos->x),
        CAST_COORD_TO_INT(pos->y)
    );

    wh_surface_invalidate_position(surface);
}

void wh_surface_invalidate_position(WhaleSurface* surface)
{
    surface->layout_pos.x = 0;
    surface->layout_pos.y = 0;

    const struct wlr_scene_tree* tree = surface->scene_tree;
    while (tree)
    {
        const struct wlr_scene_node* node = &tree->node;
        surface->layout_pos.x += node->x;
        surface->layout_pos.y += node->y;

        tree = node->parent;
    }

    VEC_FOR_EACH (child, &surface->children)
        wh_surface_invalidate_position(*child);
}

WhaleSurface* wh_surface_from_wlr_surface(const struct wlr_surface* wlr_surface)
{
    WH_ASSERT_SANITY(wlr_surface->data);
    return wlr_surface->data;
}
