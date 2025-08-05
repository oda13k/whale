
#define WLR_USE_UNSTABLE
#include <stdio.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <whale/compositor.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/output.h>
#include <whale/types.h>
#include <whale/utils.h>
#include <whale/window/client.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>

static int die(const char* msg)
{
    wh_log(FATAL, msg);
    exit(1);
}

static int wh_init_wl_interfaces(WhaleCompositor* comp)
{
    /* Interface for letting clients allocate surfaces & regions. */
    wlr_compositor_create(comp->display, 6, comp->renderer);

    /* Inteface for letting clients create sub-surfaces */
    wlr_subcompositor_create(comp->display);

    /* Interface for inter-process communication such as copy-past and
     * drag'n'drop */

    wlr_data_device_manager_create(comp->display);

    // struct wlr_data_control_device_v1* data_ctrl =
    //     wlr_data_control_manager_v1_create(comp->display);

    return 0;
}

WhaleCompositor g_comp;

int main(int, char**)
{
    if (!getenv("XDG_RUNTIME_DIR"))
        die("Wayland needs XDG_RUNTIME_DIR env variable!");

    g_comp.display = wl_display_create();
    if (!g_comp.display)
        die("Failed to create wayland display!");

    g_comp.backend = wlr_backend_autocreate(
        wl_display_get_event_loop(g_comp.display), &g_comp.session
    );
    if (!g_comp.backend)
        die("Failed to create wlr backend!");

    g_comp.renderer = wlr_renderer_autocreate(g_comp.backend);
    if (!g_comp.renderer)
        die("Failed to create wlr renderer!");

    // DWL creates the dmabuf manually to integrate it with the scene??
    wlr_renderer_init_wl_display(g_comp.renderer, g_comp.display);

    g_comp.allocator =
        wlr_allocator_autocreate(g_comp.backend, g_comp.renderer);
    if (!g_comp.allocator)
        die("Failed to create wlr renderer allocator!");

    if (wh_init_wl_interfaces(&g_comp) < 0)
        die("Failed to init some interfaces.");

    int st;
    if ((st = wh_output_ss_init(&g_comp)) < 0)
        return -st;

    if ((st = wh_client_ss_init(&g_comp)) < 0)
        return -st;

    if ((st = wh_input_init(&g_comp)) < 0)
        return -st;

    // RUN()
    const char* socket = wl_display_add_socket_auto(g_comp.display);
    if (!socket)
        die("Failed to create Wayland socket!");

    setenv("WAYLAND_DISPLAY", socket, 1);

    /* For debugging purposes */
    if (getenv("DISPLAY"))
        unsetenv("DISPLAY");

    wh_log(INFO, "WAYLAND_DISPLAY: %s", socket);

    if (!wlr_backend_start(g_comp.backend))
        die("Failed to start wlr backend!");

    wl_display_run(g_comp.display);

    // destroy input.

    return 0;
}
