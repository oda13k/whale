
#ifndef WHALE_CLIENT_CLIENT_H
#define WHALE_CLIENT_CLIENT_H

#include <whale/client/surface.h>
#include <whale/output/workspace.h>
#include <whale/types.h>
#include <wlr/types/wlr_scene.h>

typedef struct whale_client
{
    WhaleSurface* surface;
    struct wlr_scene_tree* scene_tree;

    WhaleWorkspace* workspace;

    WhaleLayout prev_layout;
    WhaleLayout layout;

    bool requested_map;

    struct
    {
        void (*set_active)(bool active, struct whale_client* client);
        struct whale_client* (*get_parent)(struct whale_client* client);
        void (*close)(struct whale_client* client);
    } driver;
} WhaleClient;

int wh_client_ss_init();

WhaleClient* wh_client_new(struct wlr_surface* wlr_surface);
void wh_client_destroy(WhaleClient* client);

void wh_client_map(WhaleClient* client);
void wh_client_unmap(WhaleClient* client);

void wh_client_set_pos(const WhalePosition2D* pos, WhaleClient* client);
void wh_client_set_size(const WhaleSize2D* size, WhaleClient* client);
void wh_client_set_active(bool active, WhaleClient* client);
void wh_client_set_layout(WhaleLayout layout, WhaleClient* client);

void wh_client_raise_to_top(WhaleClient* client);

void wh_client_get_geometry(WhaleGeometry2D* out_geom, WhaleClient* client);

WhaleClient* wh_client_get_parent(WhaleClient* client);

void wh_client_close(WhaleClient* client);

WhaleClient* wh_client_from_surface(WhaleSurface* surface);

#endif // !WHALE_CLIENT_CLIENT_H
