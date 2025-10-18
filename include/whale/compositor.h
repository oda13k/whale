
#ifndef WHALE_COMPOSITOR_H
#define WHALE_COMPOSITOR_H

#include <wayland-server-core.h>
#include <whale/types.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_output.h>

typedef struct
{
    const char* startup_cmd;
} WhaleCompositorOptions;

int wh_compositor_start(const WhaleCompositorOptions* options);

void wh_compositor_change_vt(u8 vt);

void wh_compositor_request_exit();

struct wl_display* wh_compositor_get_wl_display();
struct wlr_compositor* wh_compositor_get_wlr_compositor();

void wh_compositor_set_new_input_callback(
    void (*cb)(struct wlr_input_device* dev)
);
void wh_compositor_clear_new_input_callback();

void wh_compositor_set_new_output_callback(
    void (*cb)(struct wlr_output* output)
);
void wh_compositor_clear_new_output_callback();

#endif // !WHALE_COMPOSITOR_H
