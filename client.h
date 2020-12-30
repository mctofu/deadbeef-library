/**
 * This defines a very basic interface to a musiclib-grpc service.
 */

#include <gdk/gdk.h>

typedef struct {
    gchar *name;
    gchar *uri;
    gchar *image_uri;
    gboolean folder;
} BrowseItem;

void            client_connect ();
void            client_disconnect ();

// client_browse_items returns the next level of items under the item identified by uri.
GPtrArray *     client_browse_items (const gchar *uri, const gchar *search, const gint browse_type);

// client_media_items resolves a browse uri to a list of playable items.
// For a song this is typically the song itself. For a folder this would return
// all playable items under the folder.
GPtrArray *     client_media_items (const gchar *uri, const gchar *search, const gint browse_type);