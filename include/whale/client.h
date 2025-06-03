
#ifndef _WHALE_CLIENT_H
#define _WHALE_CLIENT_H

#include <wayland-server-core.h>
#include <whale/compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

typedef double wh_coord_t;

typedef struct
{
    WhaleCompositor* comp;

    struct wlr_xdg_toplevel* xdg_toplevel;
    struct wlr_xdg_toplevel_decoration_v1* xdg_decoration;

    struct wlr_scene_tree* scene_tree;

    struct
    {
        struct wl_listener map;
        struct wl_listener unmap;
        struct wl_listener commit;

        struct wl_listener destroy;
        struct wl_listener set_title;

        struct wl_listener decoration_request_mode;
        struct wl_listener decoration_destroy;
    } listeners;
} WhaleClient;

void wh_client_on_new_client(struct wl_listener* listener, void* data);

void wh_client_on_new_xdg_decoration(struct wl_listener* listener, void* data);

/**
 * Get the client at the given (output layout (resolution)) coords. The
 * client is considered if the point at x, y can receive input focus.
 *
 * @param x X coordinate
 * @param y Y coordinate
 * @param comp The whale compositor
 *
 * @returns Pointer to the client at the given coords.
 * @returns NULL if there is no client at the given coords.
 */
WhaleClient* wh_client_get_at_coords(
    wh_coord_t x, wh_coord_t y, const WhaleCompositor* comp
);

#endif // !_WHALE_CLIENT_H
