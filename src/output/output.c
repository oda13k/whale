
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-util.h>
#include <whale/client/client.h>
#include <whale/compositor.h>
#include <whale/debug.h>
#include <whale/input/pointer.h>
#include <whale/log.h>
#include <whale/output/output.h>
#include <whale/output/scene.h>
#include <whale/output/workspace.h>
#include <whale/types.h>
#include <whale/utils/vector.h>
#include <wlr/backend.h>
#include <wlr/backend/drm.h>
#include <wlr/types/wlr_xdg_output_v1.h>

typedef enum
{
    OUTPUT_ROTATION_NONE,
    OUTPUT_ROTATION_CLOCKWISE,
    OUTPUT_ROTATION_COUNTER_CLOCKWISE,
    OUTPUT_ROTATION_UPSIDE_DOWN
} OutputRotation;

typedef struct
{
    const char* id;
    u32 x, y;
    OutputRotation rotation;
    u32 width, height;
    float refresh_rate_hz;
    bool disabled;
} OutputConfig;

typedef struct
{
    const char* name;
    VEC(OutputConfig) configs;
} OutputSetup;

static struct wlr_output_layout* g_output_layout;
static struct wlr_scene_rect* g_bg_tree;
static VEC(WhaleOutput*) g_outputs;
static VEC(OutputSetup) g_output_setups;

static int output_workspaces_init(WhaleOutput* output)
{
    const size_t workspace_count = 5;

    if (VEC_INIT_SIZED(workspace_count, &output->workspaces) < 0)
    {
        wh_log(ERR, "output: Failed to allocate workspaces for output.");
        return -1;
    }

    for (size_t i = 0; i < workspace_count; ++i)
    {
        /* Should never fail, vector is pre-allocated. */
        WH_ASSERT_SANITY(
            VEC_PUSH((WhaleWorkspace){}, &output->workspaces) == 0
        );

        if (wh_workspace_init(output, &VEC_AT(i, &output->workspaces)) < 0)
        {
            VEC_FOR_EACH (ws, &output->workspaces)
                wh_workspace_destroy(ws);

            VEC_DESTROY(&output->workspaces);
            return -1;
        }
    }

    output->active_workspace = &VEC_AT(0, &output->workspaces);
    return 0;
}

static void output_workspaces_destroy(WhaleOutput* output)
{
    WhaleOutput* adoptive_output = wh_output_get_main();

    size_t idx = 0;
    bool keep_workspace_order =
        adoptive_output ? VEC_GET_LENGTH(&adoptive_output->workspaces)
                        : 0 >= VEC_GET_LENGTH(&output->workspaces);

    VEC_FOR_EACH (ws, &output->workspaces)
    {
        VEC_FOR_EACH (client, &ws->clients)
        {
            wh_workspace_unbind_client(*client);

            if (!adoptive_output)
                continue;

            size_t new_ws_idx = keep_workspace_order ? idx : 0;
            int st = wh_workspace_bind_client(
                *client, &VEC_AT(new_ws_idx, &adoptive_output->workspaces)
            );

            if (st < 0)
                wh_log(ERR, "output: Failed to bind client to workspace.");
        }

        wh_workspace_destroy(ws);
        ++idx;
    }

    VEC_DESTROY(&output->workspaces);
    output->active_workspace = nullptr;
}

static void on_output_frame(struct wl_listener* listener, void*)
{
    WhaleOutput* output = wl_container_of(listener, output, listeners.frame);

    wlr_scene_output_commit(output->scene_output, NULL);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    wlr_scene_output_send_frame_done(output->scene_output, &ts);
}

static void on_output_destroy(struct wl_listener* listener, void*)
{
    WhaleOutput* output = wl_container_of(listener, output, listeners.destroy);

    UNLISTEN(&output->listeners.destroy);
    UNLISTEN(&output->listeners.frame);
    UNLISTEN(&output->listeners.request_state);

    VEC_REMOVE(output, &g_outputs);

    wlr_output_layout_remove(g_output_layout, output->wlr_output);
    wh_scene_detach_output(output);

    output_workspaces_destroy(output);

    free(output);
}

static void on_output_request_state(struct wl_listener*, void* data)
{
    /* The monitor is asking us that it wants this state */
    struct wlr_output_event_request_state* ev = data;
    wlr_output_commit_state(ev->output, ev->state);

    wh_log(
        DEBUG,
        "output: [%s] requested mode %dx%d@%.2fHz",
        ((WhaleOutput*)(ev->output->data))->id,
        ev->output->width,
        ev->output->height,
        ev->output->refresh / 1000.f
    );
}

static int output_set_mode(const OutputConfig* config, WhaleOutput* output)
{
    struct wlr_output_state state;
    wlr_output_state_init(&state);

    wlr_output_state_set_enabled(&state, !config->disabled);
    if (config->disabled)
        goto done;

    enum wl_output_transform transform;
    switch (config->rotation)
    {
    case OUTPUT_ROTATION_NONE:
        transform = WL_OUTPUT_TRANSFORM_NORMAL;
        break;
    case OUTPUT_ROTATION_CLOCKWISE:
        transform = WL_OUTPUT_TRANSFORM_90;
        break;
    case OUTPUT_ROTATION_COUNTER_CLOCKWISE:
        transform = WL_OUTPUT_TRANSFORM_270;
        break;
    case OUTPUT_ROTATION_UPSIDE_DOWN:
        transform = WL_OUTPUT_TRANSFORM_180;
        break;
    default:
        unreachable();
    }

    const struct wlr_output_mode* preferred_mode =
        wlr_output_preferred_mode(output->wlr_output);

    bool is_custom_mode = true;

    struct wlr_output_mode requested_mode;
    requested_mode.width = config->width    ? config->width
                           : preferred_mode ? preferred_mode->width
                                            : 0;

    requested_mode.height = config->height   ? config->height
                            : preferred_mode ? preferred_mode->height
                                             : 0;

    requested_mode.refresh = config->refresh_rate_hz
                                 ? config->refresh_rate_hz * 1000
                             : preferred_mode ? preferred_mode->refresh
                                              : 0;

    struct wlr_output_mode* mode = nullptr;

    if (wl_list_length(&output->wlr_output->modes))
    {
        wl_list_for_each(mode, &output->wlr_output->modes, link)
        {
            /* We'll round refresh rates for simpler configs. Is this a problem?
             */
            u16 asked_hz = roundf(requested_mode.refresh / 1000.f);
            u16 mode_hz = roundf(mode->refresh / 1000.f);

            if (mode->width == requested_mode.width &&
                mode->height == requested_mode.height && asked_hz == mode_hz)
            {
                is_custom_mode = false;
                break;
            }
        }
    }

    if (!mode)
    {
        /* We can't let the output pick defaults by setting everything to 0
         * since the output doesn't have any fixed modes. Except for the refresh
         * rate for some reason? */
        wlr_output_state_set_custom_mode(&state, 800, 600, 0);
    }
    else if (is_custom_mode)
    {
        wh_log(
            WARN,
            "Output does not advertise the requested mode. If you have visual artifacts pick one of the outputs's advertised modes."
        );
        wlr_output_state_set_custom_mode(
            &state,
            requested_mode.width,
            requested_mode.height,
            requested_mode.refresh
        );
    }
    else
    {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_state_set_scale(&state, 1.0f);
    wlr_output_state_set_transform(&state, transform);

done:
    wlr_output_commit_state(output->wlr_output, &state);
    wlr_output_state_finish(&state);

    return 0;
}

static void output_set_position(const WhalePosition2D* pos, WhaleOutput* output)
{
    wlr_output_layout_add(g_output_layout, output->wlr_output, pos->x, pos->y);
    wlr_scene_output_set_position(output->scene_output, pos->x, pos->y);
}

static int output_apply_config(const OutputConfig* config, WhaleOutput* output)
{
    if (output_set_mode(config, output) < 0)
        return -1;

    if (!config->disabled)
    {
        if (wh_scene_attach_output(output) < 0)
            return -1;

        WhalePosition2D pos = {.x = config->x, .y = config->y};
        output_set_position(&pos, output);

        const char* rotation_map[] = {
            [OUTPUT_ROTATION_NONE] = "",
            [OUTPUT_ROTATION_CLOCKWISE] = ", rot: clockwise",
            [OUTPUT_ROTATION_UPSIDE_DOWN] = ", rot: upside down",
            [OUTPUT_ROTATION_COUNTER_CLOCKWISE] = ", rot: counter clockwise"
        };

        wh_log(
            INFO,
            "output: [%s] %dx%d@%.2fHz, x: %zu, y: %zu%s",
            output->id,
            output->wlr_output->width,
            output->wlr_output->height,
            output->wlr_output->refresh / 1000.f,
            (size_t)pos.x,
            (size_t)pos.y,
            rotation_map[config->rotation]
        );
    }
    else
    {
        wh_log(INFO, "output: [%s] disabled.", output->id);
    }

    return 0;
}

static OutputSetup* output_get_currently_matched_setup()
{
    VEC_FOR_EACH (setup, &g_output_setups)
    {
        size_t configs_hit = 0;

        VEC_FOR_EACH (config, &setup->configs)
        {
            VEC_FOR_EACH (output, &g_outputs)
            {
                if (config->id == (*output)->id)
                {
                    ++configs_hit;
                    break;
                }
            }
        }

        if (configs_hit == VEC_GET_LENGTH(&setup->configs))
            return setup;
    }

    return nullptr;
}

static const OutputConfig* output_get_config_for_output(WhaleOutput* output)
{
    static const OutputConfig default_config = {
        .id = 0, // unspecified
        .x = 0,
        .y = 0,
        .rotation = OUTPUT_ROTATION_NONE,
        .width = 0,           // default
        .height = 0,          // default
        .refresh_rate_hz = 0, // default
        .disabled = false
    };

    /*OutputSetup* setup = output_get_currently_matched_setup();
    if (!setup)
        return &default_config; */

    OutputSetup* setup = &VEC_AT(0, &g_output_setups);

    VEC_FOR_EACH (config, &setup->configs)
    {
        if (strcmp(config->id, output->id) == 0)
            return config;
    }

    return &default_config;

    WH_ASSERT_NOT_REACHED();
}

static int output_make_id(WhaleOutput* output)
{
    struct wlr_output* wlr_output = output->wlr_output;

#define NO_MAKE "(no make)"
#define NO_MODEL "(no model)"
#define NO_SERIAL "(no serial)"

    const char* make = wlr_output->make ? wlr_output->make : NO_MAKE;
    const char* model = wlr_output->model ? wlr_output->model : NO_MODEL;
    const char* serial = wlr_output->serial ? wlr_output->serial : NO_SERIAL;

#undef NO_MAKE
#undef NO_MODEL
#undef NO_SERIAL

    size_t size = strlen(make) + strlen(model) + strlen(serial) + 3;

    output->id = calloc(1, size);
    if (!output->id)
    {
        wh_log(ERR, "output: Failed to allocate id.");
        return -1;
    }

    int st = snprintf(output->id, size, "%s %s %s", make, model, serial);

    if (st < 0 || (size_t)st != size - 1)
    {
        wh_log(ERR, "output: Failed to make id.");
        free(output->id);
        output->id = nullptr;
        return -1;
    }

    return 0;
}

static void on_new_output(struct wlr_output* wlr_output)
{
    WhaleOutput* output = calloc(1, sizeof(WhaleOutput));
    if (!output)
    {
        wh_log(ERR, "output: Failed allocate new output.");
        return;
    }

    if (VEC_PUSH(output, &g_outputs) < 0)
    {
        free(output);
        return;
    }

    output->wlr_output = wlr_output;
    wlr_output->data = output;

    output_make_id(output);

    if (output_workspaces_init(output) < 0)
    {
        VEC_REMOVE(output, &g_outputs);
        free(output);
        return;
    }

    const OutputConfig* config = output_get_config_for_output(output);

    if (output_apply_config(config, output) < 0)
    {
        VEC_REMOVE(output, &g_outputs);
        output_workspaces_destroy(output);
        free(output);
        return;
    }

    /* Set the output's event listeners */
    LISTEN(
        &wlr_output->events.frame, &output->listeners.frame, on_output_frame
    );

    LISTEN(
        &wlr_output->events.destroy,
        &output->listeners.destroy,
        on_output_destroy
    );

    LISTEN(
        &wlr_output->events.request_state,
        &output->listeners.request_state,
        on_output_request_state
    );
}

WH_CALLBACK(output_layout_change, struct wl_listener*, void*)
{
    struct wlr_output_layout_output* output_layout_output;
    wl_list_for_each(output_layout_output, &g_output_layout->outputs, link)
    {
        WhaleOutput* output = output_layout_output->output->data;

        VEC_FOR_EACH (ws, &output->workspaces)
            wh_workspace_arrange(ws);
    }

    struct wlr_box sgeom;
    wlr_output_layout_get_box(g_output_layout, nullptr, &sgeom);
    wlr_scene_node_set_position(&g_bg_tree->node, sgeom.x, sgeom.y);
    wlr_scene_rect_set_size(g_bg_tree, sgeom.width, sgeom.height);
}

int wh_output_init()
{
    g_output_layout = wlr_output_layout_create(wh_compositor_get_wl_display());
    if (!g_output_layout)
    {
        wh_log(ERR, "output: Failed to create output layout.");
        return -1;
    }

    wlr_xdg_output_manager_v1_create(
        wh_compositor_get_wl_display(), g_output_layout
    );

#define COLOR(hex)                                                             \
    {((hex >> 24) & 0xFF) / 255.0f,                                            \
     ((hex >> 16) & 0xFF) / 255.0f,                                            \
     ((hex >> 8) & 0xFF) / 255.0f,                                             \
     (hex & 0xFF) / 255.0f}

    float color1[] = COLOR(0x3a6ea5);
    float color[] = COLOR(0x181a1b);

    float* c = nullptr;

    if ((float)rand() / (float)RAND_MAX > 0.5)
        c = color;
    else
        c = color1;

    g_bg_tree = wh_scene_make_bg_rect(c);

    VEC_INIT(&g_outputs);
    VEC_INIT(&g_output_setups);

    wh_compositor_set_new_output_callback(on_new_output);

    WH_LISTEN(&g_output_layout->events.change, output_layout_change);

    OutputSetup setup;
    VEC_INIT(&setup.configs);

    OutputConfig config = {
        .id = "AU Optronics 0xE48D (no serial)", .disabled = true
    };
    VEC_PUSH(config, &setup.configs);

    config = (OutputConfig){.id = "Dell Inc. DELL P2219H 54CXYS2",
                            .x = 0,
                            .y = 0,
                            .width = 1920,
                            .height = 1080,
                            .refresh_rate_hz = 60,
                            .rotation = OUTPUT_ROTATION_CLOCKWISE};
    VEC_PUSH(config, &setup.configs);

    config = (OutputConfig){.id = "Dell Inc. DELL P2319H DQYXW13",
                            .x = 1080,
                            .y = 420,
                            .width = 1920,
                            .height = 1080,
                            .refresh_rate_hz = 60,
                            .rotation = OUTPUT_ROTATION_NONE};
    VEC_PUSH(config, &setup.configs);

    VEC_PUSH(setup, &g_output_setups);

    return 0;
}

WhaleOutput* wh_output_get_main()
{
    if (VEC_GET_LENGTH(&g_outputs) > 0)
        return VEC_AT(0, &g_outputs);

    return nullptr;
}

void wh_output_get_geometry(WhaleGeometry2D* out_geom, WhaleOutput* output)
{
    struct wlr_box output_geom = {0};
    wlr_output_layout_get_box(
        g_output_layout, output->wlr_output, &output_geom
    );

    bool flip_sides = output->wlr_output->transform == WL_OUTPUT_TRANSFORM_90 ||
                      output->wlr_output->transform == WL_OUTPUT_TRANSFORM_180;

    out_geom->pos.x = output_geom.x;
    out_geom->pos.y = output_geom.y;
    out_geom->size.w =
        flip_sides ? output->wlr_output->height : output->wlr_output->width;
    out_geom->size.h =
        flip_sides ? output->wlr_output->width : output->wlr_output->height;
}

int wh_output_activate_workspace(u8 workspace_idx, WhaleOutput* output)
{
    size_t max_workspace_idx = VEC_GET_LENGTH(&output->workspaces) - 1;
    if (workspace_idx > max_workspace_idx)
        workspace_idx = max_workspace_idx;

    WhaleWorkspace* new_workspace = &VEC_AT(workspace_idx, &output->workspaces);

    if (output->active_workspace == new_workspace)
        return 0;

    wh_workspace_deactivate(output->active_workspace);

    output->active_workspace = new_workspace;
    wh_workspace_activate(output->active_workspace);

    return 0;
}

WhaleWorkspace* wh_output_get_workspace(u8 workspace_idx, WhaleOutput* output)
{
    size_t max_workspace_idx = VEC_GET_LENGTH(&output->workspaces) - 1;
    if (workspace_idx > max_workspace_idx)
        workspace_idx = max_workspace_idx;

    return &VEC_AT(workspace_idx, &output->workspaces);
}

WhaleOutput* wh_output_get_focused()
{
    WhalePosition2D cursor_pos;
    wh_pointer_get_pos(&cursor_pos);
    return wh_output_get_at(&cursor_pos);
}

void wh_output_layout_attach_pointer(struct wlr_cursor* pointer)
{
    wlr_cursor_attach_output_layout(pointer, g_output_layout);
}

WhaleOutput* wh_output_get_at(const WhalePosition2D* pos)
{
    struct wlr_output* wlr_output =
        wlr_output_layout_output_at(g_output_layout, pos->x, pos->y);

    if (wlr_output)
        return wlr_output->data;

    return nullptr;
}
