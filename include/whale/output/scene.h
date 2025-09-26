
#ifndef WHALE_OUTPUT_SCENE_H
#define WHALE_OUTPUT_SCENE_H

#include <whale/client/surface.h>
#include <whale/output/output.h>
#include <whale/types.h>

int wh_scene_init();

int wh_scene_attach_output(WhaleOutput* output);
int wh_scene_detach_output(WhaleOutput* output);

struct wlr_scene_tree* wh_scene_tree_new();
void wh_scene_tree_set_layer(struct wlr_scene_tree* tree, WhaleLayer layer);

WhaleSurface* wh_scene_get_topmost_surface_at(const WhalePosition2D* pos);

#endif // !WHALE_OUTPUT_SCENE_H
