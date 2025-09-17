
#include <whale/compositor.h>
#include <whale/input/clipboard.h>
#include <whale/input/keyboard.h>
#include <whale/input/pointer.h>
#include <whale/input/seat.h>
#include <whale/log.h>
#include <whale/output/scene.h>
#include <wlr/types/wlr_seat.h>

static struct wlr_seat* g_seat;

static void on_new_input(struct wlr_input_device* dev)
{
    wh_log(INFO, "seat: new input (%s)", dev->name);

    u32 seat_caps = g_seat->capabilities;

    switch (dev->type)
    {
    case WLR_INPUT_DEVICE_POINTER:
        struct wlr_pointer* ptr_dev = wlr_pointer_from_input_device(dev);
        if (wh_pointer_attach_device(ptr_dev) == 0)
            seat_caps |= WL_SEAT_CAPABILITY_POINTER;

        break;

    case WLR_INPUT_DEVICE_KEYBOARD:
        struct wlr_keyboard* keyboard_dev = wlr_keyboard_from_input_device(dev);
        if (wh_keyboard_attach_device(keyboard_dev) == 0)
            seat_caps |= WL_SEAT_CAPABILITY_KEYBOARD;

        break;

    case WLR_INPUT_DEVICE_TOUCH:
    case WLR_INPUT_DEVICE_TABLET:
    case WLR_INPUT_DEVICE_TABLET_PAD:
    case WLR_INPUT_DEVICE_SWITCH:
    default:
        wh_log(WARN, "seat: Unhandled device (%s)", dev->name);
        break;
    }

    wlr_seat_set_capabilities(g_seat, seat_caps);
}

int wh_seat_init()
{
    g_seat = wlr_seat_create(wh_compositor_get_wl_display(), "seat_0");
    if (!g_seat)
    {
        wh_log(ERR, "seat: Failed to create seat.");
        return -1;
    }

    /* We roll our own built-in clipboard implementation because I feel like the
     * clipboard is a core part of any desktop and having to install a package
     * for it's functionality is silly. */
    if (wh_clipboard_init(g_seat) < 0)
        return -1;

    if (wh_pointer_init(g_seat) < 0)
        return -1;

    if (wh_keyboard_init(g_seat) < 0)
        return -1;

    wh_compositor_set_new_input_callback(on_new_input);

    return 0;
}

void wh_seat_destroy()
{
    wh_compositor_clear_new_input_callback();

    wh_keyboard_destroy();
    wh_pointer_destroy();
    wh_clipboard_destroy();

    wlr_seat_destroy(g_seat);
    g_seat = nullptr;
}

WhaleSurface* wh_seat_refocus_input(bool force_keyboard)
{
    WhalePosition2D pointer_pos;
    wh_pointer_get_pos(&pointer_pos);

    WhaleSurface* surface = wh_scene_get_topmost_surface_at(&pointer_pos);
    if (!surface)
    {
        if (wh_pointer_get_focused_surface())
        {
            wh_pointer_unfocus_unchecked();
            wh_pointer_set_texture("default");
        }

        if (wh_keyboard_get_focused_surface() &&
            (wh_pointer_focus_follows_pointer() || force_keyboard))
        {
            wh_keyboard_unfocus_unchecked();
        }

        return nullptr;
    }

    WhalePosition2D surface_coords;
    wh_surface_layout_to_surface_coords(surface, &pointer_pos, &surface_coords);

    wh_pointer_focus_surface(&surface_coords, surface);
    if (wh_pointer_focus_follows_pointer() || force_keyboard)
        wh_keyboard_focus_surface(surface);

    return surface;
}
