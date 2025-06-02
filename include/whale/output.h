
#ifndef _WHALE_OUTPUT_H
#define _WHALE_OUTPUT_H

#include <wayland-server-core.h>
#define WLR_USE_UNSTABLE
#include <wlr/types/wlr_scene.h>

typedef struct
{
    struct wlr_output* wlr_output;
    struct wlr_scene_output* scene_output;

    struct wl_listener listener_frame;
    struct wl_listener listener_destroy;
    struct wl_listener listener_request_state;

    struct wl_list link;
} WhaleOutput;

void wh_output_on_new_output(struct wl_listener* listener, void* data);

void wh_output_layout_on_change(struct wl_listener* listener, void* data);

#endif // _WHALE_OUTPUT_H
