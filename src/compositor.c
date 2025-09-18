
#include <signal.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <whale/client/client.h>
#include <whale/compositor.h>
#include <whale/debug.h>
#include <whale/input/seat.h>
#include <whale/output/scene.h>
#include <wlr/backend.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>

typedef struct
{
    struct wl_display* display;
    struct wlr_backend* backend;
    struct wlr_session* session;
    struct wlr_renderer* renderer;
    struct wlr_allocator* allocator;
    struct wlr_compositor* compositor;
} WhaleCompositor;

static bool g_on_bare_metal;
static WhaleCompositor g_comp;

static void (*g_on_new_input_callback)(struct wlr_input_device* dev);
WH_CALLBACK(new_input, struct wl_listener*, void* data)
{
    g_on_new_input_callback(data);
}

static void (*g_on_new_output_callback)(struct wlr_output* output);
WH_CALLBACK(new_output, struct wl_listener*, void* data)
{
    struct wlr_output* wlr_output = data;

    /* This doesn't need a destroy counterpart if we error out in the cb. */
    if (!wlr_output_init_render(wlr_output, g_comp.allocator, g_comp.renderer))
    {
        wh_log(ERR, "compositor: Failed to init output renderer.");
        return;
    }

    g_on_new_output_callback(wlr_output);
}

static void on_close_signal(int)
{
    wl_display_terminate(g_comp.display);

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
}

static int wh_compositor_init_core_interfaces()
{
    /* Interface for letting clients allocate surfaces & regions. */
    g_comp.compositor =
        wlr_compositor_create(g_comp.display, 6, g_comp.renderer);

    /* Inteface for letting clients create sub-surfaces */
    wlr_subcompositor_create(g_comp.display);

    return 0;
}

int wh_compositor_start()
{
    wh_log_init();

    wh_debug_register_crash_handlers();

    if (!getenv("XDG_RUNTIME_DIR"))
        wh_die(false, "compositor: Wayland needs XDG_RUNTIME_DIR in env.");

    g_comp.display = wl_display_create();
    if (!g_comp.display)
        wh_die(false, "compositor: Failed to create wayland display.");

    struct wl_event_loop* event_loop =
        wl_display_get_event_loop(g_comp.display);

    g_comp.backend = wlr_backend_autocreate(event_loop, &g_comp.session);
    if (!g_comp.backend)
        wh_die(false, "compositor: Failed to create wlr backend.");

    g_on_bare_metal = wlr_backend_is_drm(g_comp.backend);

    g_comp.renderer = wlr_renderer_autocreate(g_comp.backend);
    if (!g_comp.renderer)
        wh_die(false, "compositor: Failed to create wlr renderer.");

    // DWL creates the dmabuf manually to integrate it with the scene??
    if (!wlr_renderer_init_wl_display(g_comp.renderer, g_comp.display))
        wh_die(false, "compositor: Failed to init wlr renderer.");

    g_comp.allocator =
        wlr_allocator_autocreate(g_comp.backend, g_comp.renderer);
    if (!g_comp.allocator)
        wh_die(false, "compositor: Failed to create wlr renderer allocator.");

    if (wh_compositor_init_core_interfaces() < 0)
        wh_die(false, "compositor: Failed to init core interfaces.");

    if (wh_scene_init() < 0)
        wh_die(false, "compositor: Failed to init scene subsystem.");

    if (wh_output_init() < 0)
        wh_die(false, "compositor: Failed to init output subsystem.");

    if (wh_client_ss_init() < 0)
        wh_die(false, "compositor: Failed to init client subsystem.");

    if (wh_seat_init() < 0)
        wh_die(false, "compositor: Failed to init seat subsystem.");

    // RUN()
    const char* socket = wl_display_add_socket_auto(g_comp.display);
    if (!socket)
        wh_die(false, "Failed to create Wayland socket!");

    setenv("WAYLAND_DISPLAY", socket, 1);

    /* For debugging purposes */
    if (getenv("DISPLAY"))
        unsetenv("DISPLAY");

    wh_log(INFO, "WAYLAND DISPLAY: %s", socket);

    if (!wlr_backend_start(g_comp.backend))
        wh_die(false, "Failed to start wlr backend!");

    signal(SIGINT, on_close_signal);
    signal(SIGTERM, on_close_signal);

    wl_display_run(g_comp.display);

    // TODO: cleanup
    // wh_clients_destroy();
    wh_seat_destroy();

    return 0;
}

bool wh_compositor_running_on_bare_metal()
{
    return g_on_bare_metal;
}

void wh_compositor_change_vt(u8 vt)
{
    wlr_session_change_vt(g_comp.session, vt);
}

void wh_compositor_request_exit()
{
    wl_display_terminate(g_comp.display);
}

struct wl_display* wh_compositor_get_wl_display()
{
    return g_comp.display;
}

struct wlr_compositor* wh_compositor_get_wlr_compositor()
{
    return g_comp.compositor;
}

void wh_compositor_set_new_input_callback(
    void (*cb)(struct wlr_input_device* dev)
)
{
    WH_ASSERT_SANITY(!g_on_new_input_callback);
    g_on_new_input_callback = cb;
    WH_LISTEN(&g_comp.backend->events.new_input, new_input);
}

void wh_compositor_clear_new_input_callback()
{
    WH_UNLISTEN(new_input);
    g_on_new_input_callback = nullptr;
}

void wh_compositor_set_new_output_callback(
    void (*cb)(struct wlr_output* output)
)
{
    WH_ASSERT_SANITY(!g_on_new_output_callback);
    g_on_new_output_callback = cb;
    WH_LISTEN(&g_comp.backend->events.new_output, new_output);
}

void wh_compositor_clear_new_output_callback()
{
    WH_UNLISTEN(new_output);
    g_on_new_output_callback = nullptr;
}
