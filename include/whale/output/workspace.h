
#ifndef WHALE_WORKSPACE_H
#define WHALE_WORKSPACE_H

#include <whale/client/client.h>
#include <whale/output/layer.h>
#include <whale/types.h>
#include <whale/utils/vector.h>

struct whale_output;

typedef enum
{
    HORIZONTAL,
    VERTICAL
} WhaleTilingOrientation;

typedef struct
{
    /* How many clients should be put in the primary region. */
    u8 primary_client_count;

    /* How much of the screen the primary client(s) should occupy in the range
     * of [0-1]. */
    float primary_split;
    float primary_min_split;
    float primary_max_split;

    bool respect_client_min_size;

    WhaleTilingOrientation orientation;
    /* Automatically choose a tiling orientation based on the output's aspect
     * ratio. */
    bool dynamic_orientation;
} WhaleWorkspaceTilingConfig;

typedef struct whale_workspace
{
    struct whale_output* parent_output;

    /* The default layer is the layer clients who are placed on this workspace
     * automatically inherit if they don't already have a layer/preferences. */
    WhaleLayer default_layer;

    VEC(WhaleClient*) clients;

    WhaleWorkspaceTilingConfig tiling_config;
} WhaleWorkspace;

int wh_workspace_init(struct whale_output* parent_output, WhaleWorkspace* ws);
void wh_workspace_destroy(WhaleWorkspace* ws);

int wh_workspace_bind_client(
    struct whale_client* client,
    WhaleWorkspace* workspace
);
WhaleWorkspace* wh_workspace_unbind_client(struct whale_client* client);

void wh_workspace_activate(WhaleWorkspace* workspace);
void wh_workspace_deactivate(WhaleWorkspace* workspace);

void wh_workspace_arrange(WhaleWorkspace* ws);

void wh_workspace_step_tiling_primary_split(float step, WhaleWorkspace* ws);
void wh_workspace_step_tiling_master_client_count(s8 step, WhaleWorkspace* ws);

#endif // !WHALE_WORKSPACE_H
