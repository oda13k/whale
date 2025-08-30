
#ifndef WHALE_CLIENT_CLIENT_H
#define WHALE_CLIENT_CLIENT_H

#include <whale/client/surface.h>
#include <whale/compositor.h>
#include <whale/output.h>
#include <whale/types.h>
#include <whale/workspace.h>
#include <wlr/types/wlr_scene.h>

struct whale_client;

typedef struct whale_client
{
    WhaleSurface* surface;
    struct wlr_scene_tree* scene_tree;

    WhaleWorkspace* workspace;
    WhaleLayout layout;

    bool requested_map;

    struct
    {
        void (*set_active)(bool active, struct whale_client* client);
        struct whale_client* (*get_parent)(struct whale_client* client);
    } driver;
} WhaleClient;

int wh_client_ss_init(WhaleCompositor* comp);

WhaleClient* wh_client_new(struct wlr_surface* wlr_surface);
void wh_client_destroy(WhaleClient* client);

void wh_client_map(WhaleClient* client);
void wh_client_unmap(WhaleClient* client);
bool wh_client_is_mapped(const WhaleClient* client);

void wh_client_set_pos(const WhalePosition2D* pos, WhaleClient* client);
void wh_client_get_pos(WhalePosition2D* out_pos, WhaleClient* client);

void wh_client_get_geometry(WhaleGeometry2D* out_geom, WhaleClient* client);

void wh_client_set_active(bool active, WhaleClient* client);

WhaleClient* wh_client_get_parent(WhaleClient* client);

WhaleClient* wh_client_from_surface(WhaleSurface* surface);

#endif // !WHALE_CLIENT_CLIENT_H
