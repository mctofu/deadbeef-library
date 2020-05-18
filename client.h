#include <gdk/gdk.h>

typedef struct {
    gchar *name;
    gchar *uri;
    gboolean folder;
} BrowseItem;

GSList *        client_browse_items (const gchar *path, const gchar *search, const gint browse_type, GError **error);