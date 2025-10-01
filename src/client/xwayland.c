
#include "xwayland.h"
#include <whale/compositor.h>
#include <whale/log.h>
#include <wlr/xwayland.h>

static struct wlr_xwayland* g_xwayland;

WH_CALLBACK(xwayland_ready, struct wl_listener*, void*) {}

    if (!xwayland_client->xsurface->parent)
        return nullptr;

    return wh_client_from_surface(
        xwayland_client->xsurface->parent->surface->data
    );
}

static void xwayland_client_map(WhaleClient* client)
{
    XWaylandClient* xwayland_client = client->driver_ctx;
    struct wlr_xwayland_surface* xsurface = xwayland_client->xsurface;

    WhaleGeometry2D geom;

    wh_log(DEBUG, "X map!");
    if (xsurface->override_redirect)
    {
        wh_client_set_layer(WH_LAYER_FLOATING, client);

        geom = (WhaleGeometry2D){
            .pos = {.x = xsurface->x, .y = xsurface->y},
            .size = {.w = xsurface->width, .h = xsurface->height}
        };

        wh_client_set_pos(&geom.pos, client);
        wh_client_set_size(&geom.size, client);

        if (!xwayland_client_OR_wants_keyboard_focus(xwayland_client))
            client->surface->implodes_on_keyboard_focus = true;
    }
    else if (xwayland_client_is_implicit_floating(xwayland_client))
    {
        wh_client_set_layer(WH_LAYER_FLOATING, client);

        geom = (WhaleGeometry2D){
            .pos = {.x = xsurface->x, .y = xsurface->y},
            .size = {.w = xsurface->width, .h = xsurface->height}
        };

        wh_client_set_pos(&geom.pos, client);
        wh_client_set_size(&geom.size, client);
    }

    wlr_xwayland_surface_configure(
        xsurface, geom.pos.x, geom.pos.y, geom.size.w, geom.size.h
    );
}

static void xwayland_client_close(WhaleClient* client)
{
    XWaylandClient* xwayland_client = client->driver_ctx;
    wlr_xwayland_surface_close(xwayland_client->xsurface);
}

static void on_xwayland_surface_associate(struct wl_listener* listener, void*)
{
    XWaylandClient* xwayland_client =
        XWAYLAND_CLIENT_FROM_LISTENER(listener, associate);

    WhaleClient* client = xwayland_client->client;
    struct wlr_xwayland_surface* xsurface = xwayland_client->xsurface;

    WH_ASSERT_SANITY(client);
    WH_ASSERT_SANITY(!client->surface);

    if (wh_client_attach_surface(xsurface->surface, client) < 0)
    {
        wh_log(ERR, "xwayland: Failed to attach surface to client.");
        return;
    }

    WhaleSurface* surface = client->surface;
    surface->driver.ctx = xwayland_client;
    surface->driver.set_size = xwayland_surface_set_size;
    surface->driver.get_size = xwayland_surface_get_size;
    surface->driver.get_minmax_size = xwayland_surface_get_minmax_size;

    WhaleGeometry2D geom;

    if (xsurface->override_redirect)
    {
        wh_client_set_layer(WH_LAYER_FLOATING, client);

        geom = (WhaleGeometry2D){
            .pos = {.x = xsurface->x, .y = xsurface->y},
            .size = {.w = xsurface->width, .h = xsurface->height}
        };

        wh_client_set_pos(&geom.pos, client);
        wh_client_set_size(&geom.size, client);

        if (!xwayland_client_OR_wants_keyboard_focus(xwayland_client))
            client->surface->implodes_on_keyboard_focus = true;
    }
    else if (xwayland_client_is_implicit_floating(xwayland_client))
    {
        wh_client_set_layer(WH_LAYER_FLOATING, client);

        geom = (WhaleGeometry2D){
            .pos = {.x = xsurface->x, .y = xsurface->y},
            .size = {.w = xsurface->width, .h = xsurface->height}
        };

        wh_client_set_pos(&geom.pos, client);
        wh_client_set_size(&geom.size, client);
    }

    wlr_xwayland_surface_configure(
        xsurface, geom.pos.x, geom.pos.y, geom.size.w, geom.size.h
    );
}

static void on_xwayland_surface_dissociate(struct wl_listener* listener, void*)
{
    XWaylandClient* xwayland_client =
        XWAYLAND_CLIENT_FROM_LISTENER(listener, dissociate);

    wh_client_detach_surface(xwayland_client->client);
}

static void
on_xwayland_surface_request_activate(struct wl_listener* listener, void*)
{
    XWaylandClient* xwayland_client =
        XWAYLAND_CLIENT_FROM_LISTENER(listener, request_activate);

    wlr_xwayland_surface_activate(xwayland_client->xsurface, true);
}

static void
on_xwayland_surface_request_configure(struct wl_listener* listener, void* data)
{
    XWaylandClient* xwayland_client =
        XWAYLAND_CLIENT_FROM_LISTENER(listener, request_configure);

    struct wlr_xwayland_surface_configure_event* ev = data;

    WhaleClient* client = xwayland_client->client;
    struct wlr_xwayland_surface* xsurface = xwayland_client->xsurface;

    WhaleGeometry2D geom;

    if (xsurface->override_redirect)
    {
        wh_client_set_layer(WH_LAYER_FLOATING, client);

        geom = (WhaleGeometry2D){.pos = {.x = ev->x, .y = ev->y},
                                 .size = {.w = ev->width, .h = ev->height}};

        wh_client_set_pos(&geom.pos, client);
        wh_client_set_size(&geom.size, client);

        if (!xwayland_client_OR_wants_keyboard_focus(xwayland_client))
            client->surface->implodes_on_keyboard_focus = true;
    }
    else if (xwayland_client_is_implicit_floating(xwayland_client))
    {
        wh_client_set_layer(WH_LAYER_FLOATING, client);

        geom = (WhaleGeometry2D){.pos = {.x = ev->x, .y = ev->y},
                                 .size = {.w = ev->width, .h = ev->height}};

        wh_client_set_pos(&geom.pos, client);
        wh_client_set_size(&geom.size, client);
    }
    else if (client->workspace)
    {
        // wh_workspace_arrange(client->workspace);
    }
}

static void
on_xwayland_surface_request_fullscreen(struct wl_listener* listener, void*)
{
    XWaylandClient* xwayland_client =
        XWAYLAND_CLIENT_FROM_LISTENER(listener, request_fullscreen);

    WhaleClient* client = xwayland_client->client;

    bool fullscreen = xwayland_client->xsurface->fullscreen;

    wlr_xwayland_surface_set_fullscreen(xwayland_client->xsurface, fullscreen);

    if (fullscreen)
        wh_client_set_layer(WH_LAYER_FULLSCREEN, client);
    else
        wh_client_restore_prev_layer(client);

    if (client->workspace)
        wh_workspace_arrange(client->workspace);
}

static void
on_xwayland_surface_set_override_redirect(struct wl_listener* listener, void*)
{
    XWaylandClient* xwayland_client =
        XWAYLAND_CLIENT_FROM_LISTENER(listener, set_override_redirect);

    WhaleClient* client = xwayland_client->client;

    if (xwayland_client->xsurface->override_redirect)
    {
        xwayland_handle_or_configure(xwayland_client);

        if (WH_LAYER_NEEDS_REARRANGE(client->prev_layer) && client->workspace)
            wh_workspace_arrange(client->workspace);
    }
    else
    {
        wh_client_restore_prev_layer(client);

        if (WH_LAYER_NEEDS_REARRANGE(client->layer) && client->workspace)
            wh_workspace_arrange(client->workspace);
    }
}

static void
on_xwayland_surface_set_geometry(struct wl_listener* listener, void*)
{
    XWaylandClient* xwayland_client =
        XWAYLAND_CLIENT_FROM_LISTENER(listener, set_geometry);

    struct wlr_xwayland_surface* xsurface = xwayland_client->xsurface;

    if (xsurface->override_redirect)
    {
        WhalePosition2D pos = {.x = xsurface->x, .y = xsurface->y};
        wh_client_set_pos(&pos, xwayland_client->client);
    }
}

static void on_xwayland_surface_destroy(struct wl_listener* listener, void*)
{
    XWaylandClient* xwayland_client =
        XWAYLAND_CLIENT_FROM_LISTENER(listener, destroy);

    UNLISTEN(&xwayland_client->listeners.associate);
    UNLISTEN(&xwayland_client->listeners.dissociate);
    UNLISTEN(&xwayland_client->listeners.request_activate);
    UNLISTEN(&xwayland_client->listeners.request_configure);
    UNLISTEN(&xwayland_client->listeners.request_fullscreen);
    UNLISTEN(&xwayland_client->listeners.set_override_redirect);
    UNLISTEN(&xwayland_client->listeners.set_geometry);
    UNLISTEN(&xwayland_client->listeners.destroy);

    // FIXME: maybe surface memeory leak?
    WH_ASSERT_SANITY(xwayland_client->client);
    wh_client_destroy(xwayland_client->client);

    free(xwayland_client);
}

WH_CALLBACK(xwayland_new_surface, struct wl_listener*, void* data)
{
    static const WhaleClientDriver xwayland_client_driver = {
        .set_active = xwayland_client_set_active,
        .set_tiled = xwayland_client_set_tiled,
        .get_parent = xwayland_client_get_parent,
        .map = xwayland_client_map,
        .close = xwayland_client_close
    };

    struct wlr_xwayland_surface* xsurface = data;

    XWaylandClient* xwayland_client = calloc(1, sizeof(XWaylandClient));
    if (!xwayland_client)
    {
        wh_log(ERR, "xwayland: Failed to allocate xwayland data.");
        return;
    }

    WhaleClient* client =
        wh_client_new(&xwayland_client_driver, xwayland_client);
    if (!client)
    {
        wh_log(ERR, "xwayland: Failed to create client.");
        free(xwayland_client);
        return;
    }

    xwayland_client->xsurface = xsurface;
    xwayland_client->client = client;

    LISTEN(
        &xsurface->events.associate,
        &xwayland_client->listeners.associate,
        on_xwayland_surface_associate
    );

    LISTEN(
        &xsurface->events.dissociate,
        &xwayland_client->listeners.dissociate,
        on_xwayland_surface_dissociate
    );

    LISTEN(
        &xsurface->events.request_activate,
        &xwayland_client->listeners.request_activate,
        on_xwayland_surface_request_activate
    );

    LISTEN(
        &xsurface->events.request_configure,
        &xwayland_client->listeners.request_configure,
        on_xwayland_surface_request_configure
    );

    LISTEN(
        &xsurface->events.request_fullscreen,
        &xwayland_client->listeners.request_fullscreen,
        on_xwayland_surface_request_fullscreen
    );

    LISTEN(
        &xsurface->events.set_override_redirect,
        &xwayland_client->listeners.set_override_redirect,
        on_xwayland_surface_set_override_redirect
    );

    LISTEN(
        &xsurface->events.set_geometry,
        &xwayland_client->listeners.set_geometry,
        on_xwayland_surface_set_geometry
    );

    LISTEN(
        &xsurface->events.destroy,
        &xwayland_client->listeners.destroy,
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
        [NET_WM_WINDOW_TYPE_NORMAL] = "_NET_WM_WINDOW_TYPE_NORMAL",
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

    wh_log(INFO, "XWayland DISPLAY: %s", g_xwayland->display_name);
    setenv("DISPLAY", g_xwayland->display_name, true);

    WH_LISTEN(&g_xwayland->events.ready, xwayland_ready);
    WH_LISTEN(&g_xwayland->events.new_surface, xwayland_new_surface);

    return 0;
}
