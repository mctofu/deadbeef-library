#include <gtk/gtk.h>
#include <gdk/gdk.h>

GdkPixbuf *     utils_pixbuf_from_stock (const gchar *icon_name, gint size);
gboolean        utils_str_equal (const gchar *a, const gchar *b);
gchar *         utils_get_utf8_from_locale(const gchar *locale_text);
gchar *         utils_tooltip_from_uri (const gchar *uri);
gchar *         utils_make_cache_path (const gchar *uri, gint imgsize, gboolean scale);
gint            utils_check_dir (const gchar *dir, mode_t mode);
void            utils_construct_style (GtkWidget *widget, const gchar *bgcolor, const gchar *fgcolor,
                const gchar *bgcolor_sel, const gchar *fgcolor_sel);

gboolean        tree_view_expand_rows_recursive (GtkTreeModel *model, GtkTreeView *view, GtkTreePath *parent, gint max_depth);
gboolean        tree_view_collapse_rows_recursive (GtkTreeModel *model, GtkTreeView *view, GtkTreePath *parent, gint max_depth);
void            tree_store_iter_clear_nodes (GtkTreeStore *store, gpointer iter, gboolean delete_root);

#if GTK_CHECK_VERSION(3,6,0)
gchar *         gtk_color_chooser_get_hex (GtkColorChooser *chooser);
#endif
