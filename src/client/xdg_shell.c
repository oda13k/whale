/**
 * Copyright Olaru Alexandru.
 * Distributed under the MIT license.
 *
 * This file implements the xdg protocols for clients, meaning xdg toplevel and
 * xdg decorations.
 */

#include "xdg_shell.h"
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <whale/client/client.h>
#include <whale/compositor.h>
#include <whale/debug.h>
#include <whale/input/pointer.h>
#include <whale/log.h>
#include <whale/output/output.h>
#include <whale/output/workspace.h>
#include <whale/types.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

static struct wlr_xdg_shell* g_xdg_shell;
static struct wlr_xdg_decoration_manager_v1* g_xdg_decoration_manager;
static struct wl_listener g_xdg_shell_new_toplevel_listener;
static struct wl_listener g_xdg_shell_new_popup_listener;
static struct wl_listener g_xdg_shell_new_toplevel_decoration_listener;

/* Modified version wl_container_of that takes in a type instead of a variable
upon which we do a typeof() */
#define CONTAINER_OF(_ptr, _sample_type, _member)                              \
    ((_sample_type*)((void*)(_ptr) - offsetof(_sample_type, _member)))

#define XDG_CLIENT_FROM_TOPLEVEL(_tl) ((XDG_Client*)(_tl)->base->data)

#define XDG_CLIENT_FROM_LISTENER(_ptr, _listener_name)                         \
    (CONTAINER_OF(_ptr, XDG_Client, listeners._listener_name))

typedef struct
{
    WhaleClient* client;
    struct wlr_xdg_toplevel* toplevel;
    struct wlr_xdg_toplevel_decoration_v1* toplevel_decoration;

    struct
    {
        struct wl_listener destroy;
        struct wl_listener request_move;
        struct wl_listener request_resize;
        struct wl_listener decoration_request_mode;
        struct wl_listener decoration_destroy;
        struct wl_listener request_fullscreen;
        struct wl_listener request_maximize;
        struct wl_listener set_parent;
    } listeners;
} XDG_Client;

/* === XDG Decorations === */
static void xdg_set_decoration_mode(XDG_Client* xdg_client)
{
    wlr_xdg_toplevel_decoration_v1_set_mode(
        xdg_client->toplevel_decoration,
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
    );
}

static void
on_xdg_toplevel_decoration_request_mode(struct wl_listener* l, void*)
{
    XDG_Client* xdg_client =
        XDG_CLIENT_FROM_LISTENER(l, decoration_request_mode);

    /* Ignore requested decoration modes, turn them off instead. */
    if (xdg_client->toplevel->base->initialized)
        xdg_set_decoration_mode(xdg_client);
}

static void on_xdg_toplevel_decoration_destroy(struct wl_listener* l, void*)
{
    XDG_Client* xdg_client = XDG_CLIENT_FROM_LISTENER(l, decoration_destroy);

    xdg_client->toplevel_decoration = nullptr;
    UNLISTEN(&xdg_client->listeners.decoration_request_mode);
    UNLISTEN(&xdg_client->listeners.decoration_destroy);
}

static void on_xdg_toplevel_decoration_new(struct wl_listener*, void* data)
{
    struct wlr_xdg_toplevel_decoration_v1* decoration = data;
    XDG_Client* xdg_client = XDG_CLIENT_FROM_TOPLEVEL(decoration->toplevel);

    xdg_client->toplevel_decoration = decoration;

    LISTEN(
        &decoration->events.request_mode,
        &xdg_client->listeners.decoration_request_mode,
        on_xdg_toplevel_decoration_request_mode
    );

    LISTEN(
        &decoration->events.destroy,
        &xdg_client->listeners.decoration_destroy,
        on_xdg_toplevel_decoration_destroy
    );

    if (xdg_client->toplevel->base->initial_commit)
        xdg_set_decoration_mode(xdg_client);
}

/* === XDG Popup === */
typedef struct
{
    WhaleSurface* surface;
    struct wlr_xdg_popup* xdg_popup;

    struct
    {
        struct wl_listener destroy;
    } listeners;
} XDG_Popup;

static void on_xdg_popup_surface_commit(WhaleSurface* surface)
{
    XDG_Popup* popup = surface->data;
    struct wlr_xdg_popup* xdg_popup = popup->xdg_popup;

    if (xdg_popup->base->initial_commit)
    {
        wlr_xdg_surface_schedule_configure(xdg_popup->base);
        return;
    }

    struct wlr_xdg_surface* xdg_surf =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);

    WH_ASSERT_SANITY(xdg_surf);

    WhalePosition2D pos = {
        .x = xdg_popup->scheduled.geometry.x + xdg_surf->geometry.x,
        .y = xdg_popup->scheduled.geometry.y + xdg_surf->geometry.y
    };

    wh_surface_set_pos(&pos, surface);

    /* Reposition popup if it goes off the bounding output. Maybe we
     * should do it for the bounding client instead? */
    WhaleClient* client = wh_client_from_surface(surface);
    if (!client->workspace)
        return;

    WhaleGeometry2D output_geom;
    wh_output_get_geometry(&output_geom, client->workspace->parent_output);

    WhaleSize2D size = {
        .w = xdg_popup->base->geometry.width,
        .h = xdg_popup->base->geometry.height
    };

    if (surface->layout_pos.x + size.w > output_geom.pos.x + output_geom.size.w)
        pos.x -= size.w;

    if (surface->layout_pos.y + size.h > output_geom.pos.y + output_geom.size.h)
        pos.y -= size.h;

    wh_surface_set_pos(&pos, surface);
}

static void on_xdg_popup_surface_destroy(WhaleSurface* surface)
{
    XDG_Popup* popup = surface->data;

    UNLISTEN(&popup->listeners.destroy);
    free(popup);
}

static void on_xdg_popup_destroy(struct wl_listener* l, void*)
{
    XDG_Popup* popup = CONTAINER_OF(l, XDG_Popup, listeners.destroy);

    wh_surface_destroy(popup->surface);
}

static void on_xdg_popup_new(struct wl_listener*, void* data)
{
    struct wlr_xdg_popup* xdg_popup = data;

    WhaleSurface* parent = wh_surface_from_wlr_surface(xdg_popup->parent);

    XDG_Popup* popup = calloc(1, sizeof(XDG_Popup));
    if (!popup)
    {
        wh_log(ERR, "xdg: Failed to allocate xdg popup.");
        return;
    }

    WhaleSurface* surface =
        wh_surface_new_child(xdg_popup->base->surface, parent);

    if (!surface)
    {
        wh_log(ERR, "xdg: Failed to create popup surface.");
        free(popup);
        return;
    }

    popup->surface = surface;
    popup->xdg_popup = xdg_popup;

    surface->type = SURFACE_TYPE_POPUP;
    surface->ignore_keyboard_focus = true;
    surface->data = popup;

    surface->commit = on_xdg_popup_surface_commit;
    surface->destroy = on_xdg_popup_surface_destroy;

    LISTEN(
        &xdg_popup->events.destroy,
        &popup->listeners.destroy,
        on_xdg_popup_destroy
    );
}

/* === XDG Client driver functions === */
static void xdg_client_set_size(const WhaleSize2D* size, WhaleClient* client)
{
    XDG_Client* xdg_client = client->driver_ctx;

    wlr_xdg_toplevel_set_size(
        xdg_client->toplevel, CAST_U32_TO_S32(size->w), CAST_U32_TO_S32(size->h)
    );
}

static void xdg_client_get_minmax_size(
    WhaleSize2D* min, WhaleSize2D* max, WhaleClient* client
)
{
    XDG_Client* xdg_client = client->driver_ctx;

    struct wlr_xdg_toplevel* toplevel = xdg_client->toplevel;
    struct wlr_xdg_toplevel_state* state = &toplevel->current;

    if (min)
    {
        min->w = CAST_S32_TO_U32(state->min_width);
        min->h = CAST_S32_TO_U32(state->min_height);
    }

    if (max)
    {
        max->w = CAST_S32_TO_U32(state->max_width);
        max->h = CAST_S32_TO_U32(state->max_height);
    }
}

static void xdg_client_set_active(bool active, WhaleClient* client)
{
    XDG_Client* xdg_client = client->driver_ctx;

    wlr_xdg_toplevel_set_activated(xdg_client->toplevel, active);
}

static void xdg_client_set_tiled(bool tiled, WhaleClient* client)
{
    XDG_Client* xdg_client = client->driver_ctx;

    u32 edges;
    if (tiled)
        edges = WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT;
    else
        edges = WLR_EDGE_NONE;

    wlr_xdg_toplevel_set_tiled(xdg_client->toplevel, edges);
}

static void xdg_toplevel_update_wants_floating(XDG_Client* xdg_client)
{
    WhaleClient* client = xdg_client->client;

    WhaleSize2D min, max;
    xdg_client_get_minmax_size(&min, &max, client);

    bool demands_size = (min.w && min.w == max.w) || (min.h && min.h == max.h);

    WhaleClient* parent =
        xdg_client->toplevel->parent
            ? XDG_CLIENT_FROM_TOPLEVEL(xdg_client->toplevel->parent)->client
            : nullptr;

    if (!parent && !demands_size)
        return;

    wh_client_set_layer(WH_LAYER_FLOATING, client);

    WhaleGeometry2D bounds;
    if (parent)
        wh_client_get_geometry(&bounds, parent);
    else if (client->workspace)
        wh_output_get_geometry(&bounds, client->workspace->parent_output);
    else
        return;

    // FIXME: make this a "client notify surface size"
    WhaleSize2D size = {
        .w = xdg_client->toplevel->base->geometry.width,
        .h = xdg_client->toplevel->base->geometry.height
    };
    wh_client_set_size(&size, client);

    WhalePosition2D pos = {
        .x = bounds.pos.x + bounds.size.w / 2.0 - size.w / 2.0,
        .y = bounds.pos.y + bounds.size.h / 2.0 - size.h / 2.0
    };

    wh_client_set_pos(&pos, client);
}

static void xdg_client_on_map(WhaleClient* client)
{
    xdg_toplevel_update_wants_floating(client->driver_ctx);
}

static void xdg_client_on_commit(WhaleClient* client)
{
    XDG_Client* xdg_client = client->driver_ctx;

    if (xdg_client->toplevel->base->initial_commit)
    {
        if (xdg_client->toplevel_decoration)
            xdg_set_decoration_mode(xdg_client);

        if (xdg_client->toplevel->title &&
            !strcmp(xdg_client->toplevel->title, "wl-clipboard"))
            wh_client_set_layer(WH_LAYER_FLOATING, xdg_client->client);

        wlr_xdg_toplevel_set_wm_capabilities(
            xdg_client->toplevel, WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN
        );

        wlr_xdg_surface_schedule_configure(xdg_client->toplevel->base);
        return;
    }

    struct wlr_xdg_surface* xdgsurf = xdg_client->toplevel->base;

    if (xdgsurf->current.committed & WLR_XDG_SURFACE_STATE_WINDOW_GEOMETRY)
    {
        WhaleClient* client = xdg_client->client;

        WhalePosition2D pos = {
            .x = -xdg_client->toplevel->base->geometry.x,
            .y = -xdg_client->toplevel->base->geometry.y
        };
        wh_surface_set_pos(&pos, client->surface);

        WhaleSize2D size = {
            .w = CAST_S32_TO_U32(xdg_client->toplevel->base->geometry.width),
            .h = CAST_S32_TO_U32(xdg_client->toplevel->base->geometry.height)
        };
        if (client->size.w != size.w || client->size.h != size.h)
            xdg_toplevel_update_wants_floating(xdg_client);
    }
}

static void xdg_client_close(WhaleClient* client)
{
    XDG_Client* xdg_client = client->driver_ctx;

    wlr_xdg_toplevel_send_close(xdg_client->toplevel);
}

/* === XDG Toplevel listeners === */
static void on_xdg_toplevel_destroy(struct wl_listener* l, void*)
{
    XDG_Client* xdg_client = XDG_CLIENT_FROM_LISTENER(l, destroy);

    WhaleClient* client = xdg_client->client;

    UNLISTEN(&xdg_client->listeners.destroy);
    UNLISTEN(&xdg_client->listeners.request_move);
    UNLISTEN(&xdg_client->listeners.request_resize);
    UNLISTEN(&xdg_client->listeners.request_fullscreen);
    UNLISTEN(&xdg_client->listeners.request_maximize);
    UNLISTEN(&xdg_client->listeners.set_parent);

    free(xdg_client);

    wh_client_destroy(client);
}

static void on_xdg_toplevel_request_move(struct wl_listener* l, void* data)
{
    XDG_Client* xdg_client = XDG_CLIENT_FROM_LISTENER(l, request_move);
    struct wlr_xdg_toplevel_move_event* ev = data;

    for (size_t i = 0; i < ev->seat->seat->pointer_state.button_count; ++i)
    {
        if (ev->seat->seat->pointer_state.buttons[i].button == BTN_LEFT)
        {
            wh_pointer_start_interactive_move(xdg_client->client->surface);
            return;
        }
    }
}

static void on_xdg_toplevel_request_resize(struct wl_listener* l, void* data)
{
    XDG_Client* xdg_client = XDG_CLIENT_FROM_LISTENER(l, request_resize);
    struct wlr_xdg_toplevel_resize_event* ev = data;

    for (size_t i = 0; i < ev->seat->seat->pointer_state.button_count; ++i)
    {
        if (ev->seat->seat->pointer_state.buttons[i].button == BTN_LEFT)
        {
            wh_pointer_start_interactive_resize(
                ev->edges, xdg_client->client->surface
            );
            return;
        }
    }
}

static void on_xdg_toplevel_request_fullscreen(struct wl_listener* l, void*)
{
    XDG_Client* xdg_client = XDG_CLIENT_FROM_LISTENER(l, request_fullscreen);
    WhaleClient* client = xdg_client->client;
    bool fullscreen = xdg_client->toplevel->requested.fullscreen;

    if (fullscreen)
        wh_client_set_layer(WH_LAYER_FULLSCREEN, client);
    else if (client->layer == WH_LAYER_FULLSCREEN)
        wh_client_restore_prev_layer(client);

    /* Cool and funny note: If we don't actually honor this request
    (we can't just send a simple configure notify) firefox shits it's
    pants and it's UI stops working after requesting a fullscreen :^) */
    if (xdg_client->toplevel->base->initialized)
        wlr_xdg_toplevel_set_fullscreen(xdg_client->toplevel, fullscreen);
}

static void on_xdg_toplevel_request_maximize(struct wl_listener* l, void*)
{
    XDG_Client* xdg_client = XDG_CLIENT_FROM_LISTENER(l, request_maximize);

    if (xdg_client->toplevel->base->initialized)
        wlr_xdg_surface_schedule_configure(xdg_client->toplevel->base);
}

static void on_xdg_toplevel_set_parent(struct wl_listener* l, void*)
{
    XDG_Client* xdg_client = XDG_CLIENT_FROM_LISTENER(l, set_parent);

    if (xdg_client->toplevel->base->initialized)
        xdg_toplevel_update_wants_floating(xdg_client);
}

static void on_xdg_toplevel_new(struct wl_listener*, void* data)
{
    static const WhaleClientDriver xdg_client_driver = {
        .set_active = xdg_client_set_active,
        .set_tiled = xdg_client_set_tiled,
        .set_size = xdg_client_set_size,
        .get_minmax_size = xdg_client_get_minmax_size,
        .map = xdg_client_on_map,
        .commit = xdg_client_on_commit,
        .close = xdg_client_close
    };

    struct wlr_xdg_toplevel* toplevel = data;

    XDG_Client* xdg_client = calloc(1, sizeof(XDG_Client));
    if (!xdg_client)
    {
        wh_log(ERR, "xdg: Failed to allocate xdg toplevel data.");
        return;
    }

    WhaleClient* client = wh_client_new(&xdg_client_driver, xdg_client);
    if (!client)
    {
        wh_log(ERR, "xdg: Failed to create client.");
        free(xdg_client);
        return;
    }

    if (wh_client_attach_surface(toplevel->base->surface, client) < 0)
    {
        wh_log(ERR, "xdg: Failed to attach surface to client.");
        free(xdg_client);
        wh_client_destroy(client);
        return;
    }

    xdg_client->client = client;
    xdg_client->toplevel = toplevel;
    /* The xdg surface can point back to the client */
    xdg_client->toplevel->base->data = xdg_client;

    LISTEN(
        &xdg_client->toplevel->events.destroy,
        &xdg_client->listeners.destroy,
        on_xdg_toplevel_destroy
    );

    LISTEN(
        &xdg_client->toplevel->events.request_move,
        &xdg_client->listeners.request_move,
        on_xdg_toplevel_request_move
    );

    LISTEN(
        &xdg_client->toplevel->events.request_resize,
        &xdg_client->listeners.request_resize,
        on_xdg_toplevel_request_resize
    );

    LISTEN(
        &xdg_client->toplevel->events.request_fullscreen,
        &xdg_client->listeners.request_fullscreen,
        on_xdg_toplevel_request_fullscreen
    );

    LISTEN(
        &xdg_client->toplevel->events.set_parent,
        &xdg_client->listeners.set_parent,
        on_xdg_toplevel_set_parent
    );

    LISTEN(
        &xdg_client->toplevel->events.request_maximize,
        &xdg_client->listeners.request_maximize,
        on_xdg_toplevel_request_maximize
    );
}

int wh_client_xdg_shell_init()
{
    g_xdg_shell = wlr_xdg_shell_create(wh_compositor_get_wl_display(), 6);
    if (!g_xdg_shell)
    {
        wh_log(ERR, "xdg-shell: Failed to create xdg shell global!");
        return -1;
    }

    LISTEN(
        &g_xdg_shell->events.new_toplevel,
        &g_xdg_shell_new_toplevel_listener,
        on_xdg_toplevel_new
    );

    LISTEN(
        &g_xdg_shell->events.new_popup,
        &g_xdg_shell_new_popup_listener,
        on_xdg_popup_new
    );

    /* This is the intended client decoration protocol. */
    g_xdg_decoration_manager =
        wlr_xdg_decoration_manager_v1_create(wh_compositor_get_wl_display());
    if (!g_xdg_decoration_manager)
    {
        /* wlroots doesn't have a way to unregister globals :^) */
        wh_client_xdg_shell_destroy();
        wh_log(
            ERR, "xdg-shell: Failed to create xdg decoration manager global!"
        );
        return -1;
    }

    LISTEN(
        &g_xdg_decoration_manager->events.new_toplevel_decoration,
        &g_xdg_shell_new_toplevel_decoration_listener,
        on_xdg_toplevel_decoration_new
    );

    return 0;
}

void wh_client_xdg_shell_destroy()
{
    if (g_xdg_decoration_manager)
    {
        g_xdg_decoration_manager = nullptr;
        UNLISTEN(&g_xdg_shell_new_toplevel_decoration_listener);
    }

    if (g_xdg_shell)
    {
        g_xdg_shell = nullptr;
        UNLISTEN(&g_xdg_shell_new_popup_listener);
        UNLISTEN(&g_xdg_shell_new_toplevel_listener);
    }
}
