
#ifndef WHALE_OUTPUT_H
#define WHALE_OUTPUT_H

#define WLR_USE_UNSTABLE
#include <wayland-server-core.h>
#include <whale/types.h>
#include <whale/vector.h>
#include <whale/workspace.h>
#include <wlr/types/wlr_scene.h>

struct whale_compositor;

typedef struct whale_output
{
    struct whale_compositor* comp;

    struct wlr_output* wlr_output;
    struct wlr_scene_output* scene_output;

    struct wl_listener listener_frame;
    struct wl_listener listener_destroy;
    struct wl_listener listener_request_state;

    VEC(WhaleWorkspace) workspaces;
    WhaleWorkspace* active_workspace;
} WhaleOutput;

int wh_output_ss_init(struct whale_compositor* comp);

WhaleOutput* wh_output_get_at(const WhalePosition2D* pos);

WhaleOutput* wh_output_get_main();

void wh_output_get_geometry(WhaleGeometry2D* out_geom, WhaleOutput* output);

int wh_output_activate_workspace(u8 workspace_idx, WhaleOutput* output);

WhaleWorkspace* wh_output_get_active_workspace(WhaleOutput* output);

WhaleWorkspace* wh_output_get_workspace(u8 workspace_idx, WhaleOutput* output);

#endif // WHALE_OUTPUT_H
