#include <gdk/gdk.h>

typedef struct {
    gchar *name;
    gchar *uri;
    gchar *image_uri;
    gboolean folder;
} BrowseItem;

void            client_connect ();
void            client_disconnect ();
GSList *        client_browse_items (const gchar *path, const gchar *search, const gint browse_type, GError **error);