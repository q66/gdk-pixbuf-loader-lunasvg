#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

#include <lunasvg.h>

#define GDK_PIXBUF_ENABLE_BACKEND 1

#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* thes are taken from rsvg */

static constexpr GdkPixbufModulePattern const svg_sigs[] = {
    {
        const_cast<char *>(" <svg"),
        const_cast<char *>("*    "),
        100
    },
    {
        const_cast<char *>(" <!DOCTYPE svg"),
        const_cast<char *>("*             "),
        100
    },
    {nullptr, nullptr, 0}
};

static constexpr char const *svg_mimes[] = {
    "image/svg+xml",
    "image/svg",
    "image/svg-xml",
    "image/vnd.adobe.svg+xml",
    "text/xml-svg",
    "image/svg+xml-compressed",
    nullptr,
};

static constexpr char const *svg_exts[] = {
    "svg",
    "svgz",
    "svg.gz",
    nullptr,
};

/* our loader code */

template<typename T>
struct gallocator {
    using value_type = T;

    T *allocate(std::size_t n) {
        return static_cast<T *>(g_malloc(n));
    }

    void deallocate(T *p, std::size_t) {
        g_free(p);
    }
};

struct Context {
    std::vector<guchar, gallocator<guchar>> filebuf{gallocator<guchar>{}};
    GdkPixbufModuleSizeFunc size_func;
    GdkPixbufModulePreparedFunc prepared_func;
    GdkPixbufModuleUpdatedFunc updated_func;
    gpointer user_data;

    static void free(Context *ctx) {
        ctx->~Context();
        g_free(ctx);
    }
};

static void data_free(guchar *, gpointer data) {
    auto *bmap = static_cast<lunasvg::Bitmap *>(data);
    using T = lunasvg::Bitmap;
    bmap->~T();
    g_free(bmap);
}

static gpointer begin_load(
    GdkPixbufModuleSizeFunc size_func,
    GdkPixbufModulePreparedFunc prepared_func,
    GdkPixbufModuleUpdatedFunc updated_func,
    gpointer user_data, GError **error
) {
    if (error) {
        *error = nullptr;
    }

    Context *ctx = static_cast<Context *>(g_malloc(sizeof(Context)));
    new (ctx) Context{};

    ctx->size_func = size_func;
    ctx->prepared_func = prepared_func;
    ctx->updated_func = updated_func;
    ctx->user_data = user_data;

    return ctx;
}

static gboolean load_increment(
    gpointer data, guchar const *buf, guint size, GError **error
) {
    auto *ctx = static_cast<Context *>(data);

    if (error) {
        *error = nullptr;
    }

    ctx->filebuf.insert(ctx->filebuf.end(), buf, buf + size);

    return TRUE;
}

static gboolean stop_load(gpointer data, GError **error) {
    auto *ctx = static_cast<Context *>(data);

    if (error) {
        *error = nullptr;
    }

    std::unique_ptr<lunasvg::Document> doc;

    void *bufp = nullptr;
    auto *bufd = reinterpret_cast<char const *>(ctx->filebuf.data());
    auto bufs = ctx->filebuf.size();

    /* input data may be gzip-compressed, decompress ahead of time */
    if ((bufs > 2) && (bufd[0] == '\x1f') && (bufd[0] == '\x8b')) {
        auto *mstream = g_memory_output_stream_new(
            nullptr, 0, g_realloc, g_free
        );
        /* decompressor */
        auto *dec = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP);
        /* converter stream */
        auto *cstream = g_converter_output_stream_new(
            mstream, G_CONVERTER(dec)
        );
        /* memory input stream for the input data */
        auto *imstream = g_memory_input_stream_new_from_data(
            bufd, bufs, nullptr
        );
        /* splice the input into the output, closing both converter and input
         * (but not the original memory stream that holds the resulting data)
         */
        auto flags = GOutputStreamSpliceFlags(
            G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE
            | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET
        );
        if (g_output_stream_splice(
            cstream, imstream, flags, nullptr, error
        ) < 0) {
            Context::free(ctx);
            g_output_stream_close(mstream, nullptr, nullptr);
            g_object_unref(imstream);
            g_object_unref(cstream);
            g_object_unref(mstream);
            g_object_unref(dec);
            return FALSE;
        }
        g_object_unref(imstream);
        g_object_unref(cstream);
        g_object_unref(dec);
        /* uncompressed stuff */
        auto *omstream = G_MEMORY_OUTPUT_STREAM(mstream);
        bufs = g_memory_output_stream_get_data_size(omstream);
        bufp = g_memory_output_stream_steal_data(omstream);
        bufd = static_cast<char const *>(bufp);
        if (!g_output_stream_close(mstream, nullptr, error)) {
            g_object_unref(mstream);
            Context::free(ctx);
            return FALSE;
        }
        g_object_unref(mstream);
    }

    try {
        doc = lunasvg::Document::loadFromData(
            reinterpret_cast<char const *>(ctx->filebuf.data()),
            ctx->filebuf.size()
        );
    } catch (std::bad_alloc &) {
        g_abort();
    }

    /* in case input was compressed */
    g_free(bufp);

    if (!doc) {
        *error = g_error_new(
            G_IO_ERROR, G_IO_ERROR_FAILED, "Failed loading document."
        );
        Context::free(ctx);
        return FALSE;
    }

    gint w = gint(doc->width()), h = gint(doc->height());
    ctx->size_func(&w, &h, ctx->user_data);

    auto *bmap = static_cast<lunasvg::Bitmap *>(
        g_malloc(sizeof(lunasvg::Bitmap))
    );

    try {
        new (bmap) lunasvg::Bitmap{doc->renderToBitmap(w, h)};
    } catch (std::bad_alloc &) {
        g_abort();
    }

    if (!bmap->valid()) {
        *error = g_error_new(
            G_IO_ERROR, G_IO_ERROR_FAILED, "Invalid bitmap."
        );
        Context::free(ctx);
        data_free(nullptr, bmap);
        return FALSE;
    }

    auto *pbuf = gdk_pixbuf_new_from_data(
        bmap->data(), GDK_COLORSPACE_RGB, TRUE, 8,
        bmap->width(), bmap->height(),
        bmap->stride(), data_free, bmap
    );

    if (ctx->prepared_func) {
        ctx->prepared_func(pbuf, nullptr, ctx->user_data);
    }
    if (ctx->updated_func) {
        ctx->updated_func(
            pbuf, 0, 0, gdk_pixbuf_get_width(pbuf),
            gdk_pixbuf_get_height(pbuf), ctx->user_data
        );
    }

    Context::free(ctx);
    return TRUE;
}

/* module hookup */

extern "C" G_MODULE_EXPORT void fill_vtable(GdkPixbufModule *mod) {
    mod->begin_load = &begin_load;
    mod->stop_load = &stop_load;
    mod->load_increment = &load_increment;
}

extern "C" G_MODULE_EXPORT void fill_info(GdkPixbufFormat *info) {
    info->name = const_cast<char *>("svg");
    info->signature = const_cast<GdkPixbufModulePattern *>(svg_sigs);
    info->description = const_cast<char *>("Scalable Vector Graphics");
    info->mime_types = const_cast<char **>(svg_mimes);
    info->extensions = const_cast<char **>(svg_exts);
    info->flags = GDK_PIXBUF_FORMAT_SCALABLE | GDK_PIXBUF_FORMAT_THREADSAFE;
    info->license = const_cast<char *>("MIT");
}
