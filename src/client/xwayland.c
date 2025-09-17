
#include "xwayland.h"
#include <whale/compositor.h>
#include <whale/log.h>
#include <wlr/xwayland.h>

static struct wlr_xwayland* g_xwayland;

WH_CALLBACK(xwayland_ready, struct wl_listener*, void*) {}

WH_CALLBACK(xwayland_new_surface, struct wl_listener*, void*) {}

int wh_xwayland_init()
{
    g_xwayland = wlr_xwayland_create(
        wh_compositor_get_wl_display(),
        wh_compositor_get_wlr_compositor(),
        false
    );

    if (!g_xwayland)
    {
        wh_log(ERR, "xwayland: Failed to create XWayland server.");
        return -1;
    }

    wh_log(INFO, "XWayland DISPLAY: %s", g_xwayland->display_name);
    setenv("DISPLAY", g_xwayland->display_name, true);

    WH_LISTEN(&g_xwayland->events.ready, xwayland_ready);
    WH_LISTEN(&g_xwayland->events.new_surface, xwayland_new_surface);

    return 0;
}
