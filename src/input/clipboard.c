
/* For anyone wondering what's the difference between a primary and normal
 * selection: Normal selections are selections made with ctrl+c and pasted
 * with ctrl+v and/or buttons that need to be explicitly clicked.
 * Primary or HIGHLIGHT selections are made by just highlighting a piece of text
 * and then pasting it with the middle mouse button. */

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <whale/debug.h>
#include <whale/input/clipboard.h>
#include <whale/log.h>
#include <whale/types.h>
#include <whale/utils/vector.h>

#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>

/* Are these sensible values? idk */
#define CLIPBOARD_SLOT_MAX_COUNT 32
#define CLIPBOARD_SLOT_DATA_MAX_SIZE (32UL * MIB)

typedef struct
{
    /* FYI: this pointer is tied to the clipboard's data_src. If the data source
     * gets destroyed (which frees this pointer) and this slot doesn't get
     * destroyed, the pointer will point to freed data. So, make sure to also
     * destroy all clipboard slots when freeing the clipboard's data source!!!
     */
    char* mime_type;
    void* data;
    size_t data_size;
} WhaleClipboardSlot;

typedef struct
{
    struct wlr_data_source* client_data_src;
    struct wl_listener client_data_src_on_destroy;

    struct wlr_data_source data_src;

    VEC(WhaleClipboardSlot) slots;
} WhaleClipboard;

static struct wlr_seat* g_seat;

static WhaleClipboard g_clipboard;

/* Any selection made, be it explicit or highlight will remain available for
 * pasting even after the client from which we've got it closes. */
static bool g_config_clipboard_persists = true;
/* Stuff copied with ctrl+c can be pasted with middle mouse click and will
 * overwrite the previously highlighted selection. */
// static bool g_config_explicit_selection_syncs_to_highlight = true;

static void
clipboard_client_data_src_on_destroy(struct wl_listener* listener, void*)
{
    UNLISTEN(listener);
    g_clipboard.client_data_src = nullptr;
}

static void
clipboard_data_src_send(struct wlr_data_source*, const char* mime_type, int fd)
{
    WhaleClipboardSlot* slot = nullptr;
    VEC_FOR_EACH (slot_cand, &g_clipboard.slots)
    {
        if (strcmp(slot_cand->mime_type, mime_type) != 0)
            continue;

        slot = slot_cand;
        break;
    }

    if (slot)
    {
        ssize_t nwritten = write(fd, slot->data, slot->data_size);

        if (nwritten < 0)
        {
            wh_log(WARN, "clipboard: Failed to paste clipboard contents.");
        }
        else if ((size_t)nwritten != slot->data_size)
        {
            wh_log(
                WARN,
                "clipboard: Failed to paste entire clipboard contents. (%zu/%zu bytes)",
                (size_t)nwritten,
                slot->data_size
            );
        }
    }

    close(fd);
}

static void clipboard_data_src_destroy(struct wlr_data_source*) {}

static struct wlr_data_source_impl g_clipboard_data_src_impl = {
    .send = clipboard_data_src_send, .destroy = clipboard_data_src_destroy
};

static void clipboard_slot_destroy(WhaleClipboardSlot* slot)
{
    slot->mime_type = nullptr;
    free(slot->data);
    slot->data_size = 0;
}

static void clipboard_destroy_own_data()
{
    /* We don't actually deallocate the vector space, we just reset it's size
     * to 0. */
    VEC_FOR_EACH (slot, &g_clipboard.slots)
        clipboard_slot_destroy(slot);

    VEC_CLEAR(&g_clipboard.slots);

    wlr_data_source_destroy(&g_clipboard.data_src);
}

static WhaleClipboardSlot* clipboard_create_slot_for_mime_data(
    struct wlr_data_source* data_source, char* mime_type
)
{
    WhaleClipboardSlot slot = {
        .mime_type = mime_type, .data = nullptr, .data_size = 0
    };

    /* Read the clipboard data from the source. For some bizzare reason wlroots
     * only gives the data to us (afaik) only through an fd. */
    int pipe_fds[2];
    if (pipe(pipe_fds) < 0)
    {
        wh_log(ERR, "clipboard: Failed to create pipe.");
        return nullptr;
    }

    /* This function closes pipe_fds[1] for us. */
    wlr_data_source_send(data_source, mime_type, pipe_fds[1]);

    wl_display_flush_clients(wh_compositor_get_wl_display());

    /* Yucky stack bomb. */
#define BUFSIZE (1 * MIB)

    u8 buf[BUFSIZE];
    ssize_t n;

    while ((n = read(pipe_fds[0], buf, BUFSIZE)) > 0)
    {
        size_t total_size = slot.data_size + (size_t)n;
        if (total_size > CLIPBOARD_SLOT_DATA_MAX_SIZE)
        {
            close(pipe_fds[0]);

            if (slot.data)
                free(slot.data);

            wh_log(
                WARN,
                "clipboard: Data was too large for mime type \"%s\" (>%zu bytes); not persisting.",
                mime_type,
                CLIPBOARD_SLOT_DATA_MAX_SIZE
            );
            return nullptr;
        }

        if (!slot.data)
        {
            slot.data = malloc(total_size);
        }
        else
        {
            void* tmp = realloc(slot.data, total_size);
            if (tmp)
            {
                slot.data = tmp;
            }
            else
            {
                free(slot.data);
                slot.data = nullptr;
            }
        }

        if (!slot.data)
        {
            wh_log(WARN, "clipboard: Failed to allocate clipboard slot data.");
            close(pipe_fds[0]);
            return nullptr;
        }

        memcpy(slot.data + slot.data_size, buf, (size_t)n);
        slot.data_size = total_size;
    }

#undef BUFSIZE

    close(pipe_fds[0]);

    if (VEC_PUSH(slot, &g_clipboard.slots) < 0)
    {
        wh_log(WARN, "clipboard: Failed to allocate clipboard slot.");
        clipboard_slot_destroy(&slot);
        return nullptr;
    }

    return &VEC_AT(VEC_GET_LENGTH(&g_clipboard.slots) - 1, &g_clipboard.slots);
}

static struct wlr_data_source* clipboard_get_effective_data_src()
{
    /* The caller should set this. */
    struct wlr_data_source* client_data_src = g_clipboard.client_data_src;

    /* Pre-emptively free the old clipboard data. */
    if (VEC_GET_LENGTH(&g_clipboard.slots) > 0)
        clipboard_destroy_own_data();

    /* If we shouldn't persist the clipboard we'll just return the data source
     * we were given. */
    if (!g_config_clipboard_persists)
        return client_data_src;

    /* If we should persist it though, we clone it's contents preemptively and
    * use our own proxy data source containing said data. Also since this data
    is coming from a random client we'll do some baseline checks on it. */
    size_t data_source_mime_type_count =
        client_data_src->mime_types.size / sizeof(char*);

    if (data_source_mime_type_count > CLIPBOARD_SLOT_MAX_COUNT)
    {
        wh_log(ERR, "clipboard: Got too many mime types.");
        return client_data_src;
    }

    if (data_source_mime_type_count == 0)
    {
        wh_log(ERR, "clipboard: Got 0 mime types.");
        return client_data_src;
    }

    wlr_data_source_init(&g_clipboard.data_src, &g_clipboard_data_src_impl);

    const char** mime_type;
    wl_array_for_each(mime_type, &client_data_src->mime_types)
    {
        if (strlen(*mime_type) > 256)
        {
            wh_log(ERR, "clipboard: Got bogus mime type.");
            goto clipboard_sync_failed;
        }

        char* tmp = strdup(*mime_type);
        if (!tmp)
        {
            wh_log(ERR, "clipboard: Failed to dupe mime type.");
            goto clipboard_sync_failed;
        }

        char** elem =
            wl_array_add(&g_clipboard.data_src.mime_types, sizeof(char*));
        if (!elem)
        {
            wh_log(ERR, "clipboard: Failed to add mime type to data source.");
            free(tmp);
            goto clipboard_sync_failed;
        }

        *elem = tmp;

        if (!clipboard_create_slot_for_mime_data(client_data_src, tmp))
            goto clipboard_sync_failed;
    }

    return &g_clipboard.data_src;

clipboard_sync_failed:

    /* Free up any data we may have allocated before failing. */
    clipboard_destroy_own_data();

    return client_data_src;
}

static void
clipboard_use_client_data_src(struct wlr_data_source* src, u32 serial)
{
    if (g_clipboard.client_data_src)
        UNLISTEN(&g_clipboard.client_data_src_on_destroy);

    LISTEN(
        &src->events.destroy,
        &g_clipboard.client_data_src_on_destroy,
        clipboard_client_data_src_on_destroy
    );

    g_clipboard.client_data_src = src;

    struct wlr_data_source* effective_src = clipboard_get_effective_data_src();
    u32 effective_serial =
        src == effective_src
            ? serial
            : wl_display_next_serial(wh_compositor_get_wl_display());

    wlr_seat_set_selection(g_seat, effective_src, effective_serial);
}

static void clipboard_drop_client_data_src(u32 serial)
{
    struct wlr_data_source* active_src = g_seat->selection_source;
    struct wlr_data_source* clipboard_src = &g_clipboard.data_src;
    struct wlr_data_source* client_src = g_clipboard.client_data_src;

    if (active_src == clipboard_src)
    {
        if (client_src)
            wlr_data_source_destroy(client_src);

        /* If we're using the clipboard source and a client drops it's source we
         * need to reset the source's serial in order for it to keep working
         * without unmapping and mapping the client again. (??) */
        wlr_seat_set_selection(g_seat, active_src, serial);
    }
    else
    {
        WH_ASSERT_SANITY(active_src == client_src);
        wlr_seat_set_selection(g_seat, nullptr, serial);
    }
}

WH_CALLBACK(request_set_selection, struct wl_listener*, void* data)
{
    struct wlr_seat_request_set_selection_event* ev = data;

    if (ev->source)
        clipboard_use_client_data_src(ev->source, ev->serial);
    else
        clipboard_drop_client_data_src(ev->serial);
}

WH_CALLBACK(request_set_primary_selection, struct wl_listener*, void* data)
{
    struct wlr_seat_request_set_primary_selection_event* ev = data;

    wlr_seat_set_primary_selection(g_seat, ev->source, ev->serial);
}

int wh_clipboard_init(struct wlr_seat* seat)
{
    g_seat = seat;

    struct wl_display* wl_display = wh_compositor_get_wl_display();

    if (!wlr_data_device_manager_create(wl_display))
    {
        wh_log(ERR, "Failed to create data device manager global.");
        return -1;
    }

    if (!wlr_primary_selection_v1_device_manager_create(wl_display))
    {
        wh_log(ERR, "Failed to create primary data device manager global.");
        return -1;
    }

    if (VEC_INIT_SIZED(CLIPBOARD_SLOT_MAX_COUNT, &g_clipboard.slots) < 0)
    {
        wh_log(ERR, "clipboard: Failed to allocate clipboard slots.");
        wl_array_release(&g_clipboard.data_src.mime_types);
        wl_list_remove(&g_clipboard.data_src.events.destroy.listener_list);
        return -1;
    }

    WH_LISTEN(&g_seat->events.request_set_selection, request_set_selection);
    WH_LISTEN(
        &g_seat->events.request_set_primary_selection,
        request_set_primary_selection
    );

    return 0;
}

void wh_clipboard_destroy()
{
    WH_UNLISTEN(request_set_primary_selection);
    WH_UNLISTEN(request_set_selection);

    u32 serial = wl_display_next_serial(wh_compositor_get_wl_display());
    clipboard_drop_client_data_src(serial);
    wlr_seat_set_primary_selection(g_seat, nullptr, serial);

    if (VEC_GET_LENGTH(&g_clipboard.slots) > 0)
        clipboard_destroy_own_data();

    // FIXME MAYBE: 2 globals were not destroyed because wlr doesn't expose that
    // to us.

    g_seat = nullptr;
}
