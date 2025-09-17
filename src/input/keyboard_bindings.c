
#include <whale/client/client.h>
#include <whale/compositor.h>
#include <whale/debug.h>
#include <whale/input/keyboard.h>
#include <whale/input/pointer.h>
#include <whale/input/seat.h>
#include <whale/log.h>
#include <whale/output/output.h>
#include <whale/output/workspace.h>
#include <whale/types.h>
#include <whale/utils/proc.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define MOD_NORMAL WLR_MODIFIER_LOGO

#define MOD_IMPORTANT (MOD_NORMAL | WLR_MODIFIER_SHIFT)

#define MODS_DISCARD_CAPS(_mods) (_mods & ~((u32)WLR_MODIFIER_CAPS))

typedef union
{
    u64 unsigned_64;
    char* string;
} BindingCallbackArg;

typedef struct
{
    xkb_keysym_t key;
    u32 modifiers;
    void (*callback)(const BindingCallbackArg* arg);
    BindingCallbackArg arg;
} WhaleKeyboardBinding;

VEC(WhaleKeyboardBinding) g_keyboard_bindings;

static void terminate_focused_client(const BindingCallbackArg*)
{
    WhaleSurface* surface = wh_keyboard_get_focused_surface();
    if (surface)
        wh_client_close(wh_client_from_surface(surface));
}

static void spawn(const BindingCallbackArg* arg)
{
    wh_proc_spawn_literal(arg->string);
}

static void inc_tiled_master_split(const BindingCallbackArg*)
{
    WhaleOutput* output = wh_output_get_focused();
    if (!output)
        return;

    WhaleWorkspace* ws = wh_output_get_active_workspace(output);
    wh_workspace_tiling_increment_master_split(0.05, ws);
    wh_workspace_arrange(ws);
}

static void dec_tiled_master_split(const BindingCallbackArg*)
{
    WhaleOutput* output = wh_output_get_focused();
    if (!output)
        return;

    WhaleWorkspace* ws = wh_output_get_active_workspace(output);
    wh_workspace_tiling_decrement_master_split(0.05, ws);
    wh_workspace_arrange(ws);
}

static void inc_tiled_master_max_clients(const BindingCallbackArg*)
{
    WhaleOutput* output = wh_output_get_focused();
    if (!output)
        return;

    WhaleWorkspace* ws = wh_output_get_active_workspace(output);
    wh_workspace_tiling_increment_master_max_clients(1, ws);
    wh_workspace_arrange(ws);
}

static void dec_tiled_master_max_clients(const BindingCallbackArg*)
{
    WhaleOutput* output = wh_output_get_focused();
    if (!output)
        return;

    WhaleWorkspace* ws = wh_output_get_active_workspace(output);
    wh_workspace_tiling_decrement_master_max_clients(1, ws);
    wh_workspace_arrange(ws);
}

static void switch_workspace(const BindingCallbackArg* arg)
{
    WhaleOutput* output = wh_output_get_focused();
    if (!output)
        return;

    WH_ASSERT(arg->unsigned_64 < 256);
    wh_output_activate_workspace((u8)arg->unsigned_64, output);
    wh_seat_refocus_input(true);
}

static void chvt(const BindingCallbackArg* arg)
{
    WH_ASSERT(arg->unsigned_64 < 256);
    wh_compositor_change_vt((u8)arg->unsigned_64);
}

static void move_client_to_workspace(const BindingCallbackArg* arg)
{
    WhaleSurface* surface = wh_keyboard_get_focused_surface();
    if (!surface)
        return;

    WhaleClient* client = wh_client_from_surface(surface);
    /* Sanity check: a focusable clients must have a bound workspace */
    WH_ASSERT_SANITY(client->workspace);

    WhaleWorkspace* new_ws = wh_output_get_workspace(
        (u8)arg->unsigned_64, client->workspace->parent_output
    );

    if (new_ws == client->workspace)
        return;

    WhaleWorkspace* old_ws = wh_workspace_unbind_client(client);

    /* If the client was managed, we need to re-arrange the workspace, as
    this is a workspace that is focused right now */
    if (client->layout == LAYOUT_TILING)
        wh_workspace_arrange(old_ws);

    /* We don't need to arrange this workspace here, it'll get re-arranged
    when we switch to it. */
    wh_workspace_bind_client(client, new_ws);

    wh_client_unmap(client);
}

static void exit_whale(const BindingCallbackArg*)
{
    wh_compositor_request_exit();
}

static void on_keyboard_bindings_config_changed()
{
    VEC_CLEAR(&g_keyboard_bindings);

    WhaleKeyboardBinding binding = {
        .modifiers = MOD_IMPORTANT,
        .key = XKB_KEY_c,
        .callback = terminate_focused_client
    };
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_grave,
                                     .modifiers = MOD_NORMAL,
                                     .callback = spawn,
                                     .arg = {.string = "/bin/alacritty"}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_f,
                                     .modifiers = MOD_NORMAL,
                                     .callback = spawn,
                                     .arg = {.string = "/bin/librewolf -p"}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){
        .key = XKB_KEY_h,
        .modifiers = MOD_NORMAL,
        .callback = dec_tiled_master_split,
    };
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){
        .key = XKB_KEY_l,
        .modifiers = MOD_NORMAL,
        .callback = inc_tiled_master_split,
    };
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){
        .key = XKB_KEY_d,
        .modifiers = MOD_NORMAL,
        .callback = dec_tiled_master_max_clients,
    };
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){
        .key = XKB_KEY_i,
        .modifiers = MOD_NORMAL,
        .callback = inc_tiled_master_max_clients,
    };
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_1,
                                     .modifiers = MOD_NORMAL,
                                     .callback = switch_workspace,
                                     .arg = {.unsigned_64 = 0}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_2,
                                     .modifiers = MOD_NORMAL,
                                     .callback = switch_workspace,
                                     .arg = {.unsigned_64 = 1}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_3,
                                     .modifiers = MOD_NORMAL,
                                     .callback = switch_workspace,
                                     .arg = {.unsigned_64 = 2}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_4,
                                     .modifiers = MOD_NORMAL,
                                     .callback = switch_workspace,
                                     .arg = {.unsigned_64 = 3}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_XF86Switch_VT_1,
                                     .modifiers =
                                         WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
                                     .callback = chvt,
                                     .arg = {.unsigned_64 = 1}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_XF86Switch_VT_2,
                                     .modifiers =
                                         WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
                                     .callback = chvt,
                                     .arg = {.unsigned_64 = 2}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_XF86Switch_VT_3,
                                     .modifiers =
                                         WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
                                     .callback = chvt,
                                     .arg = {.unsigned_64 = 3}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_XF86Switch_VT_4,
                                     .modifiers =
                                         WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT,
                                     .callback = chvt,
                                     .arg = {.unsigned_64 = 4}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_exclam,
                                     .modifiers = MOD_IMPORTANT,
                                     .callback = move_client_to_workspace,
                                     .arg = {.unsigned_64 = 0}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_at,
                                     .modifiers = MOD_IMPORTANT,
                                     .callback = move_client_to_workspace,
                                     .arg = {.unsigned_64 = 1}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_numbersign,
                                     .modifiers = MOD_IMPORTANT,
                                     .callback = move_client_to_workspace,
                                     .arg = {.unsigned_64 = 2}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_dollar,
                                     .modifiers = MOD_IMPORTANT,
                                     .callback = move_client_to_workspace,
                                     .arg = {.unsigned_64 = 3}};
    VEC_PUSH(binding, &g_keyboard_bindings);

    binding = (WhaleKeyboardBinding){.key = XKB_KEY_q,
                                     .modifiers = MOD_IMPORTANT,
                                     .callback = exit_whale};
    VEC_PUSH(binding, &g_keyboard_bindings);
}

int wh_keyboard_bindings_init()
{
    VEC_INIT_SIZED(64, &g_keyboard_bindings);
    on_keyboard_bindings_config_changed();
    return 0;
}

void wh_keyboard_bindings_destroy()
{
    VEC_DESTROY(&g_keyboard_bindings);
}

bool wh_keyboard_bindings_modifiers_match(u32 modifiers)
{
    modifiers = MODS_DISCARD_CAPS(modifiers);
    return (modifiers & MOD_NORMAL) ||
           (modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT));
}

bool wh_keyboard_bindings_try_handle_key(
    const xkb_keysym_t* keysims,
    size_t keysim_count,
    u32 modifiers,
    enum wl_keyboard_key_state key_state
)
{
    if (key_state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return false;

    VEC_FOR_EACH (binding, &g_keyboard_bindings)
    {
        /* Discard caps because we don't care if that is active or not
        for our use case. */
        if (MODS_DISCARD_CAPS(modifiers) != binding->modifiers)
            continue;

        for (size_t k = 0; k < keysim_count; ++k)
        {
            if (xkb_keysym_to_lower(keysims[k]) == binding->key)
            {
                binding->callback(&binding->arg);
                return true;
            }
        }
    }

    return false;
}
