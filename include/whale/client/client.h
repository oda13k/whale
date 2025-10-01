
#ifndef WHALE_CLIENT_CLIENT_H
#define WHALE_CLIENT_CLIENT_H

#include <whale/client/surface.h>
#include <whale/output/layer.h>
#include <whale/types.h>
#include <wlr/types/wlr_scene.h>

struct whale_workspace;

typedef struct whale_client
{
    WhaleSurface* surface;
    struct wlr_scene_tree* scene_tree;

    struct whale_workspace* workspace;

    WhaleLayer prev_layer;
    WhaleLayer layer;

    bool requested_map;
    bool is_being_moved_interactively;

    struct
    {
        void (*set_active)(bool active, struct whale_client* client);
        void (*set_tiled)(bool tiled, struct whale_client* client);
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
void wh_client_set_layer(WhaleLayer layer, WhaleClient* client);
void wh_client_restore_prev_layer(WhaleClient* client);

void wh_client_raise_to_top(WhaleClient* client);
void wh_client_lower_to_bottom(WhaleClient* client);

void wh_client_get_geometry(WhaleGeometry2D* out_geom, WhaleClient* client);

WhaleClient* wh_client_get_parent(WhaleClient* client);

void wh_client_close(WhaleClient* client);

WhaleClient* wh_client_from_surface(WhaleSurface* surface);

#endif // !WHALE_CLIENT_CLIENT_H
