
#define _POSIX_C_SOURCE 200112L
#define WLR_USE_UNSTABLE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include <whale/client.h>
#include <whale/compositor.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/output.h>
#include <whale/types.h>

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

    comp->xdg_shell = wlr_xdg_shell_create(comp->display, 6);
    LISTEN(
        &comp->xdg_shell->events.new_toplevel,
        &comp->listeners.xdg_new_toplevel,
        wh_client_on_new_client
    );

    return 0;
}

static int wh_spawn_process(const char* pathname)
{
    const pid_t pid = fork();
    if (pid < 0)
    {
        wh_log(ERR, "Failed to spawn process %s", pathname);
        return -1;
    }

    if (pid > 0)
        return 0; // parent, ok.

    /* child, close stdout, in and err on exec. */
    fcntl(fileno(stdin), F_SETFD, FD_CLOEXEC);
    fcntl(fileno(stdout), F_SETFD, FD_CLOEXEC);
    fcntl(fileno(stderr), F_SETFD, FD_CLOEXEC);

    char* const argv[] = {(char* const)pathname, NULL};
    execv(pathname, argv);

    exit(0);
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

    comp.root_scene = wlr_scene_create();

    float color[] = {0x12 / 255.f, 0x12 / 255.f, 0x12 / 255.f, 0xFF / 255.f};
    comp.root_bg_rect =
        wlr_scene_rect_create(&comp.root_scene->tree, 0, 0, color);

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

    /* An output layout is all of the outputs arranged into a
     * 2D coordinate space */
    comp.output_layout = wlr_output_layout_create(comp.display);
    LISTEN(
        &comp.output_layout->events.change,
        &comp.listeners.output_layout_change,
        wh_output_layout_on_change
    );

    /* List keeping track of all monitors */
    wl_list_init(&comp.outputs);
    LISTEN(
        &comp.backend->events.new_output,
        &comp.listeners.new_output,
        wh_output_on_new_output
    );

    /* List keeping track of all clients */
    wl_list_init(&comp.clients);

    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(comp.display),
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER
    );
    comp.xdg_decoration_manager =
        wlr_xdg_decoration_manager_v1_create(comp.display);
    LISTEN(
        &comp.xdg_decoration_manager->events.new_toplevel_decoration,
        &comp.listeners.xdg_new_decoration,
        wh_client_on_new_xdg_decoration
    );

    wh_input_init(&comp);

    // RUN()
    const char* socket = wl_display_add_socket_auto(comp.display);
    if (!socket)
        die("Failed to create Wayland socket!");

    setenv("WAYLAND_DISPLAY", socket, 1);

    wh_log(INFO, "WAYLAND_DISPLAY: %s", socket);

    if (!wlr_backend_start(comp.backend))
        die("Failed to start wlr backend!");

    wh_spawn_process("/bin/alacritty");

    wl_display_run(comp.display);

    return 0;
}
