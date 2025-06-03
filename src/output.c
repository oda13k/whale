
#define _POSIX_C_SOURCE 199309L
#define WLR_USE_UNSTABLE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-util.h>
#include <whale/compositor.h>
#include <whale/log.h>
#include <whale/output.h>
#include <whale/types.h>
#include <wlr/backend.h>

static void on_monitor_frame(struct wl_listener* listener, void*)
{
    WhaleOutput* output = wl_container_of(listener, output, listener_frame);

    wlr_scene_output_commit(output->scene_output, NULL);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    wlr_scene_output_send_frame_done(output->scene_output, &ts);
}

static void on_monitor_destroy(struct wl_listener*, void*)
{
    printf("output: destroy\n");
}

static void on_monitor_request_state(struct wl_listener*, void* data)
{
    /* The monitor is asking us that it wants this state */
    struct wlr_output_event_request_state* event = data;
    wlr_output_commit_state(event->output, event->state);

    wh_log(
        DEBUG, "output: size %dx%d", event->output->width, event->output->height
    );
}

void wh_output_on_new_output(struct wl_listener* listener, void* data)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.new_output);
    struct wlr_output* wlr_output = data;

    WhaleOutput* mon = calloc(1, sizeof(WhaleOutput));
    if (!mon)
    {
        wh_log(ERR, "mon: Failed to allocate memory for monitor");
        return;
    }

    if (!wlr_output_init_render(wlr_output, comp->allocator, comp->renderer))
    {
        wh_log(ERR, "mon: Failed to init monitor renderer!");
        free(mon);
        return;
    }

    mon->wlr_output = wlr_output;

    /* Set the output's event listeners */
    LISTEN(&wlr_output->events.frame, &mon->listener_frame, on_monitor_frame);
    LISTEN(
        &wlr_output->events.destroy, &mon->listener_destroy, on_monitor_destroy
    );
    LISTEN(
        &wlr_output->events.request_state,
        &mon->listener_request_state,
        on_monitor_request_state
    );

    /* Set the monitor to it's prefered state */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    wlr_output_state_set_scale(&state, 1);
    wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    /* Add the output to the output-layout, from left to right. */
    wlr_output_layout_add_auto(comp->output_layout, wlr_output);

    /* Add this output's viewport to the main scene-graph. */
    mon->scene_output = wlr_scene_output_create(comp->root_scene, wlr_output);

    /* Keep track of this output */
    wl_list_insert(&comp->outputs, &mon->link);
}

void wh_output_layout_on_change(struct wl_listener* listener, void*)
{
    WhaleCompositor* comp =
        wl_container_of(listener, comp, listeners.output_layout_change);

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
