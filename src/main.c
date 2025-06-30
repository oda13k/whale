
#define WLR_USE_UNSTABLE
#include <stdio.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <whale/client/client.h>
#include <whale/compositor.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/output.h>
#include <whale/types.h>
#include <whale/utils.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
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

    return 0;
}

int main(int, char**)
{
    if (!getenv("XDG_RUNTIME_DIR"))
        die("Wayland needs XDG_RUNTIME_DIR env variable!");

    WhaleCompositor comp = {0};

    comp.display = wl_display_create();
    if (!comp.display)
        die("Failed to create wayland display!");

    comp.backend = wlr_backend_autocreate(
        wl_display_get_event_loop(comp.display), &comp.session
    );
    if (!comp.backend)
        die("Failed to create wlr backend!");

    comp.renderer = wlr_renderer_autocreate(comp.backend);
    if (!comp.renderer)
        die("Failed to create wlr renderer!");

    // DWL creates the dmabuf manually to integrate it with the scene??
    wlr_renderer_init_wl_display(comp.renderer, comp.display);

    comp.allocator = wlr_allocator_autocreate(comp.backend, comp.renderer);
    if (!comp.allocator)
        die("Failed to create wlr renderer allocator!");

    if (wh_init_wl_interfaces(&comp) < 0)
        die("Failed to init some interfaces.");

    int st;
    if ((st = wh_output_subsystem_init(&comp)) < 0)
        return -st;

    if ((st = wh_client_subsystem_init(&comp)) < 0)
        return -st;

    if ((st = wh_input_init(&comp)) < 0)
        return -st;

    // RUN()
    const char* socket = wl_display_add_socket_auto(comp.display);
    if (!socket)
        die("Failed to create Wayland socket!");

    setenv("WAYLAND_DISPLAY", socket, 1);

    wh_log(INFO, "WAYLAND_DISPLAY: %s", socket);

    if (!wlr_backend_start(comp.backend))
        die("Failed to start wlr backend!");

    wl_display_run(comp.display);

    // destroy input.

    return 0;
}
