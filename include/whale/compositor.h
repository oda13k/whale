
#ifndef _WHALE_COMPOSITOR_H
#define _WHALE_COMPOSITOR_H

#define WLR_USE_UNSTABLE
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

typedef struct
{
    struct wlr_keyboard_group* wlr_keyboard_group;
    struct wl_event_source* key_repeat_source;
} KeyboardGroup;

typedef struct
{
    struct wl_display* display;
    struct wlr_backend* backend;
    struct wlr_session* session;
    struct wlr_renderer* renderer;

    struct wlr_scene* root_scene;
    struct wlr_scene_rect* root_bg_rect;

    struct wlr_allocator* allocator;

    /* List of attached outputs */
    struct wl_list outputs;

    struct wlr_output_layout* output_layout;

    /* List of clients */
    struct wl_list clients;

    struct wlr_xdg_decoration_manager_v1* xdg_decoration_manager;

    /* Order of focused clients */
    // struct wl_list focus_order;

    struct wlr_cursor* cursor;
    struct wlr_xcursor_manager* cursor_manager;

    struct wlr_seat* seat;
    KeyboardGroup keyboard_group;

    /* Listeners */
    struct
    {
        struct wl_listener new_output;
        struct wl_listener output_layout_change;

        struct wl_listener xdg_new_toplevel;
        struct wl_listener xdg_new_decoration;

        struct wl_listener cursor_motion;
        struct wl_listener cursor_motion_absolute;
        struct wl_listener cursor_button;
        struct wl_listener cursor_axis;
        struct wl_listener cursor_frame;

        struct wl_listener seat_request_set_cursor;
        struct wl_listener seat_request_set_selection;

        struct wl_listener keyboard_key;
        struct wl_listener keyboard_modifier;

        struct wl_listener new_input;
    } listeners;

    struct wlr_xdg_shell* xdg_shell;
} WhaleCompositor;

#endif // !_WHALE_COMPOSITOR_H
