
#ifndef WHALE_WORKSPACE_H
#define WHALE_WORKSPACE_H

#include <whale/types.h>
#include <whale/utils/vector.h>

struct whale_client;
struct whale_output;

typedef enum
{
    /* A client with this layout means it has never been bound to a ws. */
    LAYOUT_UNDEFINED,
    LAYOUT_TILING,
    LAYOUT_FLOATING,
    LAYOUT_FULLSCREEN
} WhaleLayout;

typedef struct
{
    u8 master_client_count;
    float master_split_factor;
} WhaleLayoutTilingContext;

typedef struct
{
    struct whale_output* parent_output;

    WhaleLayout default_layout;
    VEC(struct whale_client*) clients;

    WhaleLayoutTilingContext tiling_ctx;
} WhaleWorkspace;

int wh_workspace_init(struct whale_output* parent_output, WhaleWorkspace* ws);

void wh_workspace_destroy(WhaleWorkspace* ws);

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
