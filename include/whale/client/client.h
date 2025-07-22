
#ifndef WHALE_CLIENT_CLIENT_H
#define WHALE_CLIENT_CLIENT_H

#include <whale/compositor.h>
#include <whale/output.h>
#include <whale/types.h>
#include <whale/workspace.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

struct whale_client;

typedef struct whale_client* (*client_get_parent_t)(
    struct whale_client* client
);

typedef void (*client_send_close_t)(struct whale_client* client);

typedef struct wlr_surface* (*client_get_wlr_surface)(
    const struct whale_client* client
);

typedef WhaleGeometry2D (*client_get_geometry_2d)(
    const struct whale_client* client
);

typedef void (*client_get_minmax_size)(
    wh_size2d_t* min_size, wh_size2d_t* max_size, struct whale_client* client
);

struct whale_surface;

typedef struct
{
    void* data;

    void (*surface_init)(struct whale_surface* surface);

    void (*set_size)(const wh_size2d_t* size, struct whale_surface* surface);
    wh_size2d_t (*get_size)(struct whale_surface* surface);

    struct whale_client* (*get_parent)(struct whale_client* client);

    client_send_close_t send_close;
    client_get_geometry_2d get_internal_geometry;
    client_get_minmax_size get_minmax_size;
} WhaleSurfaceImplementation;

typedef struct whale_surface
{
    /* Only set if the surface is the primary surface of a client. */
    struct whale_client* parent_client;

    struct whale_surface* parent;
    VEC(struct whale_surface*) children;

    struct wlr_surface* wlr_surface;
    struct wlr_scene_tree* scene_surface_tree;

    struct
    {
        struct wl_listener map;
        struct wl_listener unmap;
        struct wl_listener commit;
        struct wl_listener new_subsurface;
    } listeners;

    WhaleSurfaceImplementation impl;
} WhaleSurface;

typedef struct whale_client
{
    struct whale_client* parent;

    VEC(struct whale_client*) children;

    struct wlr_scene_tree* scene_tree;

    WhaleSurface surface;

    WhaleWorkspace* bound_workspace;

    WhaleLayout layout;
} WhaleClient;

int wh_client_ss_init(WhaleCompositor* comp);

/**
 * Create a new base client. This client does not yet have any real
 * functionality; that is left to the underlying implementation e.g. xdg shell.
 */
WhaleClient* wh_client_new(
    struct wlr_surface* wlr_surface, const WhaleSurfaceImplementation* impl
);

/**
 * Destroy a client, removing it from any outputs, etc. This only destroys
 * whatever was allocated by wh_client_new. Anything else must be handled by the
 * underlying implementation e.g. xdg shell.
 */
void wh_client_destroy(WhaleClient* client);

/**
 * Get the client at the given coords. The
 * client is considered if the point at x, y can receive input focus.
 * This function is client implemented.
 *
 * @param x X coordinate
 * @param y Y coordinate
 * @param comp The whale compositor
 *
 * @returns Pointer to the client at the given coords.
 * @returns NULL if there is no client at the given coords.
 */
WhaleSurface* wh_surface_get_at_coords(wh_coord_t x, wh_coord_t y);

bool wh_client_is_mapped(const WhaleClient* client);

int wh_surface_layout_to_surface_coords(
    WhaleSurface* surface,
    const wh_pos2d_t* layout_coords,
    wh_pos2d_t* surface_coords
);

/**
 * Map a client on the screen. A mapped client will be visible, focusable
 * and considered for arrangement.
 */
void wh_surface_map(WhaleSurface* surface);

/**
 * Unmap a client from the screen. An unampped client will not be visible, will
 * not be focusable and will not be considered for arrangement. An unmapped
 * client will retain it's bounding output.
 */
void wh_surface_unmap(WhaleSurface* surface);

void wh_client_set_pos(const wh_pos2d_t* pos, WhaleClient* client);
wh_pos2d_t wh_client_get_pos(WhaleClient* client);

void wh_surface_set_size(const wh_size2d_t* size, WhaleSurface* surface);
wh_size2d_t wh_surface_get_size(WhaleSurface* surface);

WhaleClient* wh_client_get_parent(WhaleClient* surface);

void wh_client_send_close(WhaleClient* client);

WhaleGeometry2D wh_client_get_internal_geometry(WhaleClient* client);

WhaleGeometry2D wh_client_get_external_geometry(WhaleClient* client);

void wh_client_set_pos_and_size_atomic(
    const wh_pos2d_t* pos, const wh_size2d_t* size, WhaleClient* client
);

void wh_client_get_minmax_size(
    wh_size2d_t* min_size, wh_size2d_t* max_size, WhaleClient* client
);

#endif // !WHALE_CLIENT_CLIENT_H
