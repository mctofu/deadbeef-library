#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

/* Helper macros */
#define foreach_slist_free(node,list)               \
                for (node = list, list = NULL; g_slist_free_1(list), node != NULL; list = node, node = node->next)
#define foreach_dir(filename,dir)                   \
                for ((filename) = g_dir_read_name(dir); (filename) != NULL; (filename) = g_dir_read_name(dir))
#define NZV(ptr)                                    \
                (G_LIKELY((ptr)) && G_LIKELY((ptr)[0]))
#define setptr(ptr,result)                          \
                { gpointer setptr_tmp = ptr; ptr = result; g_free(setptr_tmp); }
#define GLADE_HOOKUP_OBJECT(component,widget,name)  \
                g_object_set_data_full (G_OBJECT (component), name, gtk_widget_ref (widget), (GDestroyNotify) gtk_widget_unref)


GdkPixbuf *     utils_pixbuf_from_stock (const gchar *icon_name, gint size);
gboolean        utils_str_equal (const gchar *a, const gchar *b);
gint            utils_str_casecmp (const gchar *s1, const gchar *s2);
GSList *        utils_get_file_list_full (const gchar *path, gboolean full_path, gboolean sort, GError **error);
GSList *        utils_get_file_list (const gchar *path, guint *length, gboolean sort, GError **error);
gchar *         utils_get_utf8_from_locale(const gchar *locale_text);
gchar *         utils_expand_home_dir (const gchar *path);
gchar *         utils_tooltip_from_uri (const gchar *uri);
gchar *         utils_make_cache_path (const gchar *uri, gint imgsize, gboolean scale);
gint            utils_check_dir (const gchar *dir, mode_t mode);
void            utils_construct_style (GtkWidget *widget, const gchar *bgcolor, const gchar *fgcolor,
                const gchar *bgcolor_sel, const gchar *fgcolor_sel);

gboolean        tree_view_expand_rows_recursive (GtkTreeModel *model, GtkTreeView *view, GtkTreePath *parent, gint max_depth);
gboolean        tree_view_collapse_rows_recursive (GtkTreeModel *model, GtkTreeView *view, GtkTreePath *parent, gint max_depth);
void            tree_store_iter_clear_nodes (GtkTreeStore *store, gpointer iter, gboolean delete_root);

#if GTK_CHECK_VERSION(3,6,0)
gint            gtk_grid_get_number_of_rows (GtkGrid *grid, gint column);
gchar *         gtk_color_chooser_get_hex (GtkColorChooser *chooser);
#endif
