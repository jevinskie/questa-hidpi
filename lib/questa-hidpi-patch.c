#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tk.h>

#define __USE_POSIX
#undef G_DISABLE_ASSERT
#include "frida-gum.h"

typedef int (*Tk_ConfigureWidget_fptr_t)(Tcl_Interp *interp, Tk_Window tkwin,
                                         const Tk_ConfigSpec *specs, int argc, const char **argv,
                                         char *widgRec, int flags);

typedef struct _TkListener TkListener;
typedef enum _TkHookId TkHookId;
typedef struct _TkInvocationData TkInvocationData;

struct _TkListener {
    GObject parent;
};

enum _TkHookId {
    HOOK_TK_CONFIGUREWIDGET,
};

struct _TkInvocationData {
    const char **argv_mod;
};

static void tk_listener_iface_init(gpointer g_iface, gpointer iface_data);

#define TK_TYPE_LISTENER (tk_listener_get_type())
G_DECLARE_FINAL_TYPE(TkListener, tk_listener, TK, LISTENER, GObject)
G_DEFINE_TYPE_EXTENDED(TkListener, tk_listener, G_TYPE_OBJECT, 0,
                       G_IMPLEMENT_INTERFACE(GUM_TYPE_INVOCATION_LISTENER, tk_listener_iface_init))

#define SCALE(x) ((x)*2)

static void tk_listener_on_enter(GumInvocationListener *listener, GumInvocationContext *ic) {
    (void)listener;
    TkHookId hook_id = GUM_IC_GET_FUNC_DATA(ic, TkHookId);

    switch (hook_id) {
    case HOOK_TK_CONFIGUREWIDGET: {
        TkInvocationData *id = GUM_IC_GET_INVOCATION_DATA(ic, TkInvocationData);
        int argc             = GPOINTER_TO_INT(gum_invocation_context_get_nth_argument(ic, 3));
        const char **argv    = (const char **)gum_invocation_context_get_nth_argument(ic, 4);
        int width_idx        = -1;
        int height_idx       = -1;
        for (int i = 0; i < argc; ++i) {
            if (!strcmp(argv[i], "-width")) {
                width_idx = i + 1;
            }
            if (!strcmp(argv[i], "-height")) {
                height_idx = i + 1;
            }
        }
        if (width_idx != -1 && height_idx != -1) {
            int num_pruned = 0;
            if (width_idx != -1) {
                num_pruned += 2;
            }
            if (height_idx != -1) {
                num_pruned += 2;
            }
            const char **argv_mod = malloc(sizeof(const char **) * (argc - num_pruned));
            g_assert(argv_mod);
            int argc_mod = 0;
            for (int i = 0; i < argc; ++i) {
                if (i == width_idx || i == width_idx - 1 || i == height_idx ||
                    i == height_idx - 1) {
                    // skip
                } else {
                    argv_mod[argc_mod] = argv[i];
                    argc_mod += 1;
                }
            }
            id->argv_mod = argv_mod;
            gum_invocation_context_replace_nth_argument(ic, 3, GSIZE_TO_POINTER(argc_mod));
            gum_invocation_context_replace_nth_argument(ic, 4, argv_mod);
        } else {
            id->argv_mod = NULL;
        }
    } break;
    default:
        g_assert(!"unhandled case");
    }
}

static void tk_listener_on_leave(GumInvocationListener *listener, GumInvocationContext *ic) {
    (void)listener;
    TkHookId hook_id = GUM_IC_GET_FUNC_DATA(ic, TkHookId);

    switch (hook_id) {
    case HOOK_TK_CONFIGUREWIDGET: {
        TkInvocationData *id = GUM_IC_GET_INVOCATION_DATA(ic, TkInvocationData);
        if (id->argv_mod) {
            free(id->argv_mod);
        }
    } break;
    default:
        g_assert(!"unhandled case");
    }
}

static void tk_listener_class_init(TkListenerClass *klass) {
    (void)klass;
    (void)TK_IS_LISTENER;
    (void)glib_autoptr_cleanup_TkListener;
}

static void tk_listener_iface_init(gpointer g_iface, gpointer iface_data) {
    GumInvocationListenerInterface *iface = g_iface;
    (void)iface_data;

    iface->on_enter = tk_listener_on_enter;
    iface->on_leave = tk_listener_on_leave;
}

static void tk_listener_init(TkListener *self) {
    (void)self;
}

static GumInterceptor *interceptor;
static GumInvocationListener *listener;
static gpointer Tk_ConfigureWidget_fptr;

__attribute__((constructor)) static void questa_hidpi_hook_ctor(void) {
    gum_init_embedded();

    gpointer Tk_ConfigureWidget_fptr =
        GSIZE_TO_POINTER(gum_module_find_export_by_name("libtk8.6.so", "Tk_ConfigureWidget"));
    if (!Tk_ConfigureWidget_fptr) {
        Tk_ConfigureWidget_fptr =
            GSIZE_TO_POINTER(gum_module_find_export_by_name(NULL, "Tk_ConfigureWidget"));
    }
    if (!Tk_ConfigureWidget_fptr) {
        return;
    }

    interceptor = gum_interceptor_obtain();
    g_assert(interceptor);
    listener = g_object_new(TK_TYPE_LISTENER, NULL);
    g_assert(listener);

    gum_interceptor_begin_transaction(interceptor);
    gum_interceptor_attach(interceptor, GSIZE_TO_POINTER(Tk_ConfigureWidget_fptr), listener,
                           GSIZE_TO_POINTER(HOOK_TK_CONFIGUREWIDGET));
    gum_interceptor_end_transaction(interceptor);
}

__attribute__((destructor)) static void questa_hidpi_hook_dtor(void) {
    if (Tk_ConfigureWidget_fptr) {
        g_assert(listener);
        g_object_unref(listener);
        g_assert(interceptor);
        g_object_unref(interceptor);
    }
    gum_deinit_embedded();
}
