
#ifndef WHALE_CLIENT_CLIENT_H
#define WHALE_CLIENT_CLIENT_H

#include <whale/client/surface.h>
#include <whale/output/layer.h>
#include <whale/types.h>
#include <wlr/types/wlr_scene.h>

struct whale_workspace;
struct whale_client;

typedef struct
{
    void (*set_active)(bool active, struct whale_client* client);
    void (*set_tiled)(bool tiled, struct whale_client* client);
    void (*close)(struct whale_client* client);

    void (*set_size)(const WhaleSize2D* size, struct whale_client* client);
    void (*get_minmax_size)(
        WhaleSize2D* min, WhaleSize2D* max, struct whale_client* client
    );

    void (*configure)(struct whale_client* client);

    void (*commit)(struct whale_client* client);
    void (*map)(struct whale_client* client);
} WhaleClientDriver;

typedef struct whale_client
{
    WhaleSurface* surface;
    struct wlr_scene_tree* scene_tree;

    WhaleSize2D size;

    struct whale_workspace* workspace;

    WhaleLayer layer;
    WhaleLayer prev_layer;

    bool interactive;

    bool mappable;

    const WhaleClientDriver* driver;
    void* driver_ctx;
} WhaleClient;

int wh_client_ss_init();

WhaleClient* wh_client_new(const WhaleClientDriver* driver, void* driver_ctx);
void wh_client_destroy(WhaleClient* client);

int wh_client_attach_surface(
    struct wlr_surface* wlr_surface, WhaleClient* client
);
void wh_client_detach_surface(WhaleClient* client);

void wh_client_map(WhaleClient* client);
void wh_client_unmap(WhaleClient* client);
bool wh_client_is_mapped(WhaleClient* client);

void wh_client_set_pos(const WhalePosition2D* pos, WhaleClient* client);
void wh_client_set_size(const WhaleSize2D* size, WhaleClient* client);
void wh_client_set_active(bool active, WhaleClient* client);
void wh_client_configure(WhaleClient* client);
void wh_client_get_minmax_size(
    WhaleSize2D* min, WhaleSize2D* max, WhaleClient* client
);

void wh_client_start_interactive(WhaleClient* client);
void wh_client_drop_interactive(WhaleClient* client);

void wh_client_set_layer(WhaleLayer layer, WhaleClient* client);
void wh_client_restore_prev_layer(WhaleClient* client);

void wh_client_raise_to_top(WhaleClient* client);
void wh_client_lower_to_bottom(WhaleClient* client);

void wh_client_get_geometry(WhaleGeometry2D* out_geom, WhaleClient* client);

void wh_client_close(WhaleClient* client);

WhaleClient* wh_client_from_surface(WhaleSurface* surface);

#endif // !WHALE_CLIENT_CLIENT_H
