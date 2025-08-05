
#define WLR_USE_UNSTABLE
#include <signal.h>
#include <stdlib.h>
#include <whale/compositor.h>
#include <whale/input.h>
#include <whale/log.h>
#include <whale/types.h>
#include <whale/utils.h>
#include <whale/window/client.h>
#include <whale/window/xdg.h>
#include <wlr/types/wlr_server_decoration.h>

#define CONTAINER_OF(_ptr, _sample_type, _member)                              \
    ((_sample_type*)((char*)(_ptr) - offsetof(_sample_type, _member)))

#define WH_SURFACE_FROM_SURFACE_LISTENER(_ptr, _listener_name)                 \
    (CONTAINER_OF(_ptr, WhaleSurface, listeners._listener_name))

static WhaleCompositor* g_comp;

static VEC(WhaleClient*) g_clients;

int wh_client_ss_init(WhaleCompositor* comp)
{
    g_comp = comp;

    VEC_INIT(&g_clients);

    /* This protocol is obsolete, but untill it is removed,
    we'll support it. */

    wh_client_xdg_shell_init(comp);

    return 0;
}

void wh_client_ss_destroy(WhaleCompositor* comp)
{
    wh_client_xdg_shell_destroy(comp);
}

WhaleClient* wh_client_new(struct wlr_surface* wlr_surface)
{
    WhaleClient* client = calloc(1, sizeof(WhaleClient));
    if (!client)
    {
        wh_log(ERR, "client: Failed to allocate client.");
        return nullptr;
    }

    /* Attach the client to the root scene tree. */
    client->scene_tree = wlr_scene_tree_create(&g_comp->root_scene->tree);
    if (!client->scene_tree)
    {
        wh_log(ERR, "client: Failed to allocate scene tree.");
        free(client);
        return nullptr;
    }

    client->surface = wh_surface_new(wlr_surface, client->scene_tree);
    if (!client->surface)
    {
        wh_log(ERR, "client: Failed to allocate surface.");
        wlr_scene_node_destroy(&client->scene_tree->node);
        free(client);
        return nullptr;
    }

    client->surface->focus_type =
        SURFACE_FOCUS_POINTER | SURFACE_FOCUS_KEYBOARD;

    /* Unmap the client by default */
    wh_surface_unmap(client->surface);

    /* Bind the client to an output (if any) */
    // wh_workspace_bind_client_auto(client);

    /* Keep track of this client */
    VEC_PUSH(client, &g_clients);

    return client;
}

void wh_client_destroy(WhaleClient* client)
{
    /* We don't arrange the workspace here, it was already re-arranged when the
     * client was unmapped before getting destoryed. */
    // wh_workspace_unbind_client(client);

    wh_surface_destroy(client->surface);
    wlr_scene_node_destroy(&client->scene_tree->node);
    free(client);
    VEC_REMOVE(client, &g_clients);
}

void wh_client_set_title(const char* title, WhaleClient* client)
{
    client->title = title;
}

bool wh_client_is_mapped(const WhaleClient* client)
{
    return client->scene_tree->node.enabled;
}

/* Utilities */
void wh_client_set_pos(const wh_pos2d_t* pos, WhaleClient* client)
{
    // FIXME: this is the client node, but we map/unmap the surface node.
    wlr_scene_node_set_position(&client->scene_tree->node, pos->x, pos->y);
}

wh_pos2d_t wh_client_get_pos(WhaleClient* client)
{
    return (wh_pos2d_t){.x = client->scene_tree->node.x,
                        .y = client->scene_tree->node.y};
}
