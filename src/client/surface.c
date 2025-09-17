
#include <stdlib.h>
#include <whale/client/client.h>
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

WH_SURFACE_CALLBACK(subsurface_on_commit_update_position, surface)
{
    struct wlr_subsurface* wlr_subsurface = surface->data;

    WhalePosition2D pos = {
        .x = wlr_subsurface->current.x, .y = wlr_subsurface->current.y
    };

    wh_surface_set_position_relative(&pos, surface);

    WhaleClient* client = wh_client_from_surface(surface);
    if (client->workspace)
        wh_workspace_arrange(client->workspace);

    return WHALE_SURFACE_CALLBACK_OK;
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

    VEC_FOR_EACH (cb, &surface->callbacks.map_callbacks)
    {
        if ((*cb)(surface) == WHALE_SURFACE_CALLBACK_REMOVE_SELF)
        {
            VEC_REMOVE(*cb, &surface->callbacks.map_callbacks);
            --cb;
        }
    }
}

static void on_surface_unmap(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_LISTENER(listener, unmap);
    wh_surface_unmap(surface);

    VEC_FOR_EACH (cb, &surface->callbacks.unmap_callbacks)
    {
        if ((*cb)(surface) == WHALE_SURFACE_CALLBACK_REMOVE_SELF)
        {
            VEC_REMOVE(*cb, &surface->callbacks.unmap_callbacks);
            --cb;
        }
    }
}

static void on_surface_new_subsurface(struct wl_listener*, void* data)
{
    struct wlr_subsurface* wlr_subsurface = data;

    WhaleSurface* parent = wh_surface_from_wlr_surface(wlr_subsurface->parent);

    WhaleSurface* surface =
        wh_surface_new(wlr_subsurface->surface, parent->scene_surface_tree);

    surface->type = SURFACE_TYPE_SUBSURFACE;
    surface->data = wlr_subsurface;

    wh_surface_register_commit_cb(
        subsurface_on_commit_update_position, surface
    );

    /* The surface is now owned by it's parent and will be free'd by it when
     * the time comes. */
    surface->parent = parent;
    VEC_PUSH(surface, &parent->children);

    /* Force an initial update */
    subsurface_on_commit_update_position(surface);
}

static void on_surface_destroy(struct wl_listener* listener, void*)
{
    WhaleSurface* surface = WH_SURFACE_FROM_LISTENER(listener, destroy);
    wh_surface_destroy(surface);
}

static WhaleSurface* surface_from_scene_node(const struct wlr_scene_node* node)
{
    WH_ASSERT_SANITY(node->data);
    return node->data;
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

    surface->scene_surface_tree = wlr_scene_tree_create(parent_tree);
    wlr_scene_surface_create(surface->scene_surface_tree, surface->wlr_surface);

    /* Get the 2nd child of the scene_surface_tree (the actual surface buffer)
     * and set the our surface as it's data. Thsi is used to retrieve our
     * surface from a node when we hover over it. */
    struct wlr_scene_node* node = CONTAINER_OF(
        surface->scene_surface_tree->children.next, struct wlr_scene_node, link
    );
    WH_ASSERT(node && node->type == WLR_SCENE_NODE_BUFFER);
    node->data = surface;

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
    UNLISTEN(&surface->listeners.destroy);

    VEC_DESTROY(&surface->callbacks.commit_callbacks);
    VEC_DESTROY(&surface->callbacks.map_callbacks);
    VEC_DESTROY(&surface->callbacks.unmap_callbacks);

    wlr_scene_node_destroy(&surface->scene_surface_tree->node);

    free(surface);
}

void wh_surface_map(WhaleSurface* surface)
{
    wlr_scene_node_set_enabled(&surface->scene_surface_tree->node, true);
}

void wh_surface_unmap(WhaleSurface* surface)
{
    wlr_scene_node_set_enabled(&surface->scene_surface_tree->node, false);
}

void wh_surface_set_size(const WhaleSize2D* size, WhaleSurface* surface)
{
    WH_ASSERT_SANITY(surface->driver.set_size);
    surface->driver.set_size(size, surface);
}

void wh_surface_get_size(WhaleSize2D* out_size, WhaleSurface* surface)
{
    WH_ASSERT_SANITY(surface->driver.get_size);
    surface->driver.get_size(out_size, surface);
}

void wh_surface_get_minmax_size(
    WhaleSize2D* out_min_size, WhaleSize2D* out_max_size, WhaleSurface* surface
)
{
    WH_ASSERT_SANITY(surface->driver.get_minmax_size);
    surface->driver.get_minmax_size(out_min_size, out_max_size, surface);
}

void wh_surface_set_position_relative(
    const WhalePosition2D* pos, WhaleSurface* surface
)
{
    wlr_scene_node_set_position(
        &surface->scene_surface_tree->node, pos->x, pos->y
    );
}

int wh_surface_layout_to_surface_coords(
    WhaleSurface* surface,
    const WhalePosition2D* layout_coords,
    WhalePosition2D* surface_coords
)
{
    WhaleClient* client = wh_client_from_surface(surface);
    WhaleGeometry2D geom;
    wh_client_get_geometry(&geom, client);

    /* Follow the parent tree and taking into account each parent's position */
    while (surface)
    {
        geom.pos.x += surface->scene_surface_tree->node.x;
        geom.pos.y += surface->scene_surface_tree->node.y;
        surface = surface->parent;
    }

    surface_coords->x = layout_coords->x - geom.pos.x;
    surface_coords->y = layout_coords->y - geom.pos.y;
    return 0;
}

void wh_surface_register_commit_cb(
    whale_surface_callback_t cb, WhaleSurface* surface
)
{
    if (VEC_INCLUDES(cb, &surface->callbacks.commit_callbacks))
    {
        wh_log(
            DEBUG,
            "surface: Tried to register the same commit callback multiple times."
        );
        return;
    }

    VEC_PUSH(cb, &surface->callbacks.commit_callbacks);
}

void wh_surface_register_map_cb(
    whale_surface_callback_t cb, WhaleSurface* surface
)
{
    if (VEC_INCLUDES(cb, &surface->callbacks.map_callbacks))
    {
        wh_log(
            DEBUG,
            "surface: Tried to register the same map callback multiple times."
        );
        return;
    }

    VEC_PUSH(cb, &surface->callbacks.map_callbacks);
}

void wh_surface_register_unmap_cb(
    whale_surface_callback_t cb, WhaleSurface* surface
)
{
    if (VEC_INCLUDES(cb, &surface->callbacks.unmap_callbacks))
    {
        wh_log(
            DEBUG,
            "surface: Tried to register the same unmap callback multiple times."
        );
        return;
    }

    VEC_PUSH(cb, &surface->callbacks.unmap_callbacks);
}

WhaleSurface* wh_surface_from_wlr_surface(const struct wlr_surface* wlr_surface)
{
    WH_ASSERT_SANITY(wlr_surface->data);
    return wlr_surface->data;
}
