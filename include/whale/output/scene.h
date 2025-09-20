
#ifndef WHALE_OUTPUT_SCENE_H
#define WHALE_OUTPUT_SCENE_H

#include <whale/client/client.h>
#include <whale/client/surface.h>
#include <whale/output/output.h>
#include <whale/types.h>
#include <wlr/types/wlr_cursor.h>

int wh_scene_init();

int wh_scene_attach_output(WhaleOutput* output, u32 x, u32 y);
int wh_scene_detach_output(WhaleOutput* output);

int wh_scene_get_output_position(
    const WhaleOutput* output, WhalePosition2D* out_pos
);

WhaleOutput* wh_scene_get_output_at(WhalePosition2D* pos);

struct wlr_scene_tree* wh_scene_tree_new();
void wh_scene_tree_set_layer(
    struct wlr_scene_tree* tree, WhaleClientLayer layer
);

int wh_scene_attach_pointer(struct wlr_cursor* cursor);

WhaleSurface* wh_scene_get_topmost_surface_at(const WhalePosition2D* pos);

#endif // !WHALE_OUTPUT_SCENE_H
