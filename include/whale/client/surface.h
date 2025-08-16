
/**
 * A surface is at it's simplest definition, a visible rectangle on screen
 * that an app can draw stuff into and have it shown on an output. It can
 * receive inputs but that's about it. It's a generic thing, with none of the
 * extra functionality you'd expect from a normal "window", like decorations,
 * the ability to move the window around or have it be managed by the
 * compositor, etc, etc. That functionality is left to the 'client'.
 *
 * What are the differences between a surface and a client then?
 * A client *is* at it's core, a surface with the added functionality you'd
 * expect from a window: decorations, the ability to have children, or spawn
 * popups, the ability to move the window, etc.
 *
 * If a client *is* a surface, then why do we need such a hard distinction?
 * Because there are other types of surfaces. All clients are surfaces
 * but not all surfaces are clients.
 *
 * Ok, what are the other types of surfaces?
 * - Clients: they have a surface and come in a few flavors themselves (check
 * client.h for more info).
 * - Popups: these can only exist in the context of a client, but are not
 * themselves clients. They don't have a title, nor can (should) they be managed
 * by us.
 * - Subsurfaces: These can also only exist in the context of a client. They are
 * a lot like popups. What's the difference? Basically just the API through
 * which they're created :^)
 */

#ifndef WHALE_CLIENT_SURFACE_H
#define WHALE_CLIENT_SURFACE_H

#define WLR_USE_UNSTABLE
#include <wayland-server-core.h>
#include <whale/vector.h>
#include <wlr/types/wlr_compositor.h>

struct whale_surface;

#define WHALE_SURFACE_CALLBACK_OK 0
#define WHALE_SURFACE_CALLBACK_REMOVE_SELF 1

typedef enum
{
    SURFACE_TYPE_UNKNOWN,
    SURFACE_TYPE_CLIENT,
    SURFACE_TYPE_SUBSURFACE,
    SURFACE_TYPE_POPUP
} SurfaceType;

typedef int (*whale_surface_callback_t)(struct whale_surface* surface);
#define WH_SURFACE_CALLBACK(_name, _surf_arg_name)                             \
    static int _name(WhaleSurface* _surf_arg_name)

typedef struct whale_surface
{
    struct whale_surface* parent;

    /* These can be popup surfaces or subsurfaces. */
    VEC(struct whale_surface*) children;

    struct wlr_surface* wlr_surface;
    struct wlr_scene_tree* scene_surface_tree;

    SurfaceType type;
    void* data;

    struct
    {
        struct wl_listener map;
        struct wl_listener unmap;
        struct wl_listener commit;
        struct wl_listener new_subsurface;
        struct wl_listener destroy;
    } listeners;

    struct
    {
        void (*set_size)(
            const WhaleSize2D* size, struct whale_surface* surface
        );

        void (*get_size)(WhaleSize2D* out_size, struct whale_surface* surface);

        void (*get_minmax_size)(
            WhaleSize2D* min_size,
            WhaleSize2D* max_size,
            struct whale_surface* surface
        );

        void* ctx;
    } driver;

    struct
    {
        VEC(whale_surface_callback_t) map_callbacks;
        VEC(whale_surface_callback_t) unmap_callbacks;
        VEC(whale_surface_callback_t) commit_callbacks;
        VEC(whale_surface_callback_t) destroy_callbacks;
    } callbacks;
} WhaleSurface;

WhaleSurface* wh_surface_new(
    struct wlr_surface* wlr_surface, struct wlr_scene_tree* parent_tree
);

void wh_surface_destroy(WhaleSurface* surface);

void wh_surface_map(WhaleSurface* surface);
void wh_surface_unmap(WhaleSurface* surface);

void wh_surface_set_size(const WhaleSize2D* size, WhaleSurface* surface);
void wh_surface_get_size(WhaleSize2D* out_size, WhaleSurface* surface);
void wh_surface_get_minmax_size(
    WhaleSize2D* out_min_size, WhaleSize2D* out_max_size, WhaleSurface* surface
);

void wh_surface_set_position_relative(
    const WhalePosition2D* pos, WhaleSurface* surface
);

WhaleSurface* wh_surface_get_topmost_at(const WhalePosition2D* pos);

int wh_surface_layout_to_surface_coords(
    WhaleSurface* surface,
    const WhalePosition2D* layout_coords,
    WhalePosition2D* surface_coords
);

void wh_surface_register_commit_cb(
    whale_surface_callback_t cb, WhaleSurface* surface
);

WhaleSurface* wh_surface_get_topmost_parent(WhaleSurface* surface);

WhaleSurface*
wh_surface_from_wlr_surface(const struct wlr_surface* wlr_surface);

#endif // !WHALE_CLIENT_SURFACE_H
