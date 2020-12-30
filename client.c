#include <gdk/gdk.h>

#include "client.h"
#include "../musiclib-grpc/cgo/build/client.h"

#ifdef DEBUG
#define CLIENT_DEBUG TRUE
#else
#define CLIENT_DEBUG FALSE
#endif


void
client_connect ()
{
    MLibGRPC_Connect ("127.0.0.1:8337", FALSE, CLIENT_DEBUG);
}

void
client_disconnect ()
{
    MLibGRPC_Disconnect ();
}

GPtrArray *
client_browse_items (const gchar *uri, const gchar *search, const gint browse_type)
{
    MLibGRPC_BrowseItems *results = MLibGRPC_Browse ((char*)uri, (char*)search, browse_type + 1);

    GPtrArray *items = g_ptr_array_sized_new (results->count);

    MLibGRPC_BrowseItem **idx = results->items;
    int i = 0;
    for (MLibGRPC_BrowseItem *result = *idx; i < results->count; result = *++idx, i++) {
        BrowseItem *item = g_malloc (sizeof *item);
        item->name = g_strdup (result->name);
        item->uri = g_strdup (result->uri);
        item->image_uri = g_strdup (result->image_uri);
        item->folder = result->folder;
        g_ptr_array_add (items, item);
        free (result->name);
        free (result->uri);
        free (result->image_uri);
        free (result);
    }
    free (results->items);
    free (results);

    return items;
}

GPtrArray *
client_media_items (const gchar *uri, const gchar *search, const gint browse_type)
{
    MLibGRPC_MediaItems *results = MLibGRPC_Media ((char*)uri, (char*)search, browse_type + 1);

    GPtrArray *items = g_ptr_array_sized_new (results->count);

    gchar **idx = results->items;
    int i = 0;
    for (gchar *result = *idx; i < results->count; result = *++idx, i++) {
        g_ptr_array_add (items, g_strdup (result));
        free (result);
    }
    free (results->items);
    free (results);

    return items;
}
