
#ifndef WHALE_OUTPUT_H
#define WHALE_OUTPUT_H

#define WLR_USE_UNSTABLE
#include <wayland-server-core.h>
#include <whale/types.h>
#include <whale/vector.h>
#include <wlr/types/wlr_scene.h>

struct whale_compositor;
struct whale_client;

typedef struct
{
    struct whale_compositor* comp;

    struct wlr_output* wlr_output;
    struct wlr_scene_output* scene_output;

    struct wl_listener listener_frame;
    struct wl_listener listener_destroy;
    struct wl_listener listener_request_state;

    VEC(struct whale_client*) clients;
} WhaleOutput;

int wh_output_ss_init(struct whale_compositor* comp);

WhaleOutput*
wh_output_get_at(wh_coord_t x, wh_coord_t y, struct whale_compositor* comp);

WhaleOutput* wh_output_get_default(struct whale_compositor* comp);

WhaleGeometry2D wh_output_get_geometry(WhaleOutput* output);

#endif // WHALE_OUTPUT_H
