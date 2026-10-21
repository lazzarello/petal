/* Inspect portal video formats, or show linear DMA-BUF frames in a GTK window. */
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/dma-buf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <gtk/gtk.h>
#include <pipewire/pipewire.h>
#include <spa/debug/format.h>
#include <spa/param/param.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>

struct probe {
    struct pw_main_loop *loop;
    struct pw_registry *registry;
    struct pw_node *node;
    struct pw_core *core;
    struct pw_stream *stream;
    struct spa_hook registry_listener;
    struct spa_hook node_listener;
    struct spa_hook stream_listener;
    uint32_t target;
    int formats;
    int frames;
    int failed;
    int closing;
    int view;
    uint32_t width, height;
    GtkWidget *window;
    cairo_surface_t *surface;
};

static void stop(struct probe *p)
{
    if (p->view) {
        if (gtk_main_level()) gtk_main_quit();
    } else {
        pw_main_loop_quit(p->loop);
    }
}

static gboolean draw(GtkWidget *widget, cairo_t *cr, gpointer userdata)
{
    struct probe *p = userdata;
    if (!p->surface) return FALSE;
    double scale = MIN((double)gtk_widget_get_allocated_width(widget) / p->width,
                       (double)gtk_widget_get_allocated_height(widget) / p->height);
    cairo_translate(cr, (gtk_widget_get_allocated_width(widget) - p->width * scale) / 2,
                        (gtk_widget_get_allocated_height(widget) - p->height * scale) / 2);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, p->surface, 0, 0);
    cairo_paint(cr);
    return FALSE;
}

static int sync_dmabuf(int fd, uint64_t flags)
{
    struct dma_buf_sync sync = { .flags = flags };
    int result;
    do result = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
    while (result < 0 && (errno == EINTR || errno == EAGAIN));
    return result;
}

/* shortcut: CPU-copy linear DMA-BUF only; use EGL for tiled formats or sustained high FPS. */
static void show_frame(struct probe *p, struct spa_data *d)
{
    struct spa_chunk *chunk = d->chunk;
    off_t size = d->fd >= 0 ? lseek(d->fd, 0, SEEK_END) : -1;
    if (size >= 0) (void)lseek(d->fd, 0, SEEK_SET);
    // DMA-BUF maxsize/chunk-size may be placeholders; query the allocation via the FD.
    size_t length = chunk && chunk->stride > 0
        ? (size_t)chunk->offset + (size_t)chunk->stride * (p->height - 1) + (size_t)p->width * 4
        : 0;
    if (!chunk || d->fd < 0 || size < 0 || chunk->stride < (int64_t)p->width * 4 ||
        length > 256u * 1024u * 1024u || d->mapoffset > (uint64_t)size ||
        length > (uint64_t)size - d->mapoffset) {
        fprintf(stderr, "invalid linear frame layout: %ux%u, stride=%d, offset=%u, "
                        "chunk-size=%u, maxsize=%u, mapoffset=%u, dma-buf-size=%lld\n",
                p->width, p->height, chunk ? chunk->stride : -1,
                chunk ? chunk->offset : 0, chunk ? chunk->size : 0,
                d->maxsize, d->mapoffset, (long long)size);
        p->failed = 1;
        return;
    }
    void *memory = mmap(NULL, length, PROT_READ, MAP_SHARED, d->fd, d->mapoffset);
    if (memory == MAP_FAILED) {
        perror("mapping linear DMA-BUF");
        p->failed = 1;
        return;
    }
    if (sync_dmabuf(d->fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ) < 0) {
        perror("synchronizing DMA-BUF");
        p->failed = 1;
    } else {
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, p->width, p->height);
        if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
            unsigned char *dst = cairo_image_surface_get_data(surface);
            unsigned char *src = (unsigned char *)memory + chunk->offset;
            for (uint32_t y = 0; y < p->height; y++)
                memcpy(dst + (size_t)y * cairo_image_surface_get_stride(surface),
                       src + (size_t)y * chunk->stride, (size_t)p->width * 4);
            cairo_surface_mark_dirty(surface);
            if (p->surface) cairo_surface_destroy(p->surface);
            p->surface = surface;
            gtk_widget_queue_draw(p->window);
        } else {
            fprintf(stderr, "could not allocate video surface\n");
            cairo_surface_destroy(surface);
            p->failed = 1;
        }
        if (sync_dmabuf(d->fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ) < 0) {
            perror("ending DMA-BUF sync");
            p->failed = 1;
        }
    }
    munmap(memory, length);
}

static void on_stream_state(void *userdata, enum pw_stream_state old,
                            enum pw_stream_state state, const char *error)
{
    struct probe *p = userdata;
    (void)old;
    if (!p->closing && (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED)) {
        fprintf(stderr, "native stream: %s\n", error ? error : pw_stream_state_as_string(state));
        p->failed = 1;
        stop(p);
    }
}

static void on_stream_format(void *userdata, uint32_t id, const struct spa_pod *param)
{
    struct probe *p = userdata;
    if (id != SPA_PARAM_Format || !param) return;
    struct spa_video_info_raw info;
    if (spa_format_video_raw_parse(param, &info) < 0 || info.modifier != 0) {
        fprintf(stderr, "native stream did not negotiate linear video\n");
        p->failed = 1;
        stop(p);
        return;
    }
    if (!info.size.width || !info.size.height || info.size.width > 8192 || info.size.height > 8192) {
        fprintf(stderr, "invalid video dimensions\n");
        p->failed = 1;
        stop(p);
        return;
    }
    p->width = info.size.width;
    p->height = info.size.height;
    fprintf(stderr, "native linear format: %ux%u, modifier=%llu\n",
            p->width, p->height, (unsigned long long)info.modifier);
    uint8_t buffer[256];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[] = {
        spa_pod_builder_add_object(&b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
            SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 16),
            SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
            SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(1 << SPA_DATA_DmaBuf)),
    };
    if (pw_stream_update_params(p->stream, params, 1) < 0) {
        p->failed = 1;
        stop(p);
    }
}

static void on_process(void *userdata)
{
    struct probe *p = userdata;
    struct pw_buffer *b = pw_stream_dequeue_buffer(p->stream);
    if (!b) return;
    if (b->buffer->n_datas == 1 && b->buffer->datas[0].type == SPA_DATA_DmaBuf) {
        if (p->view) show_frame(p, &b->buffer->datas[0]);
        if (!p->failed && p->frames++ == 0)
            fprintf(stderr, "native DMA-BUF frame received (1 plane)\n");
    } else {
        fprintf(stderr, "native frame is not a one-plane DMA-BUF\n");
        p->failed = 1;
    }
    pw_stream_queue_buffer(p->stream, b);
    if (p->failed || (!p->view && p->frames == 1)) stop(p);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_stream_state,
    .param_changed = on_stream_format,
    .process = on_process,
};

static void on_param(void *userdata, int seq, uint32_t id, uint32_t index,
                     uint32_t next, const struct spa_pod *param)
{
    struct probe *p = userdata;
    (void)seq; (void)index; (void)next;
    if (id == SPA_PARAM_EnumFormat && param) {
        fprintf(stderr, "EnumFormat %d:\n", ++p->formats);
        spa_debug_format(2, NULL, param);
    }
    if (p->formats == 2 && !p->stream) {
        uint8_t buffer[256];
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        struct spa_pod_frame f;
        spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
        spa_pod_builder_add(&b,
            SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
            SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
            SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_BGRx),
            SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(
                &SPA_RECTANGLE(640, 480), &SPA_RECTANGLE(1, 1),
                &SPA_RECTANGLE(8192, 8192)), 0);
        spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY);
        spa_pod_builder_long(&b, 0);
        const struct spa_pod *params[] = { spa_pod_builder_pop(&b, &f) };
        p->stream = pw_stream_new(p->core, "petal-native-probe",
            pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
                              PW_KEY_MEDIA_CATEGORY, "Capture", NULL));
        if (!p->stream) {
            fprintf(stderr, "could not create native stream\n");
            p->failed = 1;
            stop(p);
            return;
        }
        pw_stream_add_listener(p->stream, &p->stream_listener, &stream_events, p);
        if (pw_stream_connect(p->stream, PW_DIRECTION_INPUT, p->target,
                              PW_STREAM_FLAG_AUTOCONNECT, params, 1) < 0) {
            fprintf(stderr, "could not connect native stream\n");
            p->failed = 1;
            stop(p);
        }
    }
}

static const struct pw_node_events node_events = {
    PW_VERSION_NODE_EVENTS,
    .param = on_param,
};

static void on_global(void *userdata, uint32_t id, uint32_t permissions,
                      const char *type, uint32_t version, const struct spa_dict *props)
{
    struct probe *p = userdata;
    (void)permissions; (void)props;
    if (id != p->target || strcmp(type, PW_TYPE_INTERFACE_Node) != 0)
        return;
    p->node = pw_registry_bind(p->registry, id, type,
                               version < PW_VERSION_NODE ? version : PW_VERSION_NODE, 0);
    if (p->node) {
        pw_node_add_listener(p->node, &p->node_listener, &node_events, p);
        if (pw_node_enum_params(p->node, 0, SPA_PARAM_EnumFormat, 0, UINT32_MAX, NULL) < 0)
            fprintf(stderr, "could not enumerate node formats\n");
    }
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = on_global,
};

static void on_timeout(void *userdata, uint64_t expirations)
{
    struct probe *p = userdata;
    (void)expirations;
    if (!p->frames) {
        fprintf(stderr, "no native frames received in five seconds\n");
        p->failed = 1;
    }
    if (!p->view || p->failed) stop(p);
}

static gboolean pump(gpointer userdata)
{
    struct probe *p = userdata;
    if (p->failed) {
        stop(p);
        return G_SOURCE_REMOVE;
    }
    if (pw_loop_iterate(pw_main_loop_get_loop(p->loop), 0) < 0) {
        p->failed = 1;
        stop(p);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv)
{
    char *end;
    struct probe p = {0};
    struct pw_context *context;
    struct spa_source *timer;
    struct timespec limit = { .tv_sec = 5 };
    long fd;
    unsigned long target;

    if (argc != 3 && (argc != 4 || strcmp(argv[3], "--view") != 0)) {
        fprintf(stderr, "usage: %s <portal-fd> <node-id> [--view]\n", argv[0]);
        return 2;
    }
    errno = 0;
    fd = strtol(argv[1], &end, 10);
    if (errno || *end || fd < 0 || fd > INT32_MAX) return 2;
    errno = 0;
    target = strtoul(argv[2], &end, 10);
    if (errno || *end || target > UINT32_MAX) return 2;
    p.target = (uint32_t)target;
    p.view = argc == 4;
    if (p.view) {
        if (!gtk_init_check(NULL, NULL)) {
            fprintf(stderr, "could not initialize GTK display\n");
            return 1;
        }
        p.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(p.window), "Petal PipeWire preview");
        gtk_window_set_default_size(GTK_WINDOW(p.window), 900, 600);
        GtkWidget *area = gtk_drawing_area_new();
        gtk_container_add(GTK_CONTAINER(p.window), area);
        g_signal_connect(area, "draw", G_CALLBACK(draw), &p);
        g_signal_connect(p.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    }

    pw_init(NULL, NULL);
    p.loop = pw_main_loop_new(NULL);
    if (!p.loop) return 1;
    context = pw_context_new(pw_main_loop_get_loop(p.loop), NULL, 0);
    if (!context) return 1;
    p.core = pw_context_connect_fd(context, (int)fd, NULL, 0);
    if (!p.core) {
        perror("connecting to portal PipeWire remote");
        return 1;
    }
    p.registry = pw_core_get_registry(p.core, PW_VERSION_REGISTRY, 0);
    if (!p.registry) return 1;
    pw_registry_add_listener(p.registry, &p.registry_listener, &registry_events, &p);
    timer = pw_loop_add_timer(pw_main_loop_get_loop(p.loop), on_timeout, &p);
    pw_loop_update_timer(pw_main_loop_get_loop(p.loop), timer, &limit, NULL, false);
    if (p.view) {
        g_timeout_add(10, pump, &p);
        gtk_widget_show_all(p.window);
        gtk_main();
    } else {
        pw_main_loop_run(p.loop);
    }
    if (!p.frames) fprintf(stderr, "native probe received no frames\n");
    p.closing = 1;
    if (p.stream) pw_stream_destroy(p.stream);
    if (p.node) pw_proxy_destroy((struct pw_proxy *)p.node);
    pw_proxy_destroy((struct pw_proxy *)p.registry);
    pw_core_disconnect(p.core);
    pw_context_destroy(context);
    pw_main_loop_destroy(p.loop);
    if (p.surface) cairo_surface_destroy(p.surface);
    pw_deinit();
    return !p.failed && p.frames > 0 ? 0 : 1;
}
