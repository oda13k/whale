
#define WLR_USE_UNSTABLE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-util.h>
#include <whale/client/client.h>
#include <whale/compositor.h>
#include <whale/log.h>
#include <whale/output.h>
#include <whale/types.h>
#include <wlr/backend.h>

static void on_output_frame(struct wl_listener* listener, void*)
{
    WhaleOutput* output = wl_container_of(listener, output, listener_frame);

    wlr_scene_output_commit(output->scene_output, NULL);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    wlr_scene_output_send_frame_done(output->scene_output, &ts);
}

static void on_output_destroy(struct wl_listener* listener, void*)
{
    WhaleOutput* output = wl_container_of(listener, output, listener_destroy);

    wl_list_remove(&output->link);

    // FIXME: destroy/move bound clients

    wlr_output_layout_remove(output->comp->output_layout, output->wlr_output);
    wlr_scene_output_destroy(output->scene_output);

    UNLISTEN(&output->listener_destroy);
    UNLISTEN(&output->listener_frame);
    UNLISTEN(&output->listener_request_state);
    output->wlr_output->data = NULL;

    free(output);
}

static void on_output_request_state(struct wl_listener*, void* data)
{
    /* The monitor is asking us that it wants this state */
    struct wlr_output_event_request_state* event = data;
    wlr_output_commit_state(event->output, event->state);

    wh_log(
        DEBUG,
        "output (%s) @ %dx%d",
        event->output->name,
        event->output->width,
        event->output->height
    );
}

static WhaleOutput* wh_output_from_wlr_output(struct wlr_output* output)
{
    return output->data;
}

static int wh_output_set_mode(WhaleOutput* output)
{
    /* Set the monitor to it's prefered state */
    struct wlr_output_state state;
    wlr_output_state_init(&state);

    wlr_output_state_set_enabled(&state, true);
    wlr_output_state_set_scale(&state, 1);
    wlr_output_state_set_mode(
        &state, wlr_output_preferred_mode(output->wlr_output)
    );
    wlr_output_state_set_transform(&state, WL_OUTPUT_TRANSFORM_NORMAL);

    wlr_output_commit_state(output->wlr_output, &state);
    wlr_output_state_finish(&state);

    return 0;
}

static int wh_output_configure(WhaleOutput* output)
{
    int st = wh_output_set_mode(output);
    if (st < 0)
        return st;

    /* Add the output to the output-layout, from left to right. */
    wlr_output_layout_add_auto(output->comp->output_layout, output->wlr_output);

    return 0;
}

static void on_output_new(struct wl_listener* listener, void* data)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.new_output);
    struct wlr_output* wlr_output = data;

    WhaleOutput* output = calloc(1, sizeof(WhaleOutput));
    if (!output)
    {
        wh_log(ERR, "output: Failed to allocate memory for monitor");
        return;
    }

    if (!wlr_output_init_render(wlr_output, comp->allocator, comp->renderer))
    {
        wh_log(ERR, "mon: Failed to init monitor renderer!");
        free(output);
        return;
    }

    output->wlr_output = wlr_output;
    output->comp = comp;
    wlr_output->data = output;
    
    VEC_INIT_SIZED(4, &output->clients);

    /* Set the output's event listeners */
    LISTEN(&wlr_output->events.frame, &output->listener_frame, on_output_frame);
    LISTEN(
        &wlr_output->events.destroy,
        &output->listener_destroy,
        on_output_destroy
    );
    LISTEN(
        &wlr_output->events.request_state,
        &output->listener_request_state,
        on_output_request_state
    );

    wh_output_configure(output);

    /* Add this output's viewport to the main scene-graph. */
    output->scene_output =
        wlr_scene_output_create(comp->root_scene, wlr_output);

    /* Keep track of this output */
    wl_list_insert(&comp->outputs, &output->link);
}

static void on_output_layout_change(struct wl_listener* listener, void*)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.output_layout_change);

    wh_client_refresh_all_client_bounds(comp);

    struct wlr_box scene_geom;
    wlr_output_layout_get_box(comp->output_layout, NULL, &scene_geom);

    /* Resize root bg */
    wlr_scene_node_set_position(
        &comp->root_bg_rect->node, scene_geom.x, scene_geom.y
    );
    wlr_scene_rect_set_size(
        comp->root_bg_rect, scene_geom.width, scene_geom.height
    );
}

int wh_output_subsystem_init(WhaleCompositor* comp)
{
    comp->root_scene = wlr_scene_create();

    float color[] = {0x12 / 255.f, 0x12 / 255.f, 0x12 / 255.f, 0xFF / 255.f};
    comp->root_bg_rect =
        wlr_scene_rect_create(&comp->root_scene->tree, 0, 0, color);

    /* An output layout is all of the outputs arranged into a
     * 2D coordinate space */
    comp->output_layout = wlr_output_layout_create(comp->display);
    LISTEN(
        &comp->output_layout->events.change,
        &comp->listeners.output_layout_change,
        on_output_layout_change
    );

    /* List keeping track of all monitors */
    wl_list_init(&comp->outputs);
    LISTEN(
        &comp->backend->events.new_output,
        &comp->listeners.new_output,
        on_output_new
    );

    return 0;
}

WhaleOutput* wh_output_get_at(wh_coord_t x, wh_coord_t y, WhaleCompositor* comp)
{
    struct wlr_output* output =
        wlr_output_layout_output_at(comp->output_layout, x, y);

    if (!output)
        return NULL;

    return wh_output_from_wlr_output(output);
}

WhaleOutput* wh_output_get_default(WhaleCompositor* comp)
{
    return wh_output_from_wlr_output(
        wlr_output_layout_get_center_output(comp->output_layout)
    );
}

struct wlr_box wh_output_get_geometry(WhaleOutput* output)
{
    struct wlr_box geom;
    wlr_output_layout_get_box(
        output->comp->output_layout, output->wlr_output, &geom
    );
    return geom;
}
