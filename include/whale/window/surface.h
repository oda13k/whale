
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

#ifndef WHALE_WINDOW_SURFACE_H
#define WHALE_WINDOW_SURFACE_H

#define WLR_USE_UNSTABLE
#include <wayland-server-core.h>
#include <whale/vector.h>
#include <wlr/types/wlr_compositor.h>

struct whale_surface;

#define WHALE_SURFACE_CALLBACK_OK 0
#define WHALE_SURFACE_CALLBACK_REMOVE_SELF 1

typedef enum
{
    SURFACE_FOCUS_NONE = 0,
    SURFACE_FOCUS_KEYBOARD = (1 << 0),
    SURFACE_FOCUS_POINTER = (1 << 1),
    SURFACE_FOCUS_KEYBOARD_TOPMOST = (1 << 2)
} SurfaceFocusType;

typedef int (*whale_surface_callback_t)(struct whale_surface* surface);

typedef struct whale_surface
{
    struct whale_surface* parent;

    /* These can be popup surfaces or subsurfaces. */
    VEC(struct whale_surface*) children;

    struct wlr_surface* wlr_surface;
    struct wlr_scene_tree* scene_surface_tree;

    SurfaceFocusType focus_type;

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
            const wh_size2d_t* size, struct whale_surface* surface
        );

        void (*get_size)(wh_size2d_t* out_size, struct whale_surface* surface);

        void (*get_minmax_size)(
            wh_size2d_t* min_size,
            wh_size2d_t* max_size,
            struct whale_surface* surface
        );

        void (*get_internal_geom)(
            WhaleGeometry2D* geom_out, const struct whale_surface* surface
        );

        void* ctx;
    } implementation;

    struct
    {
        VEC(whale_surface_callback_t) commit_callbacks;
        VEC(whale_surface_callback_t) map_callbacks;
        VEC(whale_surface_callback_t) unmap_callbacks;
    } callbacks;
} WhaleSurface;

WhaleSurface* wh_surface_new(
    struct wlr_surface* wlr_surface, struct wlr_scene_tree* parent_tree
);

void wh_surface_destroy(WhaleSurface* surface);

void wh_surface_map(WhaleSurface* surface);
void wh_surface_unmap(WhaleSurface* surface);

WhaleSurface* wh_surface_get_focusable_at(wh_coord_t x, wh_coord_t y);

int wh_surface_layout_to_surface_coords(
    WhaleSurface* surface,
    const wh_pos2d_t* layout_coords,
    wh_pos2d_t* surface_coords
);

void wh_surface_register_commit_cb(
    whale_surface_callback_t cb, WhaleSurface* surface
);

WhaleSurface* wh_surface_get_topmost_parent(WhaleSurface* surface);

#endif // !WHALE_WINDOW_SURFACE_H
