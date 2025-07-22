
#ifndef WHALE_WORKSPACE_H
#define WHALE_WORKSPACE_H

#include <whale/types.h>
#include <whale/vector.h>

struct whale_client;
struct whale_output;

typedef enum
{
    /* A client's layout is undefined if it's not bound to
    an output (because maybe there are no outputs). */
    LAYOUT_UNDEFINED,
    LAYOUT_TILING,
    LAYOUT_FLOATING,
    LAYOUT_MONOCLE
} WhaleLayout;

typedef struct
{
    size_t master_client_count;
    float master_split_factor;
} WhaleLayoutTilingContext;

typedef struct
{
    struct whale_output* parent_output;

    WhaleLayout default_layout;
    VEC(struct whale_client*) clients;

    WhaleLayoutTilingContext tiling_ctx;
} WhaleWorkspace;

void wh_workspace_init(struct whale_output* parent_output, WhaleWorkspace* ws);

int wh_workspace_set_layout(WhaleLayout layout, WhaleWorkspace* output);

int wh_workspace_init_client_layout(struct whale_client* client);

int wh_workspace_set_client_layout(
    WhaleLayout new_layout, struct whale_client* client
);

int wh_workspace_bind_client_auto(struct whale_client* client);

int wh_workspace_bind_client(
    struct whale_client* client, WhaleWorkspace* workspace
);

WhaleWorkspace* wh_workspace_unbind_client(struct whale_client* client);

void wh_workspace_arrange(WhaleWorkspace* ws);

void wh_workspace_tiling_increment_master_split(float step, WhaleWorkspace* ws);

void wh_workspace_tiling_decrement_master_split(float step, WhaleWorkspace* ws);

void wh_workspace_tiling_increment_master_max_clients(
    u8 step, WhaleWorkspace* ws
);

void wh_workspace_tiling_decrement_master_max_clients(
    u8 step, WhaleWorkspace* ws
);

#endif // !WHALE_WORKSPACE_H
