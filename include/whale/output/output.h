
#ifndef WHALE_OUTPUT_H
#define WHALE_OUTPUT_H

#include <wayland-server-core.h>
#include <whale/output/workspace.h>
#include <whale/types.h>
#include <whale/utils/vector.h>
#include <wlr/types/wlr_scene.h>

typedef struct whale_output
{
    u32 id;

    struct wlr_output* wlr_output;
    struct wlr_scene_output* scene_output;

    struct wl_listener listener_frame;
    struct wl_listener listener_destroy;
    struct wl_listener listener_request_state;

    VEC(WhaleWorkspace) workspaces;
    WhaleWorkspace* active_workspace;
} WhaleOutput;

int wh_output_init();

WhaleOutput* wh_output_get_main();

void wh_output_get_geometry(WhaleGeometry2D* out_geom, WhaleOutput* output);

int wh_output_activate_workspace(u8 workspace_idx, WhaleOutput* output);

WhaleWorkspace* wh_output_get_active_workspace(WhaleOutput* output);

WhaleWorkspace* wh_output_get_workspace(u8 workspace_idx, WhaleOutput* output);

WhaleOutput* wh_output_get_focused();

#endif // WHALE_OUTPUT_H
