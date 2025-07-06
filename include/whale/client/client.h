
#ifndef WHALE_CLIENT_CLIENT_H
#define WHALE_CLIENT_CLIENT_H

#include <whale/compositor.h>
#include <whale/output.h>
#include <whale/types.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

typedef enum
{
    ARRANGE_TILED,
    ARRANGE_FLOATING,
    ARRANGE_MONOCLE
} WhaleClientArrangement;

struct whale_client;

typedef void (*client_set_size_t)(
    const wh_size2d_t* size, struct whale_client* client
);
typedef wh_size2d_t (*client_get_size_t)(struct whale_client* client);

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

typedef struct whale_client
{
    struct wlr_scene_tree* scene_tree;

    WhaleClientArrangement arrangement;

    WhaleOutput* bound_output;

    void* data;

    struct
    {
        client_set_size_t set_size;
        client_get_size_t get_size;
        client_get_parent_t get_parent;
        client_send_close_t send_close;
        client_get_wlr_surface get_wlr_surface;
        client_get_geometry_2d get_internal_geometry;
    } methods;
} WhaleClient;

int wh_client_ss_init(WhaleCompositor* comp);

/**
 * Create a new base client. This client does not yet have any real
 * functionality; that is left to the underlying implementation e.g. xdg shell.
 */
WhaleClient* wh_client_new(struct wlr_scene_tree* scene_tree);

/**
 * Destroy a client, removing it from any outputs, etc. This only destroys
 * whatever was allocated by wh_client_new. Anything else must be handled by the
 * underlying implementation e.g. xdg shell.
 */
void wh_client_destroy(WhaleClient* client);

void wh_client_arrange_clients_on_output(WhaleOutput* output);

int wh_client_refresh_bounds(WhaleClient* client);

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
WhaleClient* wh_client_get_at_coords(
    wh_coord_t x, wh_coord_t y, const WhaleCompositor* comp
);

bool wh_client_is_mapped(const WhaleClient* client);

int wh_client_layout_to_client_coords(
    WhaleClient* client,
    const wh_pos2d_t* layout_coords,
    wh_pos2d_t* client_coords
);

/**
 * Map a client on the screen. A mapped client will be visible, focusable
 * and considered for arrangement.
 */
void wh_client_map(WhaleClient* client);

/**
 * Unmap a client from the screen. An unampped client will not be visible, will
 * not be focusable and will not be considered for arrangement. An unmapped
 * client will retain it's bounding output.
 */
void wh_client_unmap(WhaleClient* client);

void wh_client_set_pos(const wh_pos2d_t* pos, WhaleClient* client);
wh_pos2d_t wh_client_get_pos(WhaleClient* client);

void wh_client_set_size(const wh_size2d_t* size, WhaleClient* client);
wh_size2d_t wh_client_get_size(WhaleClient* client);

WhaleClient* wh_client_get_parent(WhaleClient* client);

void wh_client_send_close(WhaleClient* client);

struct wlr_surface* wh_client_get_wlr_surface(WhaleClient* client);

WhaleGeometry2D wh_client_get_internal_geometry(WhaleClient* client);

void wh_client_set_pos_and_size_atomic(
    const wh_pos2d_t* pos, const wh_size2d_t* size, WhaleClient* client
);

#endif // !WHALE_CLIENT_CLIENT_H
