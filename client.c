#include "client.h"
#include "../music-library-grpc/cgo/build/client.h"

GSList *
client_browse_items (const gchar *path, const gchar *search, const gint browse_type, GError **error)
{
    GSList *list = NULL;
    MLibGRPC_Connect();
    MLibGRPC_BrowseItem **results = MLibGRPC_Browse((char*)path, (char*)search, browse_type);

    MLibGRPC_BrowseItem **idx = results;
    for (MLibGRPC_BrowseItem *result = *idx; result; result = *++idx) {
        BrowseItem *item = g_malloc(sizeof *item);
        item->name = g_strdup(result->name);
        item->uri = g_strdup(result->uri);
        item->folder = result->folder;
        list = g_slist_prepend (list, item);
        free(result->name);
        free(result->uri);
        free(result);
    }

    free(results);

    MLibGRPC_Disconnect();

    return list;
}
