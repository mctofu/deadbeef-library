/*
    Filebrowser plugin for the DeaDBeeF audio player
    http://sourceforge.net/projects/deadbeef-fb/

    Copyright (C) 2011-2016 Jan D. Behrens <zykure@web.de>

    Based on Geany treebrowser plugin:
        treebrowser.c - v0.20
        Copyright 2010 Adrian Dimitrov <dimitrov.adrian@gmail.com>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <gtk/gtk.h>


/* Config options */

#define     CONFSTR_FB_ENABLED              "libbrowser.enabled"
#define     CONFSTR_FB_HIDDEN               "libbrowser.hidden"
#define     CONFSTR_FB_DEFAULT_PATH         "libbrowser.defaultpath"
#define     CONFSTR_FB_SHOW_ICONS           "libbrowser.showicons"
#define     CONFSTR_FB_SHOW_TREE_LINES      "libbrowser.treelines"
#define     CONFSTR_FB_WIDTH                "libbrowser.sidebar_width"
#define     CONFSTR_FB_SHOW_COVERART        "libbrowser.show_coverart"
#define     CONFSTR_FB_COVERART_SIZE        "libbrowser.coverart_size"
#define     CONFSTR_FB_COVERART_SCALE       "libbrowser.coverart_scale"
#define     CONFSTR_FB_SAVE_TREEVIEW        "libbrowser.save_treeview"
#define     CONFSTR_FB_EXPANDED_ROWS        "libbrowser.expanded_rows"
#define     CONFSTR_FB_COLOR_BG             "libbrowser.bgcolor"
#define     CONFSTR_FB_COLOR_FG             "libbrowser.fgcolor"
#define     CONFSTR_FB_COLOR_BG_SEL         "libbrowser.bgcolor_selected"
#define     CONFSTR_FB_COLOR_FG_SEL         "libbrowser.fgcolor_selected"
#define     CONFSTR_FB_COLOR_FG             "libbrowser.fgcolor"
#define     CONFSTR_FB_FONT_SIZE            "libbrowser.font_size"
#define     CONFSTR_FB_ICON_SIZE            "libbrowser.icon_size"
#define     CONFSTR_FB_SORT_TREEVIEW        "libbrowser.sort_treeview"
#define     CONFSTR_FB_SEARCH_DELAY         "libbrowser.search_delay"
#define     CONFSTR_FB_FULLSEARCH_WAIT      "libbrowser.fullsearch_wait"
#define     CONFSTR_FB_HIDE_NAVIGATION      "libbrowser.hide_navigation"
#define     CONFSTR_FB_HIDE_SEARCH          "libbrowser.hide_search"
#define     CONFSTR_FB_HIDE_TOOLBAR         "libbrowser.hide_toolbar"

#define     DEFAULT_FB_DEFAULT_PATH         ""

/* Treebrowser setup */
enum
{
    TREEBROWSER_COLUMN_ICON             = 0,
    TREEBROWSER_COLUMN_NAME             = 1,
    TREEBROWSER_COLUMN_URI              = 2,        // needed for browsing
    TREEBROWSER_COLUMN_TOOLTIP          = 3,
    TREEBROWSER_COLUMN_FLAG             = 4,        // needed for separator
    TREEBROWSER_COLUMNC,                            // count is set automatically

    TREEBROWSER_RENDER_ICON             = 0,
    TREEBROWSER_RENDER_TEXT             = 1,

    TREEBROWSER_FLAGS_SEPARATOR         = -1,
    TREEBROWSER_FLAGS_BOOKMARK          = -2
};


/* Adding files to playlists */
enum
{
    PLT_CURRENT         = -1,
    PLT_NEW             = -2
};


static void         gtkui_update_listview_headers (void);
static void         setup_dragdrop (void);
static void         save_config (void);
static void         save_config_expanded_rows (void);
static void         load_config (void);
static void         load_config_expanded_rows (void);
static gchar *      get_default_dir (void);
static GdkPixbuf *  get_icon_from_cache (const gchar *uri, const gchar *coverart);
static GdkPixbuf *  get_icon_for_uri (gchar *uri);
static void         get_uris_from_selection (gpointer data, gpointer userdata);
static void         expand_all();
static void         collapse_all();

static int          handle_message (uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2);
static int          on_config_changed (uintptr_t data);
static void         on_drag_data_get (GtkWidget *widget, GdkDragContext *drag_context, GtkSelectionData *sdata,
                    guint info, guint time, gpointer user_data);

static int          create_menu_entry (void);
static int          create_interface (GtkWidget *cont);
static int          restore_interface (GtkWidget *cont);
static GtkWidget *  create_popup_menu (GtkTreePath *path, gchar *name, GList *uri_list);
static GtkWidget *  create_view_and_model (void);
static void         create_sidebar (void);
#if GTK_CHECK_VERSION(3,16,0)
static void         create_settings_dialog (void);
#endif

static void         add_uri_to_playlist_worker (void *data);
static void         add_uri_to_playlist (GList *uri_list, int plt, int append, int threaded);

static gboolean     treeview_row_expanded_iter (GtkTreeView *tree_view, GtkTreeIter *iter);
static GSList *     treeview_check_expanded (gchar *uri);
static void         treeview_clear_expanded (void);
static void         treeview_restore_expanded (gpointer parent);
static gboolean     treeview_separator_func (GtkTreeModel *model, GtkTreeIter *iter, gpointer data);
static gboolean     treebrowser_checkdir (const gchar *directory);
static void         treebrowser_browse_dir (gpointer directory);
static gboolean     treebrowser_browse (gchar *directory, gpointer parent);

static void         on_mainmenu_toggle (GtkMenuItem *menuitem, gpointer *user_data);

static void         on_menu_add (GtkMenuItem *menuitem, GList *uri_list);
static void         on_menu_add_current (GtkMenuItem *menuitem, GList *uri_list);
static void         on_menu_replace_current (GtkMenuItem *menuitem, GList *uri_list);
static void         on_menu_add_new (GtkMenuItem *menuitem, GList *uri_list);
static void         on_menu_refresh (GtkMenuItem *menuitem, gpointer *user_data);
static void         on_menu_expand_one (GtkMenuItem *menuitem, gpointer *user_data);
static void         on_menu_expand_all (GtkMenuItem *menuitem, gpointer *user_data);
static void         on_menu_collapse_all (GtkMenuItem *menuitem, gpointer *user_data);
static void         on_menu_copy_uri (GtkMenuItem *menuitem, GList *uri_list);
static void         on_menu_show_bookmarks (GtkMenuItem *menuitem, gpointer *user_data);
static void         on_menu_show_hidden_files( GtkMenuItem *menuitem, gpointer *user_data);
static void         on_menu_use_filter (GtkMenuItem *menuitem, gpointer *user_data);
static void         on_menu_hide_navigation (GtkMenuItem *menuitem, gpointer *user_data);
static void         on_menu_hide_search (GtkMenuItem *menuitem, gpointer *user_data);
static void         on_menu_hide_toolbar (GtkMenuItem *menuitem, gpointer *user_data);
#if GTK_CHECK_VERSION(3,16,0)
static void         on_menu_config (GtkMenuItem *menuitem, gpointer user_data);
#endif

static void         on_button_add_current (void);
static void         on_button_replace_current (void);
static void         on_button_refresh (void);
static void         on_addressbar_changed (void);
static void         on_searchbar_changed (void);
#if !GTK_CHECK_VERSION(3,6,0)
static void         on_searchbar_cleared (void);
#endif

static void         treeview_activate (GtkTreePath *path, GtkTreeViewColumn *column, GtkTreeSelection *selection,
                    gboolean create, gboolean append, gboolean play);
static gboolean     on_treeview_key_press (GtkWidget *widget, GdkEventKey *event, GtkTreeSelection *selection);
static gboolean     on_treeview_mouseclick_press (GtkWidget *widget, GdkEventButton *event, GtkTreeSelection *selection);
static gboolean     on_treeview_mouseclick_release (GtkWidget *widget, GdkEventButton *event, GtkTreeSelection *selection);
static gboolean     on_treeview_mousemove (GtkWidget *widget, GdkEventButton *event);
static void         on_treeview_changed (GtkWidget *widget, gpointer user_data);
static void         on_treeview_row_expanded (GtkWidget *widget, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data);
static void         on_treeview_row_collapsed (GtkWidget *widget, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data);

static gboolean     treeview_update (void *ctx);
static gboolean     libbrowser_init (void *ctx);
static int          plugin_init (void);
static int          plugin_cleanup (void);


/* Exported public functions */

int                 libbrowser_start (void);
int                 libbrowser_stop (void);
int                 libbrowser_startup (GtkWidget *);
int                 libbrowser_shutdown (GtkWidget *);
int                 libbrowser_connect (void);
int                 libbrowser_disconnect (void);
DB_plugin_t *       ddb_misc_libbrowser_load (DB_functions_t *ddb);
