#include <gdk/gdk.h>

typedef struct {
    gchar *name;
    gchar *uri;
    gchar *image_uri;
    gboolean folder;
} BrowseItem;

void            client_connect ();
void            client_disconnect ();
GPtrArray *     client_browse_items (const gchar *uri, const gchar *search, const gint browse_type, GError **error);
GPtrArray *     client_media_items (const gchar *uri, const gchar *search, const gint browse_type, GError **error);