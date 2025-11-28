
#ifndef WHALE_CLIENT_SURFACE_H
#define WHALE_CLIENT_SURFACE_H

#include <wayland-server-core.h>
#include <whale/utils/vector.h>
#include <wlr/types/wlr_compositor.h>

typedef enum
{
    SURFACE_TYPE_CLIENT,
    SURFACE_TYPE_SUBSURFACE,
    SURFACE_TYPE_POPUP
} SurfaceType;

struct whale_surface;

typedef struct whale_surface
{
    struct whale_surface* parent;

    VEC(struct whale_surface*) children;

    struct wlr_surface* wlr_surface;
    struct wlr_scene_tree* scene_tree;
    struct wlr_scene_surface* scene_surface;

    WhalePosition2D layout_pos;

    bool ignore_keyboard_focus;

    SurfaceType type;
    void* data;

    struct
    {
        struct wl_listener map;
        struct wl_listener unmap;
        struct wl_listener commit;
        struct wl_listener new_subsurface;
    } listeners;

    void (*map)(struct whale_surface* surface);
    void (*unmap)(struct whale_surface* surface);
    void (*commit)(struct whale_surface* surface);
    void (*destroy)(struct whale_surface* surface);
} WhaleSurface;

WhaleSurface* wh_surface_new(
    struct wlr_surface* wlr_surface,
    struct wlr_scene_tree* parent_tree
);

void wh_surface_destroy(WhaleSurface* surface);

void wh_surface_map(WhaleSurface* surface);
void wh_surface_unmap(WhaleSurface* surface);

WhaleSurface* wh_surface_new_child(
    struct wlr_surface* wlr_child_surface,
    WhaleSurface* surface
);

void wh_surface_set_pos(const WhalePosition2D* pos, WhaleSurface* surface);

void wh_surface_invalidate_position(WhaleSurface* surface);

WhaleSurface*
wh_surface_from_wlr_surface(const struct wlr_surface* wlr_surface);

#endif // !WHALE_CLIENT_SURFACE_H
