#include "client.h"
#include "../music-library-grpc/cgo/build/client.h"

void
client_connect ()
{
    MLibGRPC_Connect();
}

void
client_disconnect ()
{
    MLibGRPC_Disconnect();
}

GSList *
client_browse_items (const gchar *uri, const gchar *search, const gint browse_type, GError **error)
{
    GSList *list = NULL;

    MLibGRPC_BrowseItem **results = MLibGRPC_Browse((char*)uri, (char*)search, browse_type + 1);

    MLibGRPC_BrowseItem **idx = results;
    for (MLibGRPC_BrowseItem *result = *idx; result; result = *++idx) {
        BrowseItem *item = g_malloc(sizeof *item);
        item->name = g_strdup(result->name);
        item->uri = g_strdup(result->uri);
        item->image_uri = g_strdup(result->image_uri);
        item->folder = result->folder;
        list = g_slist_prepend (list, item);
        free(result->name);
        free(result->uri);
        free(result->image_uri);
        free(result);
    }

    free(results);

    return list;
}

GSList *
client_media_items (const gchar *uri, const gchar *search, const gint browse_type, GError **error)
{
    GSList *list = NULL;

    gchar **results = MLibGRPC_Media((char*)uri, (char*)search, browse_type + 1);

    gchar **idx = results;
    for (gchar *result = *idx; result; result = *++idx) {
        list = g_slist_prepend (list, g_strdup(result));
        free(result);
    }

    free(results);

    return list;
}