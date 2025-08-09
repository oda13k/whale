
#ifndef WHALE_CLIENT_CLIENT_H
#define WHALE_CLIENT_CLIENT_H

#include <whale/compositor.h>
#include <whale/output.h>
#include <whale/types.h>
#include <whale/window/surface.h>
#include <whale/workspace.h>
#include <wlr/types/wlr_scene.h>

struct whale_client;

typedef struct whale_client
{
    WhaleSurface* surface;
    struct wlr_scene_tree* scene_tree;

    struct whale_client* parent;
    VEC(struct whale_client*) children;

    const char* title;

    WhaleWorkspace* bound_workspace;
    WhaleLayout layout;

    struct
    {
        void (*set_active)(bool active, struct whale_client* client);
    } driver;
} WhaleClient;

int wh_client_ss_init(WhaleCompositor* comp);

/**
 * Create a new base client. This client does not yet have any real
 * functionality; that is left to the underlying implementation e.g. xdg shell.
 */
WhaleClient* wh_client_new(struct wlr_surface* wlr_surface);

/**
 * Destroy a client, removing it from any outputs, etc. This only destroys
 * whatever was allocated by wh_client_new. Anything else must be handled by the
 * underlying implementation e.g. xdg shell.
 */
void wh_client_destroy(WhaleClient* client);

void wh_client_set_title(const char* title, WhaleClient* client);

bool wh_client_is_mapped(const WhaleClient* client);

void wh_client_set_pos(const wh_pos2d_t* pos, WhaleClient* client);

wh_pos2d_t wh_client_get_pos(WhaleClient* client);

void wh_client_set_active(bool active, WhaleClient* client);

WhaleClient* wh_client_from_surface(WhaleSurface* surface);

#endif // !WHALE_CLIENT_CLIENT_H
