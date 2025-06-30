
#ifndef WHALE_OUTPUT_H
#define WHALE_OUTPUT_H

#define WLR_USE_UNSTABLE
#include <wayland-server-core.h>
#include <whale/types.h>
#include <whale/vector.h>
#include <wlr/types/wlr_scene.h>

struct WhaleCompositor;
struct whale_client_t;

typedef struct
{
    WhaleCompositor* comp;

    struct wlr_output* wlr_output;
    struct wlr_scene_output* scene_output;

    struct wl_listener listener_frame;
    struct wl_listener listener_destroy;
    struct wl_listener listener_request_state;

    struct wl_list link;

    VEC(struct whale_client_t*) clients;
} WhaleOutput;

int wh_output_subsystem_init(WhaleCompositor* comp);

WhaleOutput*
wh_output_get_at(wh_coord_t x, wh_coord_t y, WhaleCompositor* comp);

WhaleOutput* wh_output_get_default(WhaleCompositor* comp);

struct wlr_box wh_output_get_geometry(WhaleOutput* output);

#endif // WHALE_OUTPUT_H
