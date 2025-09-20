
#ifndef WHALE_WORKSPACE_H
#define WHALE_WORKSPACE_H

#include <whale/client/client.h>
#include <whale/types.h>
#include <whale/utils/vector.h>

struct whale_output;

typedef struct
{
    u8 master_client_count;
    float master_split_factor;
} WhaleWorkspaceTilingContext;

typedef struct whale_workspace
{
    struct whale_output* parent_output;

    /* The default layer is the layer clients who are placed on this workspace
     * automatically inherit if they don't already have a layer/preferences. */
    WhaleClientLayer default_layer;

    VEC(WhaleClient*) clients;

    WhaleWorkspaceTilingContext tiling_ctx;
} WhaleWorkspace;

int wh_workspace_init(struct whale_output* parent_output, WhaleWorkspace* ws);

void wh_workspace_destroy(WhaleWorkspace* ws);

int wh_workspace_bind_client(
    struct whale_client* client, WhaleWorkspace* workspace
);

WhaleWorkspace* wh_workspace_unbind_client(struct whale_client* client);

void wh_workspace_arrange(WhaleWorkspace* ws);

void wh_workspace_step_tiling_master_split_factor(
    float step, WhaleWorkspace* ws
);
void wh_workspace_step_tiling_master_client_count(s8 step, WhaleWorkspace* ws);

#endif // !WHALE_WORKSPACE_H
