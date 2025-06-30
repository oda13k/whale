
#ifndef WHALE_CLIENT_CLIENT_H
#define WHALE_CLIENT_CLIENT_H

#include <wayland-server-core.h>
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

typedef struct whale_client_t
{
    WhaleCompositor* comp;

    struct wlr_xdg_toplevel* xdg_toplevel;
    struct wlr_xdg_toplevel_decoration_v1* xdg_decoration;

    struct wlr_scene_tree* scene_tree;

    WhaleClientArrangement arrangement;

    WhaleOutput* bound_output;
    // struct wl_list output_link;

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

int wh_client_subsystem_init(WhaleCompositor* comp);

void wh_client_arrange_clients_on_output(WhaleOutput* output);

int wh_client_refresh_all_client_bounds(WhaleCompositor* comp);

/* These functions are each implemented in two different places. Those
that need xdg context are implemented in client/xdg_shell.c and those
that don't, are implemented in client/client.c. In the future if other
protocols appear we can make these function pointers in WhaleClient. */

/**
 * Check if a client has a parent. This function is xdg implemented.
 *
 * @param client Target client.
 *
 * @returns true if the client has a parent, false otherwise.
 */
bool wh_client_has_parent(WhaleClient* client);

/**
 * Get the parent of a client. Does no safety checks in order to determine if
 * the client actually has a parent. This function is xdg implemented.
 *
 * @param client Target client.
 *
 * @returns Pointer to the parent client.
 * @returns Probably a segfault if there is no parent.
 */
WhaleClient* wh_client_get_parent(WhaleClient* client);

/**
 * Set the position of a client. This function is client implemented.
 *
 * @param x Target x coord in pixels.
 * @param y Target y coord in pixels.
 * @param client Target client.
 */
void wh_client_set_pos(wh_coord_t x, wh_coord_t y, WhaleClient* client);

/**
 * Set the size of a client. This function is xdg implemented.
 *
 * @param w Target width in pixels
 * @param h Target height in pixels
 * @param client Target client.
 *
 * @returns 0 on success, negative value on failure.
 */
int wh_client_set_size(wh_size_t w, wh_size_t h, WhaleClient* client);

/**
 * Get the geometry of a client. The width and height are what you'd expect, but
 * the x and y are NOT the position of the client within the scene. They are
 * the bounds of the visible portion of the window. This function is xdg
 * implemented.
 *
 * @param client Target client.
 *
 * @return Pointer to the client's geometry.
 */
struct wlr_box* wh_client_get_geometry(WhaleClient* client);

/**
 * Get the position of a client on screen. This function is
 * client implemented.
 *
 * @param client Target client
 *
 * @return Vector containing the position.
 */
wh_pos2d_t wh_client_get_pos(WhaleClient* client);

wh_size2d_t wh_client_get_size(WhaleClient* client);

int wh_client_refresh_bounds(WhaleClient* client);

/**
 * Get the client at the given (output layout (resolution)) coords. The
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

WhaleClient* wh_client_from_wlr_surface(struct wlr_surface* surface);

int wh_client_sigterm(WhaleClient* client);

#endif // !WHALE_CLIENT_CLIENT_H
