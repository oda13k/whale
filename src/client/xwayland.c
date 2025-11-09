
#include "xwayland.h"
#include <security/pam_appl.h>
#include <string.h>
#include <whale/client/client.h>
#include <whale/compositor.h>
#include <whale/input/keyboard.h>
#include <whale/input/pointer.h>
#include <whale/input/seat.h>
#include <whale/log.h>
#include <whale/output/workspace.h>
#include <whale/utils/env.h>
#include <wlr/xwayland.h>
#include <wlr/xwayland/xwayland.h>

/* Modified version wl_container_of that takes in a type instead of a variable
upon which we do a typeof() */
#define CONTAINER_OF(_ptr, _sample_type, _member)                              \
    ((_sample_type*)((void*)(_ptr) - offsetof(_sample_type, _member)))

#define XCLIENT_FROM_LISTENER(_ptr, _listener_name)                            \
    (CONTAINER_OF(_ptr, XWaylandClient, listeners._listener_name))

typedef struct
{
    WhaleClient* client;
    struct wlr_xwayland_surface* xsurface;

    struct
    {
        struct wl_listener associate;
        struct wl_listener dissociate;
        struct wl_listener request_activate;
        struct wl_listener request_configure;
        struct wl_listener request_fullscreen;
        struct wl_listener request_minimize;
        struct wl_listener request_maximize;
        struct wl_listener request_move;
        struct wl_listener set_override_redirect;
        struct wl_listener set_geometry;
        struct wl_listener destroy;
    } listeners;
} XWaylandClient;

typedef enum
{
    NET_WM_WINDOW_TYPE_DESKTOP,
    NET_WM_WINDOW_TYPE_DOCK,
    NET_WM_WINDOW_TYPE_TOOLBAR,
    NET_WM_WINDOW_TYPE_MENU,
    NET_WM_WINDOW_TYPE_UTILITY,
    NET_WM_WINDOW_TYPE_SPLASH,
    NET_WM_WINDOW_TYPE_DIALOG,
    NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
    NET_WM_WINDOW_TYPE_POPUP_MENU,
    NET_WM_WINDOW_TYPE_TOOLTIP,
    NET_WM_WINDOW_TYPE_NOTIFICATION,
    NET_WM_WINDOW_TYPE_COMBO,
    NET_WM_WINDOW_TYPE_DND,
    NET_WM_WINDOW_TYPE_NORMAL,
    XWAYLAND_ATOM_COUNT
} XWaylandAtomName;

static struct wlr_xwayland* g_xwayland;
static xcb_atom_t g_xwayland_atoms[XWAYLAND_ATOM_COUNT];

static bool
xclient_has_window_type(XWaylandAtomName atom_name, XWaylandClient* client)
{
    for (size_t i = 0; i < client->xsurface->window_type_len; ++i)
    {
        if (g_xwayland_atoms[atom_name] == client->xsurface->window_type[i])
            return true;
    }

    return false;
}

static bool
xclient_override_redirect_wants_keyboard_focus(XWaylandClient* client)
{
    static const XWaylandAtomName needles[] = {
        NET_WM_WINDOW_TYPE_COMBO,
        NET_WM_WINDOW_TYPE_DND,
        NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
        NET_WM_WINDOW_TYPE_MENU,
        NET_WM_WINDOW_TYPE_NOTIFICATION,
        NET_WM_WINDOW_TYPE_POPUP_MENU,
        NET_WM_WINDOW_TYPE_SPLASH,
        NET_WM_WINDOW_TYPE_DESKTOP,
        NET_WM_WINDOW_TYPE_TOOLTIP,
        NET_WM_WINDOW_TYPE_UTILITY,
    };

    for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); ++i)
    {
        if (xclient_has_window_type(needles[i], client))
            return false;
    }

    return true;
}

static bool xclient_wants_floating(XWaylandClient* xclient)
{
    struct wlr_xwayland_surface* xsurface = xclient->xsurface;

    if (xsurface->modal)
        return true;

    if (xclient_has_window_type(NET_WM_WINDOW_TYPE_DIALOG, xclient) ||
        xclient_has_window_type(NET_WM_WINDOW_TYPE_UTILITY, xclient) ||
        xclient_has_window_type(NET_WM_WINDOW_TYPE_TOOLBAR, xclient) ||
        xclient_has_window_type(NET_WM_WINDOW_TYPE_SPLASH, xclient))
        return true;

    xcb_size_hints_t* size_hints = xsurface->size_hints;
    if (size_hints && size_hints->min_width > 0 && size_hints->min_height > 0 &&
        size_hints->max_width > 0 && size_hints->max_height > 0 &&
        ((size_hints->min_width == size_hints->max_width) ||
         (size_hints->min_height == size_hints->max_height)))
        return true;

    return false;
}

/* XWayland client driver functions */
static void
xwayland_surface_set_size(const WhaleSize2D* size, WhaleClient* client)
{
    XWaylandClient* xclient = client->driver_ctx;

    WhaleGeometry2D geom;
    wh_client_get_geometry(&geom, client);

    wlr_xwayland_surface_configure(
        xclient->xsurface, geom.pos.x, geom.pos.y, size->w, size->h
    );
}

static void xwayland_surface_get_minmax_size(
    WhaleSize2D* min, WhaleSize2D* max, WhaleClient* client
)
{
    const XWaylandClient* xclient = client->driver_ctx;
    const xcb_size_hints_t* size_hints = xclient->xsurface->size_hints;

    if (min)
    {
        if (!size_hints || size_hints->min_width == -1)
            min->w = 1;
        else
            min->w = CAST_S32_TO_U32(size_hints->min_width);

        if (!size_hints || size_hints->min_height == -1)
            min->h = 1;
        else
            min->h = CAST_S32_TO_U32(size_hints->min_height);
    }

    if (max)
    {
        if (!size_hints || size_hints->max_width == -1)
            max->w = UINT32_MAX;
        else
            max->w = CAST_S32_TO_U32(size_hints->max_width);

        if (!size_hints || size_hints->max_height == -1)
            max->h = UINT32_MAX;
        else
            max->h = CAST_S32_TO_U32(size_hints->max_height);
    }
}

static void xclient_set_active(bool active, WhaleClient* client)
{
    XWaylandClient* xclient = client->driver_ctx;

    if (active && xclient->xsurface->minimized)
        wlr_xwayland_surface_set_minimized(xclient->xsurface, false);

    wlr_xwayland_surface_activate(xclient->xsurface, active);
}

static void xclient_set_fullscreen(bool fullscreen, WhaleClient* client)
{
    XWaylandClient* xclient = client->driver_ctx;
    wlr_xwayland_surface_set_fullscreen(xclient->xsurface, fullscreen);
}

static void xclient_set_tiled(bool tiled, WhaleClient* client)
{
    XWaylandClient* xclient = client->driver_ctx;
    wlr_xwayland_surface_set_maximized(xclient->xsurface, tiled, tiled);
}

static void xclient_configure(WhaleClient* client)
{
    XWaylandClient* xclient = client->driver_ctx;

    WhaleGeometry2D geom;
    wh_client_get_geometry(&geom, client);

    wlr_xwayland_surface_configure(
        xclient->xsurface, geom.pos.x, geom.pos.y, geom.size.w, geom.size.h
    );
}

static void xclient_close(WhaleClient* client)
{
    XWaylandClient* xclient = client->driver_ctx;
    wlr_xwayland_surface_close(xclient->xsurface);
}

static void on_xwayland_surface_associate(struct wl_listener* listener, void*)
{
    XWaylandClient* xclient = XCLIENT_FROM_LISTENER(listener, associate);

    WhaleClient* client = xclient->client;
    struct wlr_xwayland_surface* xsurface = xclient->xsurface;

    WH_ASSERT_SANITY(client);
    WH_ASSERT_SANITY(!client->surface);

    if (wh_client_attach_surface(xsurface->surface, client) < 0)
    {
        wh_log(ERR, "xwayland: Failed to attach surface to client.");
        return;
    }

    if (xclient->xsurface->override_redirect)
    {
        if (xclient_has_window_type(NET_WM_WINDOW_TYPE_NOTIFICATION, xclient))
            wh_client_set_layer(WH_LAYER_NOTIFICATION, client);
        else
            wh_client_set_layer(WH_LAYER_FLOATING, client);

        WhalePosition2D pos = {.x = xsurface->x, .y = xsurface->y};
        WhaleSize2D size = {.w = xsurface->width, .h = xsurface->height};

        wh_client_set_pos(&pos, client);
        wh_client_set_size(&size, client);

        if (!xclient_override_redirect_wants_keyboard_focus(xclient))
            client->surface->ignore_keyboard_focus = true;
    }
    else if (xclient_wants_floating(xclient))
    {
        WhalePosition2D pos = {.x = xsurface->x, .y = xsurface->y};
        WhaleSize2D size = {.w = xsurface->width, .h = xsurface->height};

        wh_client_set_layer(WH_LAYER_FLOATING, client);
        wh_client_set_pos(&pos, client);
        wh_client_set_size(&size, client);
    }
}

static void on_xwayland_surface_dissociate(struct wl_listener* listener, void*)
{
    XWaylandClient* xclient = XCLIENT_FROM_LISTENER(listener, dissociate);

    if (xclient->client->surface)
        wh_client_detach_surface(xclient->client);
}

static void
on_xwayland_surface_request_activate(struct wl_listener* listener, void*)
{
    XWaylandClient* xclient = XCLIENT_FROM_LISTENER(listener, request_activate);

    xclient_set_active(true, xclient->client);
}

static void
on_xwayland_surface_request_configure(struct wl_listener* listener, void* data)
{
    XWaylandClient* xclient =
        XCLIENT_FROM_LISTENER(listener, request_configure);

    struct wlr_xwayland_surface_configure_event* ev = data;

    if (xclient->xsurface->override_redirect)
    {
        if (ev->mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y))
        {
            WhalePosition2D pos = {.x = ev->x, .y = ev->y};
            wh_client_set_pos(&pos, xclient->client);
        }

        if (ev->mask & (XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT))
        {
            WhaleSize2D size = {.w = ev->width, .h = ev->height};
            wh_client_set_size(&size, xclient->client);
        }
    }
    else if (xclient_wants_floating(xclient))
    {
        WhalePosition2D pos = {.x = ev->x, .y = ev->y};
        WhaleSize2D size = {.w = ev->width, .h = ev->height};

        wh_client_set_layer(WH_LAYER_FLOATING, xclient->client);
        wh_client_set_pos(&pos, xclient->client);
        wh_client_set_size(&size, xclient->client);
    }
    else
    {
        WhaleGeometry2D geom;
        wh_client_get_geometry(&geom, xclient->client);

        wlr_xwayland_surface_configure(
            xclient->xsurface, geom.pos.x, geom.pos.y, geom.size.w, geom.size.h
        );
    }

    // FIXME: maybe handle clients that are returning from a floating state to a
    // non floating one?
}

static void
on_xwayland_surface_request_fullscreen(struct wl_listener* listener, void*)
{
    XWaylandClient* xclient =
        XCLIENT_FROM_LISTENER(listener, request_fullscreen);

    wh_client_set_fullscreen(xclient->xsurface->fullscreen, xclient->client);
}

static void
on_xwayland_request_minimize(struct wl_listener* listener, void* data)
{
    struct wlr_xwayland_minimize_event* ev = data;

    XWaylandClient* xclient = XCLIENT_FROM_LISTENER(listener, request_minimize);

    wlr_xwayland_surface_set_minimized(xclient->xsurface, ev->minimize);
}

static void on_xwayland_request_maximize(struct wl_listener* listener, void*)
{
    XWaylandClient* xclient = XCLIENT_FROM_LISTENER(listener, request_maximize);

    wlr_xwayland_surface_set_maximized(xclient->xsurface, true, true);
}

static void on_xwayland_request_move(struct wl_listener* listener, void*)
{
    XWaylandClient* xclient = XCLIENT_FROM_LISTENER(listener, request_move);

    if (xclient->client->surface)
        wh_pointer_start_interactive_move(BTN_LEFT, xclient->client->surface);
}

static void
on_xwayland_surface_set_override_redirect(struct wl_listener* listener, void*)
{
    XWaylandClient* xclient =
        XCLIENT_FROM_LISTENER(listener, set_override_redirect);

    WhaleClient* client = xclient->client;
    struct wlr_xwayland_surface* xsurface = xclient->xsurface;

    if (xclient->xsurface->override_redirect)
    {
        if (xclient_has_window_type(NET_WM_WINDOW_TYPE_NOTIFICATION, xclient))
            wh_client_set_layer(WH_LAYER_NOTIFICATION, client);
        else
            wh_client_set_layer(WH_LAYER_FLOATING, client);

        WhalePosition2D pos = {.x = xsurface->x, .y = xsurface->y};
        WhaleSize2D size = {.w = xsurface->width, .h = xsurface->height};

        // FIXME: make this a "client notify surface size"
        wh_client_set_pos(&pos, client);
        wh_client_set_size(&size, client);

        if (!xclient_override_redirect_wants_keyboard_focus(xclient) &&
            client->surface)
            client->surface->ignore_keyboard_focus = true;
    }
    else
    {
        wh_client_restore_prev_layer(client);
        if (client->surface)
            client->surface->ignore_keyboard_focus = false;
    }
}

static void on_xwayland_set_geometry(struct wl_listener* listener, void*)
{
    XWaylandClient* xclient = XCLIENT_FROM_LISTENER(listener, set_geometry);

    if (!xclient->xsurface->override_redirect)
        return;

    WhalePosition2D pos = {
        .x = xclient->xsurface->x, .y = xclient->xsurface->y
    };
    wh_client_set_pos(&pos, xclient->client);
}

static void on_xwayland_surface_destroy(struct wl_listener* listener, void*)
{
    XWaylandClient* xclient = XCLIENT_FROM_LISTENER(listener, destroy);

    UNLISTEN(&xclient->listeners.associate);
    UNLISTEN(&xclient->listeners.dissociate);
    UNLISTEN(&xclient->listeners.request_activate);
    UNLISTEN(&xclient->listeners.request_configure);
    UNLISTEN(&xclient->listeners.request_fullscreen);
    UNLISTEN(&xclient->listeners.request_minimize);
    UNLISTEN(&xclient->listeners.request_maximize);
    UNLISTEN(&xclient->listeners.request_move);
    UNLISTEN(&xclient->listeners.set_override_redirect);
    UNLISTEN(&xclient->listeners.set_geometry);
    UNLISTEN(&xclient->listeners.destroy);

    wh_client_destroy(xclient->client);

    free(xclient);
}

WH_CALLBACK(xwayland_new_surface, struct wl_listener*, void* data)
{
    static const WhaleClientDriver xclient_driver = {
        .set_active = xclient_set_active,
        .set_fullscreen = xclient_set_fullscreen,
        .set_tiled = xclient_set_tiled,
        .set_size = xwayland_surface_set_size,
        .get_minmax_size = xwayland_surface_get_minmax_size,
        .configure = xclient_configure,
        .close = xclient_close
    };

    struct wlr_xwayland_surface* xsurface = data;

    XWaylandClient* xclient = calloc(1, sizeof(XWaylandClient));
    if (!xclient)
    {
        wh_log(ERR, "xwayland: Failed to allocate xwayland data.");
        return;
    }

    WhaleClient* client = wh_client_new(&xclient_driver, xclient);
    if (!client)
    {
        wh_log(ERR, "xwayland: Failed to create client.");
        free(xclient);
        return;
    }

    xclient->xsurface = xsurface;
    xclient->client = client;

    xsurface->data = xclient;

    LISTEN(
        &xsurface->events.associate,
        &xclient->listeners.associate,
        on_xwayland_surface_associate
    );

    LISTEN(
        &xsurface->events.dissociate,
        &xclient->listeners.dissociate,
        on_xwayland_surface_dissociate
    );

    LISTEN(
        &xsurface->events.request_activate,
        &xclient->listeners.request_activate,
        on_xwayland_surface_request_activate
    );

    LISTEN(
        &xsurface->events.request_configure,
        &xclient->listeners.request_configure,
        on_xwayland_surface_request_configure
    );

    LISTEN(
        &xsurface->events.request_fullscreen,
        &xclient->listeners.request_fullscreen,
        on_xwayland_surface_request_fullscreen
    );

    LISTEN(
        &xsurface->events.request_minimize,
        &xclient->listeners.request_minimize,
        on_xwayland_request_minimize
    );

    LISTEN(
        &xsurface->events.request_maximize,
        &xclient->listeners.request_maximize,
        on_xwayland_request_maximize
    );

    LISTEN(
        &xsurface->events.request_move,
        &xclient->listeners.request_move,
        on_xwayland_request_move
    );

    LISTEN(
        &xsurface->events.set_override_redirect,
        &xclient->listeners.set_override_redirect,
        on_xwayland_surface_set_override_redirect
    );

    LISTEN(
        &xsurface->events.set_geometry,
        &xclient->listeners.set_geometry,
        on_xwayland_set_geometry
    );

    LISTEN(
        &xsurface->events.destroy,
        &xclient->listeners.destroy,
        on_xwayland_surface_destroy
    );
}

WH_CALLBACK(xwayland_ready, struct wl_listener*, void*)
{
    wlr_xwayland_set_seat(g_xwayland, wh_seat_get());

    struct wlr_xcursor* xcursor = wh_pointer_get_texture("default");
    if (xcursor)
    {
        struct wlr_xcursor_image* img = xcursor->images[0];
        wlr_xwayland_set_cursor(
            g_xwayland,
            img->buffer,
            img->width * 4,
            img->width,
            img->height,
            img->hotspot_x,
            img->hotspot_y
        );
    }

    const char* g_atom_map[XWAYLAND_ATOM_COUNT] = {
        [NET_WM_WINDOW_TYPE_DESKTOP] = "_NET_WM_WINDOW_TYPE_DESKTOP",
        [NET_WM_WINDOW_TYPE_DOCK] = "_NET_WM_WINDOW_TYPE_DOCK",
        [NET_WM_WINDOW_TYPE_TOOLBAR] = "_NET_WM_WINDOW_TYPE_TOOLBAR",
        [NET_WM_WINDOW_TYPE_MENU] = "_NET_WM_WINDOW_TYPE_MENU",
        [NET_WM_WINDOW_TYPE_UTILITY] = "_NET_WM_WINDOW_TYPE_UTILITY",
        [NET_WM_WINDOW_TYPE_SPLASH] = "_NET_WM_WINDOW_TYPE_SPLASH",
        [NET_WM_WINDOW_TYPE_DIALOG] = "_NET_WM_WINDOW_TYPE_DIALOG",
        [NET_WM_WINDOW_TYPE_DROPDOWN_MENU] =
            "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
        [NET_WM_WINDOW_TYPE_POPUP_MENU] = "_NET_WM_WINDOW_TYPE_POPUP_MENU",
        [NET_WM_WINDOW_TYPE_TOOLTIP] = "_NET_WM_WINDOW_TYPE_TOOLTIP",
        [NET_WM_WINDOW_TYPE_NOTIFICATION] = "_NET_WM_WINDOW_TYPE_NOTIFICATION",
        [NET_WM_WINDOW_TYPE_COMBO] = "_NET_WM_WINDOW_TYPE_COMBO",
        [NET_WM_WINDOW_TYPE_DND] = "_NET_WM_WINDOW_TYPE_DND",
        [NET_WM_WINDOW_TYPE_NORMAL] = "_NET_WM_WINDOW_TYPE_NORMAL"
    };

    xcb_connection_t* xcb_con = wlr_xwayland_get_xwm_connection(g_xwayland);
    WH_ASSERT_SANITY(xcb_con);

    for (size_t i = 0; i < XWAYLAND_ATOM_COUNT; ++i)
    {
        xcb_intern_atom_cookie_t cookie = xcb_intern_atom(
            xcb_con, false, strlen(g_atom_map[i]), g_atom_map[i]
        );

        xcb_generic_error_t* err = nullptr;
        xcb_intern_atom_reply_t* reply =
            xcb_intern_atom_reply(xcb_con, cookie, &err);

        if (err)
        {
            wh_log(
                ERR,
                "xwayland: Failed to get atom '%s'. Error: %d.",
                g_atom_map[i],
                err->error_code
            );
            free(err);
            break;
        }

        if (reply)
        {
            g_xwayland_atoms[i] = reply->atom;
            free(reply);
        }
    }
}

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

    WH_LISTEN(&g_xwayland->events.ready, xwayland_ready);
    WH_LISTEN(&g_xwayland->events.new_surface, xwayland_new_surface);

    wh_setenv("DISPLAY", g_xwayland->display_name, true);

    return 0;
}
