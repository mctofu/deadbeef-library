#include <gdk/gdk.h>

typedef struct {
    gchar *name;
    gchar *uri;
    gboolean folder;
} BrowseItem;

GSList *        client_browse_items (const gchar *path, guint *length, gboolean sort, GError **error);