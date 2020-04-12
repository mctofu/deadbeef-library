/*
    Library plugin for the DeaDBeeF audio player
    http://sourceforge.net/projects/deadbeef-fb/

    Copyright (C) 2011-2016 Jan D. Behrens <zykure@web.de>

    With contributions by:
        Tobias Bengfort <tobias.bengfort@posteo.de>
        420MuNkEy

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

/*
 * TODO: Add more config options
 *
 *   Content:
 *      CONFIG_SHOW_BOOKMARKS       -> Show/hide all bookmarks
 *      CONFIG_SHOW_BOOKMARKS_GTK   -> Enable/disable showing GTK bookmarks
 *      CONFIG_SHOW_BOOKMARKS_FILE  -> Enable/disable showing user bookmarks
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>

#include <deadbeef/deadbeef.h>
#include <deadbeef/gtkui_api.h>

#include "libbrowser.h"
#include "support.h"
#include "utils.h"

#ifdef DEBUG
#pragma message "DEBUG MODE ENABLED!"
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define trace(...) { fprintf (stderr, "libbrowser[" __FILE__ ":" TOSTRING(__LINE__) "] " __VA_ARGS__); }
#else
#define trace(...)
#endif

#define ICON_SIZE(s)  ( (s) < 24 ? (24) : (s) )  // size < 24 crashes plugin?

/*------------------*/
/* GLOBAL VARIABLES */
/*------------------*/


/* Hard-coded options */
// none so far...

/* Options changeable by user */
static gboolean             CONFIG_ENABLED              = TRUE;
static gboolean             CONFIG_HIDDEN               = FALSE;
static const gchar *        CONFIG_DEFAULT_PATH         = NULL;
static gboolean             CONFIG_SHOW_HIDDEN_FILES    = FALSE;
static gboolean             CONFIG_FILTER_ENABLED       = TRUE;
static const gchar *        CONFIG_FILTER               = NULL;
static gboolean             CONFIG_FILTER_AUTO          = TRUE;
static gboolean             CONFIG_SHOW_BOOKMARKS       = FALSE;
static const gchar *        CONFIG_BOOKMARKS_FILE       = NULL;
static gboolean             CONFIG_SHOW_ICONS           = TRUE;
static gboolean             CONFIG_SHOW_TREE_LINES      = FALSE;
static gint                 CONFIG_WIDTH                = 220;
static gboolean             CONFIG_SHOW_COVERART        = TRUE;
static const gchar *        CONFIG_COVERART             = NULL;
static gint                 CONFIG_COVERART_SIZE        = 24;
static gboolean             CONFIG_COVERART_SCALE       = TRUE;
static gboolean             CONFIG_SAVE_TREEVIEW        = TRUE;
static const gchar *        CONFIG_COLOR_BG             = NULL;
static const gchar *        CONFIG_COLOR_FG             = NULL;
static const gchar *        CONFIG_COLOR_BG_SEL         = NULL;
static const gchar *        CONFIG_COLOR_FG_SEL         = NULL;
static gint                 CONFIG_ICON_SIZE            = 24;
static gint                 CONFIG_FONT_SIZE            = 0;
static gboolean             CONFIG_SORT_TREEVIEW        = TRUE;
static gint                 CONFIG_SEARCH_DELAY         = 1000;
static gint                 CONFIG_FULLSEARCH_WAIT      = 5;
static gboolean             CONFIG_HIDE_NAVIGATION      = FALSE;
static gboolean             CONFIG_HIDE_SEARCH          = FALSE;
static gboolean             CONFIG_HIDE_TOOLBAR         = FALSE;

/* Internal variables */
static DB_misc_t            plugin;
static DB_functions_t *     deadbeef                    = NULL;
static ddb_gtkui_t *        gtkui_plugin                = NULL;
//static uintptr_t            treebrowser_mutex           = NULL;

static GtkWidget *          mainmenuitem                = NULL;
static GtkWidget *          vbox_playlist               = NULL;
static GtkWidget *          hbox_all                    = NULL;
static GtkWidget *          treeview                    = NULL;
static GtkTreeStore *       treestore                   = NULL;
static GtkWidget *          sidebar                     = NULL;
static GtkWidget *          sidebar_searchbox           = NULL;
static GtkWidget *          sidebar_addressbox          = NULL;
static GtkWidget *          sidebar_toolbar             = NULL;
static GtkWidget *          toolbar_button_add          = NULL;
static GtkWidget *          toolbar_button_replace      = NULL;
static GtkWidget *          addressbar                  = NULL;
static gchar *              addressbar_last_address     = NULL;
static GtkWidget *          searchbar                   = NULL;
static gchar *              searchbar_text              = NULL;
static GtkTreeIter          bookmarks_iter;
static gboolean             bookmarks_expanded          = FALSE;
static GtkTreeViewColumn *  treeview_column_icon        = NULL;
static GtkTreeViewColumn *  treeview_column_text        = NULL;
static GSList *             expanded_rows               = NULL;
static gchar *              known_extensions            = NULL;
static gboolean             flag_on_expand_refresh      = FALSE;

static gint                 mouseclick_lastpos[2]       = { 0, 0 };
static gboolean             mouseclick_dragwait         = FALSE;
static GtkTreePath *        mouseclick_lastpath         = NULL;

static gboolean             all_expanded                = FALSE;
static gint64               last_searchbar_change       = 0;


/*------------------*/
/* HELPER FUNCTIONS */
/*------------------*/


static void
gtkui_update_listview_headers (void)
{
    GtkWidget *headers_menuitem = lookup_widget (gtkui_plugin->get_mainwin (), "view_headers");
    gboolean menu_enabled = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (headers_menuitem));
    gboolean conf_enabled = deadbeef->conf_get_int ("gtkui.headers.visible", 1);

    /* Nasty workaround: emit the "headers visible" menuitem signal once or
     * twice to update the playlist view
     * TODO (upstream): Would be better to have direct acces to the ddblistview instance.
     */
    if (! conf_enabled)
    {
        if (! menu_enabled)
            g_signal_emit_by_name (headers_menuitem, "activate");
        g_signal_emit_by_name (headers_menuitem, "activate");
    }
}

static gboolean
bookmarks_foreach_func (GtkTreeModel *model, GtkTreePath  *path, GtkTreeIter *iter, GList **rowref_list)
{
    g_assert (rowref_list != NULL);

    gboolean flag;
    gtk_tree_model_get (model, iter, TREEBROWSER_COLUMN_FLAG, &flag, -1);

    if (flag == TREEBROWSER_FLAGS_BOOKMARK)
    {
        GtkTreeRowReference  *rowref;
        rowref = gtk_tree_row_reference_new (model, path);
        *rowref_list = g_list_append (*rowref_list, rowref);
    }

    return FALSE; // do not stop walking the store, call us with next row
}

static void
setup_dragdrop (void)
{
    GtkTargetEntry entry =
    {
        .target = "text/uri-list",
        .flags = GTK_TARGET_SAME_APP,
        .info = 0
    };

    gtk_drag_source_set (treeview, GDK_BUTTON1_MASK, &entry, 1,
                    GDK_ACTION_COPY | GDK_ACTION_MOVE);
    gtk_drag_source_add_uri_targets (treeview);
    g_signal_connect (treeview, "drag-data-get", G_CALLBACK (on_drag_data_get), NULL);
}

static void
create_autofilter (void)
{
    // This uses GString to dynamically append all known extensions into a string
    GString *buf = g_string_sized_new (256);  // reasonable initial size

    struct DB_decoder_s **decoders = deadbeef->plug_get_decoder_list ();
    for (gint i = 0; decoders[i]; i++)
    {
        const gchar **exts = decoders[i]->exts;
        for (gint j = 0; exts[j]; j++)
            g_string_append_printf (buf, "*.%s;", exts[j]);
    }

    if (known_extensions)
        g_free (known_extensions);
    known_extensions = g_string_free (buf, FALSE);  // frees GString, but leaves gchar* behind

    trace("autofilter: %s\n", known_extensions);
}

static void
save_config (void)
{
    trace("save config\n");

    deadbeef->conf_set_int (CONFSTR_FB_ENABLED,             CONFIG_ENABLED);
    deadbeef->conf_set_int (CONFSTR_FB_HIDDEN,              CONFIG_HIDDEN);
    deadbeef->conf_set_int (CONFSTR_FB_SHOW_HIDDEN_FILES,   CONFIG_SHOW_HIDDEN_FILES);
    deadbeef->conf_set_int (CONFSTR_FB_FILTER_ENABLED,      CONFIG_FILTER_ENABLED);
    deadbeef->conf_set_int (CONFSTR_FB_FILTER_AUTO,         CONFIG_FILTER_AUTO);
    deadbeef->conf_set_int (CONFSTR_FB_SHOW_BOOKMARKS,      CONFIG_SHOW_BOOKMARKS);
    deadbeef->conf_set_int (CONFSTR_FB_SHOW_ICONS,          CONFIG_SHOW_ICONS);
    deadbeef->conf_set_int (CONFSTR_FB_SHOW_TREE_LINES,     CONFIG_SHOW_TREE_LINES);
    deadbeef->conf_set_int (CONFSTR_FB_WIDTH,               CONFIG_WIDTH);
    deadbeef->conf_set_int (CONFSTR_FB_SHOW_COVERART,       CONFIG_SHOW_COVERART);
    deadbeef->conf_set_int (CONFSTR_FB_COVERART_SIZE,       CONFIG_COVERART_SIZE);
    deadbeef->conf_set_int (CONFSTR_FB_COVERART_SCALE,      CONFIG_COVERART_SCALE);
    deadbeef->conf_set_int (CONFSTR_FB_SAVE_TREEVIEW,       CONFIG_SAVE_TREEVIEW);
    deadbeef->conf_set_int (CONFSTR_FB_ICON_SIZE,           CONFIG_ICON_SIZE);
    deadbeef->conf_set_int (CONFSTR_FB_FONT_SIZE,           CONFIG_FONT_SIZE);
    deadbeef->conf_set_int (CONFSTR_FB_SORT_TREEVIEW,       CONFIG_SORT_TREEVIEW);
    deadbeef->conf_set_int (CONFSTR_FB_SEARCH_DELAY,        CONFIG_SEARCH_DELAY);
    deadbeef->conf_set_int (CONFSTR_FB_FULLSEARCH_WAIT,     CONFIG_FULLSEARCH_WAIT);
    deadbeef->conf_set_int (CONFSTR_FB_HIDE_NAVIGATION,     CONFIG_HIDE_NAVIGATION);
    deadbeef->conf_set_int (CONFSTR_FB_HIDE_SEARCH,         CONFIG_HIDE_SEARCH);
    deadbeef->conf_set_int (CONFSTR_FB_HIDE_TOOLBAR,        CONFIG_HIDE_TOOLBAR);

    if (CONFIG_DEFAULT_PATH)
        deadbeef->conf_set_str (CONFSTR_FB_DEFAULT_PATH,    CONFIG_DEFAULT_PATH);
    if (CONFIG_FILTER)
        deadbeef->conf_set_str (CONFSTR_FB_FILTER,          CONFIG_FILTER);
    if (CONFIG_COVERART)
        deadbeef->conf_set_str (CONFSTR_FB_COVERART,        CONFIG_COVERART);
    if (CONFIG_BOOKMARKS_FILE)
        deadbeef->conf_set_str (CONFSTR_FB_BOOKMARKS_FILE,  CONFIG_BOOKMARKS_FILE);
    if (CONFIG_COLOR_BG)
        deadbeef->conf_set_str (CONFSTR_FB_COLOR_BG,        CONFIG_COLOR_BG);
    if (CONFIG_COLOR_FG)
        deadbeef->conf_set_str (CONFSTR_FB_COLOR_FG,        CONFIG_COLOR_FG);
    if (CONFIG_COLOR_BG_SEL)
        deadbeef->conf_set_str (CONFSTR_FB_COLOR_BG_SEL,    CONFIG_COLOR_BG_SEL);
    if (CONFIG_COLOR_FG_SEL)
        deadbeef->conf_set_str (CONFSTR_FB_COLOR_FG_SEL,    CONFIG_COLOR_FG_SEL);

    if (CONFIG_SAVE_TREEVIEW)
        save_config_expanded_rows ();
}

static void
save_config_expanded_rows ()
{
    // prevent overwriting with an empty list
    if (! expanded_rows)
        return;

    GString *config_expanded_rows_str = g_string_new ("");
    GSList *node;
    for (node = expanded_rows->next; node; node = node->next)  // first item is always NULL
    {
        if (config_expanded_rows_str->len > 0)
            config_expanded_rows_str = g_string_append_c (config_expanded_rows_str, ' ');
        config_expanded_rows_str = g_string_append (config_expanded_rows_str, node->data);
    }
    gchar *config_expanded_rows = g_string_free (config_expanded_rows_str, FALSE);

    trace("expanded rows: %s\n", config_expanded_rows);

    deadbeef->conf_set_str (CONFSTR_FB_EXPANDED_ROWS, config_expanded_rows);
    g_free (config_expanded_rows);
}

static void
load_config (void)
{
    trace("load config\n");

    if (CONFIG_DEFAULT_PATH)
        g_free ((gchar*) CONFIG_DEFAULT_PATH);
    if (CONFIG_FILTER)
        g_free ((gchar*) CONFIG_FILTER);
    if (CONFIG_COVERART)
        g_free ((gchar*) CONFIG_COVERART);
    if (CONFIG_BOOKMARKS_FILE)
        g_free ((gchar*) CONFIG_BOOKMARKS_FILE);
    if (CONFIG_COLOR_BG)
        g_free ((gchar*) CONFIG_COLOR_BG);
    if (CONFIG_COLOR_FG)
        g_free ((gchar*) CONFIG_COLOR_FG);
    if (CONFIG_COLOR_BG_SEL)
        g_free ((gchar*) CONFIG_COLOR_BG_SEL);
    if (CONFIG_COLOR_FG_SEL)
        g_free ((gchar*) CONFIG_COLOR_FG_SEL);

    deadbeef->conf_lock ();

    CONFIG_ENABLED              = deadbeef->conf_get_int (CONFSTR_FB_ENABLED,             TRUE);
    CONFIG_HIDDEN               = deadbeef->conf_get_int (CONFSTR_FB_HIDDEN,              FALSE);
    CONFIG_SHOW_HIDDEN_FILES    = deadbeef->conf_get_int (CONFSTR_FB_SHOW_HIDDEN_FILES,   FALSE);
    CONFIG_FILTER_ENABLED       = deadbeef->conf_get_int (CONFSTR_FB_FILTER_ENABLED,      TRUE);
    CONFIG_FILTER_AUTO          = deadbeef->conf_get_int (CONFSTR_FB_FILTER_AUTO,         TRUE);
    CONFIG_SHOW_BOOKMARKS       = deadbeef->conf_get_int (CONFSTR_FB_SHOW_BOOKMARKS,      TRUE);
    CONFIG_SHOW_ICONS           = deadbeef->conf_get_int (CONFSTR_FB_SHOW_ICONS,          TRUE);
    CONFIG_SHOW_TREE_LINES      = deadbeef->conf_get_int (CONFSTR_FB_SHOW_TREE_LINES,     FALSE);
    CONFIG_WIDTH                = deadbeef->conf_get_int (CONFSTR_FB_WIDTH,               220);
    CONFIG_SHOW_COVERART        = deadbeef->conf_get_int (CONFSTR_FB_SHOW_COVERART,       TRUE);
    CONFIG_COVERART_SIZE        = deadbeef->conf_get_int (CONFSTR_FB_COVERART_SIZE,       24);
    CONFIG_COVERART_SCALE       = deadbeef->conf_get_int (CONFSTR_FB_COVERART_SCALE,      TRUE);
    CONFIG_SAVE_TREEVIEW        = deadbeef->conf_get_int (CONFSTR_FB_SAVE_TREEVIEW,       TRUE);
    CONFIG_ICON_SIZE            = deadbeef->conf_get_int (CONFSTR_FB_ICON_SIZE,           24);
    CONFIG_FONT_SIZE            = deadbeef->conf_get_int (CONFSTR_FB_FONT_SIZE,           0);
    CONFIG_SORT_TREEVIEW        = deadbeef->conf_get_int (CONFSTR_FB_SORT_TREEVIEW,       TRUE);
    CONFIG_SEARCH_DELAY         = deadbeef->conf_get_int (CONFSTR_FB_SEARCH_DELAY,        1000);
    CONFIG_FULLSEARCH_WAIT      = deadbeef->conf_get_int (CONFSTR_FB_FULLSEARCH_WAIT,     5);
    CONFIG_HIDE_NAVIGATION      = deadbeef->conf_get_int (CONFSTR_FB_HIDE_NAVIGATION,     FALSE);
    CONFIG_HIDE_SEARCH          = deadbeef->conf_get_int (CONFSTR_FB_HIDE_SEARCH,         FALSE);
    CONFIG_HIDE_TOOLBAR         = deadbeef->conf_get_int (CONFSTR_FB_HIDE_TOOLBAR,        FALSE);

    CONFIG_DEFAULT_PATH         = g_strdup (deadbeef->conf_get_str_fast (CONFSTR_FB_DEFAULT_PATH,   DEFAULT_FB_DEFAULT_PATH));
    CONFIG_FILTER               = g_strdup (deadbeef->conf_get_str_fast (CONFSTR_FB_FILTER,         DEFAULT_FB_FILTER));
    CONFIG_COVERART             = g_strdup (deadbeef->conf_get_str_fast (CONFSTR_FB_COVERART,       DEFAULT_FB_COVERART));
    CONFIG_BOOKMARKS_FILE       = g_strdup (deadbeef->conf_get_str_fast (CONFSTR_FB_BOOKMARKS_FILE, DEFAULT_FB_BOOKMARKS_FILE));
    CONFIG_COLOR_BG             = g_strdup (deadbeef->conf_get_str_fast (CONFSTR_FB_COLOR_BG,       ""));
    CONFIG_COLOR_FG             = g_strdup (deadbeef->conf_get_str_fast (CONFSTR_FB_COLOR_FG,       ""));
    CONFIG_COLOR_BG_SEL         = g_strdup (deadbeef->conf_get_str_fast (CONFSTR_FB_COLOR_BG_SEL,   ""));
    CONFIG_COLOR_FG_SEL         = g_strdup (deadbeef->conf_get_str_fast (CONFSTR_FB_COLOR_FG_SEL,   ""));

    if (CONFIG_SAVE_TREEVIEW)
        load_config_expanded_rows ();

    deadbeef->conf_unlock ();

    utils_construct_style (treeview, CONFIG_COLOR_BG, CONFIG_COLOR_FG, CONFIG_COLOR_BG_SEL, CONFIG_COLOR_FG_SEL);

    trace("config loaded - new settings: \n"
        "enabled:           %d \n"
        "hidden:            %d \n"
        "defaultpath:       %s \n"
        "show_hidden:       %d \n"
        "filter_enabled:    %d \n"
        "filter:            %s \n"
        "filter_auto:       %d \n"
        "show_bookmarks:    %d \n"
        "extra_bookmarks:   %s \n"
        "show_icons:        %d \n"
        "tree_lines:        %d \n"
        "width:             %d \n"
        "show_coverart:     %d \n"
        "coverart:          %s \n"
        "coverart_size:     %d \n"
        "coverart_scale:    %d \n"
        "save_treeview:     %d \n"
        "bgcolor:           %s \n"
        "fgcolor:           %s \n"
        "bgcolor_sel:       %s \n"
        "fgcolor_sel:       %s \n"
        "icon_size:         %d \n"
        "font_size:         %d \n"
        "sort_treeview:     %d \n"
        "search_delay:      %d \n"
        "fullsearch_wait:   %d \n"
        "hide_navigation:   %d \n"
        "hide_search:       %d \n"
        "hide_toolbar:      %d \n",
        CONFIG_ENABLED,
        CONFIG_HIDDEN,
        CONFIG_DEFAULT_PATH,
        CONFIG_SHOW_HIDDEN_FILES,
        CONFIG_FILTER_ENABLED,
        CONFIG_FILTER,
        CONFIG_FILTER_AUTO,
        CONFIG_SHOW_BOOKMARKS,
        CONFIG_BOOKMARKS_FILE,
        CONFIG_SHOW_ICONS,
        CONFIG_SHOW_TREE_LINES,
        CONFIG_WIDTH,
        CONFIG_SHOW_COVERART,
        CONFIG_COVERART,
        CONFIG_COVERART_SIZE,
        CONFIG_COVERART_SCALE,
        CONFIG_SAVE_TREEVIEW,
        CONFIG_COLOR_BG,
        CONFIG_COLOR_FG,
        CONFIG_COLOR_BG_SEL,
        CONFIG_COLOR_FG_SEL,
        CONFIG_ICON_SIZE,
        CONFIG_FONT_SIZE,
        CONFIG_SORT_TREEVIEW,
        CONFIG_SEARCH_DELAY,
        CONFIG_FULLSEARCH_WAIT,
        CONFIG_HIDE_NAVIGATION,
        CONFIG_HIDE_SEARCH,
        CONFIG_HIDE_TOOLBAR
        );
}

static void
load_config_expanded_rows ()
{
    if (expanded_rows)
        g_slist_free (expanded_rows);
    expanded_rows = g_slist_alloc();

    gchar **config_expanded_rows;
    config_expanded_rows = g_strsplit (deadbeef->conf_get_str_fast (CONFSTR_FB_EXPANDED_ROWS,   ""), " ", 0);

    for (int i = 0; i < g_strv_length(config_expanded_rows); i++)
    {
        expanded_rows = g_slist_append (expanded_rows, g_strdup (config_expanded_rows[i]));
    }
    g_strfreev (config_expanded_rows);
}

static void
get_uris_from_selection (gpointer data, gpointer userdata)
{
    GtkTreeIter     iter;
    gchar           *uri;
    GtkTreePath     *path       = data;
    GList           *uri_list   = userdata;

    if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (treestore), &iter, path))
        return;

    gtk_tree_model_get (GTK_TREE_MODEL (treestore), &iter,
                    TREEBROWSER_COLUMN_URI, &uri, -1);
    uri_list = g_list_append (uri_list, g_strdup (uri));
    g_free (uri);
}

static void
update_rootdirs ()
{
    trace("update rootdirs\n");

#if !GTK_CHECK_VERSION(3,0,0)
    gtk_list_store_clear (GTK_LIST_STORE (gtk_combo_box_get_model (GTK_COMBO_BOX (addressbar))));
#else
    gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (addressbar));
#endif

    gchar **config_rootdirs;
    config_rootdirs = g_strsplit (CONFIG_DEFAULT_PATH, ";", 0);

    for (int i = 0; i < g_strv_length (config_rootdirs); i++)
    {
#if !GTK_CHECK_VERSION(3,0,0)
        gtk_combo_box_append_text (GTK_COMBO_BOX (addressbar), g_strdup (config_rootdirs[i]));
#else
        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (addressbar), NULL, g_strdup (config_rootdirs[i]));
#endif
    }
    g_strfreev (config_rootdirs);

    gtk_combo_box_set_active (GTK_COMBO_BOX (addressbar), 0);
}

static void
collapse_all ()
{
    trace("collapse all rows\n");

    GtkTreePath *path = NULL;

    GtkTreeIter iter;
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (treestore), &iter);

    path = gtk_tree_model_get_path (GTK_TREE_MODEL (treestore), &iter);
    while (tree_view_collapse_rows_recursive (GTK_TREE_MODEL (treestore), GTK_TREE_VIEW (treeview), path, 0))
        gtk_tree_path_next (path);
}

static void
expand_all ()
{
    trace("expand all rows\n");

    GtkTreePath *path = NULL;

    GtkTreeIter iter;
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (treestore), &iter);

    path = gtk_tree_model_get_path (GTK_TREE_MODEL (treestore), &iter);
    while (tree_view_expand_rows_recursive (GTK_TREE_MODEL (treestore), GTK_TREE_VIEW (treeview), path, 0))
        gtk_tree_path_next (path);
}


/*-----------------*/
/* SIGNAL HANDLERS */
/*-----------------*/


static int
handle_message (uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2)
{
    if (! CONFIG_ENABLED)
        return 0;

    if (id == DB_EV_CONFIGCHANGED)
        on_config_changed (ctx);

    return 0;
}

static int
on_config_changed (uintptr_t ctx)
{
    trace("signal: config changed\n");

    gboolean    enabled         = CONFIG_ENABLED;
    gboolean    show_hidden     = CONFIG_SHOW_HIDDEN_FILES;
    gboolean    filter_enabled  = CONFIG_FILTER_ENABLED;
    gboolean    filter_auto     = CONFIG_FILTER_AUTO;
    gboolean    show_bookmarks  = CONFIG_SHOW_BOOKMARKS;
    gboolean    show_icons      = CONFIG_SHOW_ICONS;
    gboolean    tree_lines      = CONFIG_SHOW_TREE_LINES;
    gint        width           = CONFIG_WIDTH;
    gboolean    show_coverart   = CONFIG_SHOW_COVERART;
    gint        coverart_size   = CONFIG_COVERART_SIZE;
    gboolean    coverart_scale  = CONFIG_COVERART_SCALE;
    gint        icon_size       = CONFIG_ICON_SIZE;
    gboolean    sort_treeview   = CONFIG_SORT_TREEVIEW;

    gchar *     default_path    = g_strdup (CONFIG_DEFAULT_PATH);
    gchar *     filter          = g_strdup (CONFIG_FILTER);
    gchar *     coverart        = g_strdup (CONFIG_COVERART);
    gchar *     bookmarks_file  = g_strdup (CONFIG_BOOKMARKS_FILE);
    gchar *     bgcolor         = g_strdup (CONFIG_COLOR_BG);
    gchar *     fgcolor         = g_strdup (CONFIG_COLOR_BG);
    gchar *     bgcolor_sel     = g_strdup (CONFIG_COLOR_BG_SEL);
    gchar *     fgcolor_sel     = g_strdup (CONFIG_COLOR_BG_SEL);

    gboolean do_update = FALSE;

    load_config ();

    if (enabled != CONFIG_ENABLED)
    {
        if (CONFIG_ENABLED)
            libbrowser_startup (NULL);
        else
            libbrowser_shutdown (NULL);
    }

    if (CONFIG_ENABLED)
    {
        if (CONFIG_HIDDEN)
            gtk_widget_hide (sidebar);
        else
            gtk_widget_show (sidebar);

        if (CONFIG_HIDE_NAVIGATION)
            gtk_widget_hide (sidebar_addressbox);
        else
            gtk_widget_show (sidebar_addressbox);

        if (CONFIG_HIDE_SEARCH)
            gtk_widget_hide (sidebar_searchbox);
        else
            gtk_widget_show (sidebar_searchbox);

        if (CONFIG_HIDE_TOOLBAR)
            gtk_widget_hide (sidebar_toolbar);
        else
            gtk_widget_show (sidebar_toolbar);

        if (width != CONFIG_WIDTH)
            gtk_widget_set_size_request (sidebar, CONFIG_WIDTH, -1);

        if ((show_hidden != CONFIG_SHOW_HIDDEN_FILES) ||
                (filter_enabled != CONFIG_FILTER_ENABLED) ||
                (filter_enabled && (filter_auto != CONFIG_FILTER_AUTO)) ||
                (show_bookmarks != CONFIG_SHOW_BOOKMARKS) ||
                (show_icons != CONFIG_SHOW_ICONS) ||
                (show_icons && (icon_size != CONFIG_ICON_SIZE)) ||
                (show_coverart != CONFIG_SHOW_COVERART) ||
                (show_coverart && (coverart_size != CONFIG_COVERART_SIZE)) ||
                (show_coverart && (coverart_scale != CONFIG_COVERART_SCALE)) ||
                (tree_lines != CONFIG_SHOW_TREE_LINES) ||
                (sort_treeview != CONFIG_SORT_TREEVIEW))
            do_update = TRUE;

        if (CONFIG_FILTER_ENABLED)
        {
            if (CONFIG_FILTER_AUTO)
            {
                gchar *autofilter = g_strdup (known_extensions);
                create_autofilter ();
                if (! utils_str_equal (autofilter, known_extensions))
                    do_update = TRUE;
                g_free (autofilter);
            }
            else
            {
                if (! utils_str_equal (filter, CONFIG_FILTER))
                    do_update = TRUE;
            }
        }

        if (! utils_str_equal (coverart, CONFIG_COVERART))
            do_update = TRUE;

        if (! utils_str_equal (bookmarks_file, CONFIG_BOOKMARKS_FILE))
            do_update = TRUE;

        if (! utils_str_equal (default_path, CONFIG_DEFAULT_PATH))
            do_update = TRUE;
    }

    g_free (default_path);
    g_free (filter);
    g_free (coverart);
    g_free (bookmarks_file);
    g_free (bgcolor);
    g_free (fgcolor);
    g_free (bgcolor_sel);
    g_free (fgcolor_sel);

    if (do_update)
    {
        treeview_update (NULL);
    }

    return 0;
}

void on_drag_data_get_helper (gpointer data, gpointer userdata)
{
    GtkTreeIter     iter;
    gchar           *uri, *enc_uri;
    GtkTreePath     *path       = data;
    GString         *uri_str    = userdata;

    if (! gtk_tree_model_get_iter (GTK_TREE_MODEL (treestore), &iter, path))
        return;

    gtk_tree_model_get (GTK_TREE_MODEL (treestore), &iter,
                    TREEBROWSER_COLUMN_URI, &uri, -1);

    // Encode Filename to URI - important!
    enc_uri = g_filename_to_uri (uri, NULL, NULL);

    if (uri_str->len > 0)
        uri_str = g_string_append_c (uri_str, ' ');
    uri_str = g_string_append (uri_str, enc_uri);

    g_free (uri);
}

static void
on_drag_data_get (GtkWidget *widget, GdkDragContext *drag_context,
                GtkSelectionData *sdata, guint info, guint time,
                gpointer user_data)
{
    GtkTreeSelection    *selection;
    GList               *rows;
    GString             *uri_str;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
    rows = gtk_tree_selection_get_selected_rows (selection, NULL);

    uri_str = g_string_new ("");
    g_list_foreach (rows, (GFunc) on_drag_data_get_helper, uri_str);
    g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
    g_list_free (rows);

    gchar *uri = g_string_free (uri_str, FALSE);

    trace("dnd send: %s\n", uri);

#if GTK_CHECK_VERSION(3,0,0)
    GdkAtom target = gtk_selection_data_get_target (sdata);
    gtk_selection_data_set (sdata, target, 8, (guchar*) uri, strlen (uri));
#else
    gtk_selection_data_set (sdata, sdata->target, 8, (guchar*) uri, strlen (uri));
#endif

    g_free (uri);
}


/*--------------------*/
/* INTERFACE HANDLING */
/*--------------------*/


static int
create_menu_entry (void)
{
    trace("create menu entry\n");

    mainmenuitem = gtk_check_menu_item_new_with_mnemonic (_("_Librarybrowser"));
    if (! mainmenuitem)
        return -1;

    GtkWidget *viewmenu = lookup_widget (gtkui_plugin->get_mainwin (), "View_menu");

    gtk_container_add (GTK_CONTAINER (viewmenu), mainmenuitem);
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (mainmenuitem), ! CONFIG_HIDDEN);

    gtk_widget_show (mainmenuitem);
    g_signal_connect (mainmenuitem, "activate", G_CALLBACK (on_mainmenu_toggle), NULL);

    return 0;
}

static int
create_interface (GtkWidget *cont)
{
    trace("create interface\n");

    create_sidebar ();
    if (! sidebar)
        return -1;

    // Deadbeef's new API allows clean adjusting of the interface
    if (cont)
    {
        gtk_container_add (GTK_CONTAINER (cont), sidebar);
        return 0;
    }

    gtk_widget_set_size_request (sidebar, CONFIG_WIDTH, -1);

    /* Deadbeef's main window structure is like this:
     * + mainwin
     *   + vbox1
     *     + menubar
     *     + hbox2 (toolbar)
     *       + hbox3 (toolbar buttons)
     *       + seekbar
     *       + volumebar
     *     + plugins_bottom_vbox (playlist & plugins)
     *     + statusbar
     */

    trace("modify interface\n");

    // Really dirty hack to include the sidebar in main GUI
    GtkWidget *mainbox  = lookup_widget (gtkui_plugin->get_mainwin (), "vbox1");
    GtkWidget *playlist = lookup_widget (gtkui_plugin->get_mainwin (), "plugins_bottom_vbox");
    GtkWidget* playlist_parent = gtk_widget_get_parent (playlist);

    if (playlist_parent != mainbox)
    {
        trace("interface has been altered already, will try to accomodate\n");

        // not sure if this hack is even more dirty than the normal one...
        GtkWidget* playlist_parent_parent = gtk_widget_get_parent (playlist_parent);

        g_object_ref (playlist_parent);  // prevent destruction of widget by removing from container
        gtk_container_remove (GTK_CONTAINER (playlist_parent_parent), playlist_parent);

        hbox_all = gtk_hpaned_new ();
        gtk_paned_pack1 (GTK_PANED (hbox_all), sidebar, FALSE, TRUE);
        gtk_paned_pack2 (GTK_PANED (hbox_all), playlist_parent, TRUE, TRUE);
        g_object_unref (playlist_parent);

        gtk_container_add (GTK_CONTAINER (mainbox), hbox_all);
        gtk_box_reorder_child (GTK_BOX (mainbox), hbox_all, 2);

        gtk_widget_show_all (hbox_all);
        gtkui_update_listview_headers ();
    }
    else
    {
        g_object_ref (playlist);  // prevent destruction of widget by removing from container
        gtk_container_remove (GTK_CONTAINER (mainbox), playlist);

        hbox_all = gtk_hpaned_new ();
        gtk_paned_pack1 (GTK_PANED (hbox_all), sidebar, FALSE, TRUE);
        gtk_paned_pack2 (GTK_PANED (hbox_all), playlist, TRUE, TRUE);

        g_object_unref (playlist);

        gtk_container_add (GTK_CONTAINER (mainbox), hbox_all);
        gtk_box_reorder_child (GTK_BOX (mainbox), hbox_all, 2);

        gtk_widget_show_all (hbox_all);
        gtkui_update_listview_headers ();
    }

    return 0;
}

static int
restore_interface (GtkWidget *cont)
{
    trace("restore interface\n");

    if (! sidebar)
        return 0;

    // Deadbeef's new API allows clean adjusting of the interface
    if (cont)
    {
        gtk_container_remove (GTK_CONTAINER (cont), sidebar);
        sidebar = NULL;
        return 0;
    }

    // save current width of sidebar
    if (CONFIG_ENABLED && ! CONFIG_HIDDEN)
    {
        GtkAllocation alloc;
        gtk_widget_get_allocation (sidebar, &alloc);
        CONFIG_WIDTH = alloc.width;
    }

    trace("remove sidebar\n");

    if (! sidebar)
        return -1;

    trace("modify interface\n");

    // Really dirty hack to include the sidebar in main GUI
    GtkWidget *mainbox  = lookup_widget (gtkui_plugin->get_mainwin (), "vbox1");
    GtkWidget *playlist = lookup_widget (gtkui_plugin->get_mainwin (), "plugins_bottom_vbox");

    gtk_widget_hide (mainbox);

    g_object_ref (playlist);  // prevent destruction of widget by removing from container
    gtk_container_remove (GTK_CONTAINER (vbox_playlist), playlist);
    gtk_box_pack_start (GTK_BOX (mainbox), playlist, TRUE, TRUE, 0);
    gtk_box_reorder_child (GTK_BOX (mainbox), playlist, 2);
    g_object_unref (playlist);

    gtk_container_remove (GTK_CONTAINER (hbox_all), sidebar);
    gtk_container_remove (GTK_CONTAINER (hbox_all), vbox_playlist);
    gtk_container_remove (GTK_CONTAINER (mainbox), hbox_all);

    gtk_widget_show_all (mainbox);
    gtkui_update_listview_headers ();

    sidebar = NULL;

    return 0;
}

static GtkWidget*
create_popup_menu (GtkTreePath *path, gchar *name, GList *uri_list)
{
    trace("create popup menu\n");

    GtkWidget *menu     = gtk_menu_new ();
    GtkWidget *plmenu   = gtk_menu_new ();  // submenu for playlists
    GtkWidget *item;

    gchar *uri = "";
    if (uri_list && uri_list->next)
        uri = g_strdup (uri_list->next->data);  // first "real" item in list

    gint num_items      = g_list_length (uri_list) - 1;  // first item is always NULL
    gboolean is_exists  = FALSE;
    gboolean is_dir     = FALSE;
    if (num_items == 1)
    {
        is_exists   = g_file_test (uri, G_FILE_TEST_EXISTS);
        is_dir      = is_exists && g_file_test (uri, G_FILE_TEST_IS_DIR);
    }
    else if (num_items > 1)
    {
        is_exists = TRUE;
        GList *node;
        for (node = uri_list->next; node; node = node->next)
        {
            is_exists = is_exists && g_file_test (node->data, G_FILE_TEST_EXISTS);
        }
    }

    item = gtk_menu_item_new_with_mnemonic (_("_Add to current playlist"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_add_current), uri_list);
    gtk_widget_set_sensitive (item, is_exists);

    item = gtk_menu_item_new_with_mnemonic (_("_Replace current playlist"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_replace_current), uri_list);
    gtk_widget_set_sensitive (item, is_exists);

    item = gtk_menu_item_new_with_mnemonic (_("Add to _new playlist"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_add_new), uri_list);
    gtk_widget_set_sensitive (item, is_exists);

    if (is_exists)
    {
        gchar plt_title[32];
        ddb_playlist_t *plt;
        gchar *label;

        deadbeef->pl_lock ();
        for (int i = 0; i < deadbeef->plt_get_count (); i++)
        {
            plt = deadbeef->plt_get_for_idx (i);
            deadbeef->plt_get_title (plt, plt_title, 32);

            label = g_strdup_printf ("%s%d: %s",
                            i < 9 ? "_" : "",   // playlists 1..9 with mnemonic
                            i+1, plt_title);
            item = gtk_menu_item_new_with_mnemonic (label);
            g_free (label);

            gtk_container_add (GTK_CONTAINER (plmenu), item);
            g_signal_connect (item, "activate", G_CALLBACK (on_menu_add), uri_list);
        }
        deadbeef->pl_unlock ();
    }

    item = gtk_menu_item_new_with_mnemonic (_("Add to _playlist ..."));
    gtk_container_add (GTK_CONTAINER (menu), item);
    gtk_widget_set_sensitive (item, is_exists);
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), plmenu);

    item = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("Enter _directory"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_enter_directory), uri);
    gtk_widget_set_sensitive (item, is_dir);

    item = gtk_menu_item_new_with_mnemonic (_("Go _up"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_go_up), NULL);

    item = gtk_menu_item_new_with_mnemonic (_("Re_fresh"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_refresh), NULL);

    item = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("Cop_y path to clipboard"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_copy_uri), uri_list);
    gtk_widget_set_sensitive (item, is_exists);

#if GTK_CHECK_VERSION(3,16,0)
    // new rename dialog (uses GTK3)
    item = gtk_menu_item_new_with_mnemonic (_("Rena_me file or directory"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_rename), uri_list);
    gtk_widget_set_sensitive (item, is_exists && (num_items == 1));  // can only rename one file at once
#endif

    item = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("Expand (one _level)"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_expand_one), path);

    item = gtk_menu_item_new_with_mnemonic (_("E_xpand (all levels)"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_expand_all), path);

    item = gtk_menu_item_new_with_mnemonic (_("_Collapse"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_collapse_all), path);

    item = gtk_menu_item_new_with_mnemonic (_("Expand ent_ire tree (one level)"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_expand_all), NULL);

    item = gtk_menu_item_new_with_mnemonic (_("C_ollapse entire tree"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_collapse_all), NULL);

    item = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), item);

    item = gtk_check_menu_item_new_with_mnemonic (_("Show _bookmarks"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), CONFIG_SHOW_BOOKMARKS);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_show_bookmarks), NULL);

    item = gtk_check_menu_item_new_with_mnemonic (_("Show _hidden files"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), CONFIG_SHOW_HIDDEN_FILES);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_show_hidden_files), NULL);

    item = gtk_check_menu_item_new_with_mnemonic (_("Filter files by _extension"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), CONFIG_FILTER_ENABLED);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_use_filter), NULL);

    item = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), item);

    item = gtk_check_menu_item_new_with_mnemonic (_("Hide _search bar"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), CONFIG_HIDE_SEARCH);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_hide_search), NULL);

    item = gtk_check_menu_item_new_with_mnemonic (_("Hide na_vigation bar"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), CONFIG_HIDE_NAVIGATION);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_hide_navigation), NULL);

    item = gtk_check_menu_item_new_with_mnemonic (_("Hide _toolbar"));
    gtk_container_add (GTK_CONTAINER (menu), item);
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), CONFIG_HIDE_TOOLBAR);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_hide_toolbar), NULL);

#if GTK_CHECK_VERSION(3,16,0)
    // new settings dialog (uses GTK3)
    item = gtk_separator_menu_item_new ();
    gtk_container_add (GTK_CONTAINER (menu), item);

    item = gtk_menu_item_new_with_mnemonic (_("Confi_gure ..."));
    gtk_container_add (GTK_CONTAINER (menu), item);
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_config), NULL);
#endif

    gtk_widget_show_all (menu);

    return menu;
}

static GtkWidget *
create_view_and_model (void)
{
    trace("create view and model\n");

    GtkWidget *view                 = gtk_tree_view_new ();
    GtkCellRenderer *render_icon    = gtk_cell_renderer_pixbuf_new ();
    GtkCellRenderer *render_text    = gtk_cell_renderer_text_new ();

    treeview_column_icon            = gtk_tree_view_column_new ();
    treeview_column_text            = gtk_tree_view_column_new ();

    gtk_widget_set_name (view, "deadbeef_libbrowser_treeview");

    gtk_widget_set_events (view, GDK_EXPOSURE_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), FALSE);

    // TREEBROWSER_COLUMN_ICON
    gtk_tree_view_append_column (GTK_TREE_VIEW (view), treeview_column_icon);
    gtk_tree_view_column_pack_start (treeview_column_icon, render_icon, TRUE);
    //gtk_tree_view_column_pack_start (treeview_column_icon, render_dummy, FALSE);
    gtk_tree_view_column_set_attributes (treeview_column_icon, render_icon,
                    "pixbuf", TREEBROWSER_RENDER_ICON, NULL);
    gtk_tree_view_column_set_spacing (treeview_column_icon, 0);
    gtk_tree_view_column_set_sizing (treeview_column_icon, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

    // TREEBROWSER_COLUMN_NAME
    gtk_tree_view_append_column (GTK_TREE_VIEW (view), treeview_column_text);
    gtk_tree_view_column_pack_start (treeview_column_text, render_text, TRUE);
    gtk_tree_view_column_add_attribute (treeview_column_text, render_text,
                    "text", TREEBROWSER_RENDER_TEXT);
    gtk_tree_view_column_set_spacing (treeview_column_text, 0);
    gtk_tree_view_column_set_sizing (treeview_column_text, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

#if GTK_CHECK_VERSION(2, 18, 0)
    gtk_cell_renderer_set_alignment (render_icon, 0, 0.5);  // left-middle
    gtk_cell_renderer_set_alignment (render_text, 0, 0.5);  // left-middle
    gtk_cell_renderer_set_padding (render_text, 4, 0);
#endif

    if (CONFIG_FONT_SIZE > 0)
        g_object_set (render_text, "size", CONFIG_FONT_SIZE*1024, NULL);

    gtk_tree_view_set_enable_search (GTK_TREE_VIEW (view), TRUE);
    gtk_tree_view_set_expander_column (GTK_TREE_VIEW (view), TREEBROWSER_COLUMN_ICON);
    gtk_tree_view_set_search_column (GTK_TREE_VIEW (view), TREEBROWSER_COLUMN_NAME);

    //gtk_tree_view_set_rubber_banding (GTK_TREE_VIEW (view), TRUE);
    gtk_tree_view_set_show_expanders (GTK_TREE_VIEW (view), TRUE);
    //gtk_tree_view_set_level_indentation (GTK_TREE_VIEW (view), 16);

    gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (view), treeview_separator_func,
                    NULL, NULL);
    gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (view)),
                    GTK_SELECTION_MULTIPLE);

#if GTK_CHECK_VERSION(2, 10, 0)
    g_object_set (view, "has-tooltip", TRUE, "tooltip-column", TREEBROWSER_COLUMN_TOOLTIP, NULL);
    gtk_tree_view_set_enable_tree_lines (GTK_TREE_VIEW (view), CONFIG_SHOW_TREE_LINES);
#endif

    treestore = gtk_tree_store_new (TREEBROWSER_COLUMNC,
                    GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    gtk_tree_view_set_model (GTK_TREE_VIEW (view), GTK_TREE_MODEL (treestore));

    return view;
}

static void
create_sidebar (void)
{
    trace("create sidebar\n");

    GtkWidget           *scrollwin;
    GtkWidget           *wid, *button_go;
#if !GTK_CHECK_VERSION(3,6,0)
    GtkWidget           *button_clear;
#endif
    GtkTreeSelection    *selection;

    treeview            = create_view_and_model ();
    scrollwin           = gtk_scrolled_window_new (NULL, NULL);
#if !GTK_CHECK_VERSION(3,0,0)
    sidebar             = gtk_vbox_new (FALSE, 0);
    sidebar_searchbox   = gtk_hbox_new (FALSE, 0);
    sidebar_addressbox  = gtk_hbox_new (FALSE, 0);
    addressbar          = gtk_combo_box_text_new_with_entry ();
#else
    sidebar             = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    sidebar_searchbox   = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    sidebar_addressbox  = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    addressbar          = gtk_combo_box_text_new_with_entry ();
#endif
    sidebar_toolbar     = gtk_toolbar_new ();
#if !GTK_CHECK_VERSION(3,6,0)
    searchbar           = gtk_entry_new ();
    button_clear        = gtk_button_new_with_label (_("Clear"));
#else
    searchbar           = gtk_search_entry_new ();
#endif
    button_go           = gtk_button_new_with_label (_("Go!"));

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));

    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrollwin),
                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    gtk_toolbar_set_icon_size (GTK_TOOLBAR (sidebar_toolbar), GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_toolbar_set_style (GTK_TOOLBAR (sidebar_toolbar), GTK_TOOLBAR_ICONS);

    wid = GTK_WIDGET (gtk_tool_button_new (NULL, ""));
    gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (wid), "gtk-go-up");
    gtk_tool_item_set_tooltip_text (GTK_TOOL_ITEM (wid), _("Go to parent directory"));
    g_signal_connect (wid, "clicked", G_CALLBACK (on_button_go_up), NULL);
    gtk_container_add (GTK_CONTAINER (sidebar_toolbar), wid);

    wid = GTK_WIDGET (gtk_tool_button_new (NULL, ""));
    gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (wid), "gtk-refresh");
    gtk_tool_item_set_tooltip_text (GTK_TOOL_ITEM (wid), _("Refresh current directory"));
    g_signal_connect (wid, "clicked", G_CALLBACK (on_button_refresh), NULL);
    gtk_container_add (GTK_CONTAINER (sidebar_toolbar), wid);

    wid = GTK_WIDGET (gtk_tool_button_new (NULL, ""));
    gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (wid), "gtk-home");
    gtk_tool_item_set_tooltip_text (GTK_TOOL_ITEM (wid), _("Go to home directory"));
    g_signal_connect (wid, "clicked", G_CALLBACK (on_button_go_home), NULL);
    gtk_container_add (GTK_CONTAINER (sidebar_toolbar), wid);

    wid = GTK_WIDGET (gtk_tool_button_new (NULL, ""));
    gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (wid), "gtk-clear");
    gtk_tool_item_set_tooltip_text (GTK_TOOL_ITEM (wid), _("Go to default directory"));
    g_signal_connect (wid, "clicked", G_CALLBACK (on_button_go_default), NULL);
    gtk_container_add (GTK_CONTAINER (sidebar_toolbar), wid);

    wid = GTK_WIDGET (gtk_tool_item_new ());
    gtk_tool_item_set_expand (GTK_TOOL_ITEM (wid), TRUE);
    gtk_container_add (GTK_CONTAINER (sidebar_toolbar), wid);

    wid = GTK_WIDGET (gtk_tool_button_new (NULL, ""));
    gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (wid), "gtk-apply");
    gtk_tool_item_set_tooltip_text (GTK_TOOL_ITEM (wid), _("Replace current playlist with selection"));
    g_signal_connect (wid, "clicked", G_CALLBACK (on_button_replace_current), NULL);
    gtk_container_add (GTK_CONTAINER (sidebar_toolbar), wid);
    gtk_widget_set_sensitive (wid, FALSE);
    toolbar_button_replace = wid;

    wid = GTK_WIDGET (gtk_tool_button_new (NULL, ""));
    gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (wid), "gtk-add");
    gtk_tool_item_set_tooltip_text (GTK_TOOL_ITEM (wid), _("Add selection to current playlist"));
    g_signal_connect (wid, "clicked", G_CALLBACK (on_button_add_current), NULL);
    gtk_container_add (GTK_CONTAINER (sidebar_toolbar), wid);
    gtk_widget_set_sensitive (wid, FALSE);
    toolbar_button_add = wid;

    gtk_container_add (GTK_CONTAINER (scrollwin), treeview);

    gtk_box_pack_start (GTK_BOX (sidebar_addressbox), addressbar, TRUE, TRUE, 1);
    gtk_box_pack_start (GTK_BOX (sidebar_addressbox), button_go,  FALSE, TRUE, 0);

    gtk_box_pack_start (GTK_BOX (sidebar_searchbox), searchbar, TRUE, TRUE, 1);
#if !GTK_CHECK_VERSION(3,6,0)
    gtk_box_pack_start (GTK_BOX (sidebar_searchbox), button_clear,  FALSE, TRUE, 0);
#endif

    gtk_box_pack_start (GTK_BOX (sidebar), sidebar_searchbox, FALSE, TRUE, 1);
    gtk_box_pack_start (GTK_BOX (sidebar), sidebar_addressbox,  FALSE, TRUE, 1);
    gtk_box_pack_start (GTK_BOX (sidebar), sidebar_toolbar,  FALSE, TRUE, 1);
    gtk_box_pack_start (GTK_BOX (sidebar), scrollwin, TRUE, TRUE, 1);

    // adjust tab-focus
    /* GList *focus_list = NULL; */
    /* focus_list = g_list_append (focus_list, sidebar_searchbox); */
    /* focus_list = g_list_append (focus_list, sidebar_addressbox); */
    /* focus_list = g_list_append (focus_list, sidebar_toolbar); */
    /* gtk_container_set_focus_chain (GTK_CONTAINER (sidebar), focus_list); */
    /* g_list_free (focus_list); */

    g_signal_connect (selection,    "changed",              G_CALLBACK (on_treeview_changed),               NULL);
    g_signal_connect (treeview,     "button-press-event",   G_CALLBACK (on_treeview_mouseclick_press),      selection);
    g_signal_connect (treeview,     "button-release-event", G_CALLBACK (on_treeview_mouseclick_release),    selection);
    g_signal_connect (treeview,     "motion-notify-event",  G_CALLBACK (on_treeview_mousemove),             NULL);
    g_signal_connect (treeview,     "key-press-event",      G_CALLBACK (on_treeview_key_press),             selection);
    g_signal_connect (treeview,     "row-collapsed",        G_CALLBACK (on_treeview_row_collapsed),         NULL);
    g_signal_connect (treeview,     "row-expanded",         G_CALLBACK (on_treeview_row_expanded),          NULL);
    g_signal_connect (button_go,    "clicked",              G_CALLBACK (on_addressbar_changed),             NULL);
#if !GTK_CHECK_VERSION(3,6,0)
    g_signal_connect (searchbar,    "changed",              G_CALLBACK (on_searchbar_changed),              NULL);
    g_signal_connect (button_clear, "clicked",              G_CALLBACK (on_searchbar_cleared),              NULL);
#else
    g_signal_connect (searchbar,    "search-changed",       G_CALLBACK (on_searchbar_changed),              NULL);
#endif

    gtk_widget_show_all (sidebar);

    if (CONFIG_HIDDEN)
        gtk_widget_hide (sidebar);

    if (CONFIG_HIDE_NAVIGATION)
        gtk_widget_hide (sidebar_addressbox);

    if (CONFIG_HIDE_SEARCH)
        gtk_widget_hide (sidebar_searchbox);

    if (CONFIG_HIDE_TOOLBAR)
        gtk_widget_hide (sidebar_toolbar);
}

#if GTK_CHECK_VERSION(3,16,0)
static void settings_update_paths (GtkGrid *grid, gchar *config_paths);
static gchar* settings_get_paths(GtkGrid *grid);

static void
on_settings_radio_filter_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
    gboolean enabled = gtk_toggle_button_get_active (togglebutton);
    gtk_widget_set_sensitive (GTK_WIDGET (user_data), enabled);
}

static void
on_settings_path_remove (GtkButton *button, gpointer label)
{
    GtkWidget *grid;
    const gchar *path, *dirname;

    grid = gtk_widget_get_parent (GTK_WIDGET (label));
    path = gtk_label_get_text (GTK_LABEL (label));

    int numrows = gtk_grid_get_number_of_rows (GTK_GRID (grid), 0);  // get rows with labels
    for (int row = 0; row < numrows; row++)
    {
        label = gtk_grid_get_child_at (GTK_GRID (grid), 0, row);
        if (label == NULL)
            break;

        dirname = gtk_label_get_text (GTK_LABEL (label));

        if (! g_ascii_strncasecmp (path, dirname, 256))
        {
            gtk_grid_remove_row (GTK_GRID (grid), row);
            gtk_widget_show_all (GTK_WIDGET (grid));
        }
    }
}

static void
on_settings_path_add (GtkButton *button, gpointer grid)
{
    GtkWidget *dialog;
    GtkWidget *label, *button_remove;
    gchar *dirname;

    dialog = gtk_file_chooser_dialog_new (
                "Add Directory",
                GTK_WINDOW (gtkui_plugin->get_mainwin ()),
                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                _("_Cancel"), GTK_RESPONSE_CANCEL,
                _("_Open"), GTK_RESPONSE_ACCEPT,
                NULL);

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
        dirname = gtk_file_chooser_get_filename (chooser);

        int row = gtk_grid_get_number_of_rows (GTK_GRID (grid), 0);  // get rows with labels
        gtk_grid_insert_row (GTK_GRID (grid), row);

        label = gtk_label_new (dirname);
#if GTK_CHECK_VERSION(3,16,0)
        gtk_label_set_xalign (GTK_LABEL (label), 0.);
#endif
        gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
        gtk_widget_set_hexpand (label, TRUE);

        button_remove = GTK_WIDGET (gtk_tool_button_new (NULL, "DEL"));
        gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (button_remove), "gtk-delete");
        gtk_grid_attach (GTK_GRID (grid), button_remove, 1, row, 1, 1);
        g_signal_connect (button, "clicked", G_CALLBACK (on_settings_path_remove), label);

        gtk_widget_show_all (GTK_WIDGET (grid));
    }

    gtk_widget_destroy (dialog);
}

static gchar *
settings_get_paths(GtkGrid *grid)
{
    GtkWidget *label;
    gchar *paths, *newpaths;
    const gchar *dirname;

    paths = NULL;

    int numrows = gtk_grid_get_number_of_rows (GTK_GRID (grid), 0);  // get rows with labels
    for (int row = 0; row < numrows; row++)
    {
        label = gtk_grid_get_child_at (grid, 0, row);
        if (label == NULL)
            break;

        dirname = gtk_label_get_text (GTK_LABEL (label));

        if (paths == NULL)
            paths = g_strdup (dirname);
        else
        {
            newpaths = g_strconcat (paths, ";", dirname, NULL);
            g_free (paths);
            paths = newpaths;
        }
    }

    return paths;
}

static void
settings_update_paths (GtkGrid *grid, gchar *config_paths)
{
    // clean the grid
    GList *children, *iter;
    children = gtk_container_get_children (GTK_CONTAINER (grid));
    for (iter = children; iter != NULL; iter = g_list_next (iter))
        gtk_widget_destroy (GTK_WIDGET (iter->data));
    g_list_free (children);

    GtkWidget *label, *button;
    gchar **path_list;
    path_list = g_strsplit (config_paths, ";", 0);

    int row;
    for (row = 0; row < g_strv_length (path_list); row++)
    {
        if (strlen (path_list[row]) == 0)
            continue;

        label = gtk_label_new (path_list[row]);
#if GTK_CHECK_VERSION(3,16,0)
        gtk_label_set_xalign (GTK_LABEL (label), 0.);
#endif
        gtk_grid_attach (grid, label, 0, row, 1, 1);
        gtk_widget_set_hexpand (label, TRUE);

        button = GTK_WIDGET (gtk_tool_button_new (NULL, "DEL"));
        gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (button), "gtk-delete");
        gtk_grid_attach (grid, button, 1, row, 1, 1);
        g_signal_connect (button, "clicked", G_CALLBACK (on_settings_path_remove), label);
        gtk_widget_set_tooltip_text (button, _("Remove this path from the list."));
    }
    g_strfreev (path_list);

    button = GTK_WIDGET (gtk_tool_button_new (NULL, "ADD"));
    gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (button), "gtk-add");
    gtk_grid_attach (grid, button, 1, row+1, 1, 1);
    g_signal_connect (button, "clicked", G_CALLBACK (on_settings_path_add), grid);
    gtk_widget_set_tooltip_text (button, _("Add another path to the list. Paths listed here will be added to the navigation/address bar drop-down list, and the first path in the list will be used on startup."));

    gtk_widget_show_all (GTK_WIDGET (grid));

    g_free (config_paths);
}

static void
create_settings_dialog ()
{
    save_config ();  // make sure current settings (e.g. changes via popup menu) are saved

    GtkWidget *settings = gtk_dialog_new_with_buttons (
            _("Library Browser Plugin Settings"),
            GTK_WINDOW (gtkui_plugin->get_mainwin ()),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            _("_Apply"), GTK_RESPONSE_APPLY,
            _("_Cancel"), GTK_RESPONSE_CANCEL,
            _("_OK"), GTK_RESPONSE_OK,
            NULL);

    gtk_window_set_default_size (GTK_WINDOW (settings), 400, 300);

    GtkWidget *content              = gtk_dialog_get_content_area (GTK_DIALOG (settings));
    GtkWidget *notebook             = gtk_notebook_new ();
    GtkWidget *page1                = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *page2                = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *page3                = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *page4                = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *page5                = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);

    GtkWidget *frame_plugin         = gtk_frame_new (_(" Plugin  "));
    GtkWidget *grid_plugin          = gtk_grid_new ();
    GtkWidget *check_enabled        = gtk_check_button_new_with_mnemonic (_("Enable libbrowser _plugin"));
    GtkWidget *check_save_view      = gtk_check_button_new_with_mnemonic (_("_Save tree state (restore expanded/collapsed rows)"));

    GtkWidget *frame_layout         = gtk_frame_new (_(" Layout  "));
    GtkWidget *grid_layout          = gtk_grid_new ();
    GtkWidget *check_hidden         = gtk_check_button_new_with_mnemonic (_("_Hide everything"));
    GtkWidget *check_hide_nav       = gtk_check_button_new_with_mnemonic (_("Hide _navigation area"));
    GtkWidget *check_hide_search    = gtk_check_button_new_with_mnemonic (_("Hide _search bar"));
    GtkWidget *check_hide_tools     = gtk_check_button_new_with_mnemonic (_("Hide _toolbar"));

    GtkWidget *frame_paths          = gtk_frame_new (_(" Default paths  "));
    GtkWidget *grid_paths           = gtk_grid_new ();

    GtkWidget *frame_search         = gtk_frame_new (_(" Search  "));
    GtkWidget *grid_search          = gtk_grid_new ();
    GtkWidget *lbl_search_delay     = gtk_label_new (_("Delay search while typing (millisec):  "));
    GtkWidget *spin_search_delay    = gtk_spin_button_new_with_range (100, 2000, 100);
    GtkWidget *lbl_fullsearch_wait  = gtk_label_new (_("Expand full tree after typing N characters:  "));
    GtkWidget *spin_fullsearch_wait = gtk_spin_button_new_with_range (1, 10, 1);

    GtkWidget *frame_filter         = gtk_frame_new (_(" Shown files  "));
    GtkWidget *grid_filter          = gtk_grid_new ();
    GtkWidget *check_show_hidden    = gtk_check_button_new_with_mnemonic (_("Show _hidden files"));
    GtkWidget *check_filter_enabled = gtk_check_button_new_with_mnemonic (_("_Filter files by extension"));
    GtkWidget *radio_filter_auto    = gtk_radio_button_new_with_mnemonic (NULL, _("Use _automatic filtering based on plugins"));
    GtkWidget *radio_filter_custom  = gtk_radio_button_new_with_mnemonic (NULL, _("Use _custom list of filetypes"));
    GtkWidget *lbl_filter           = gtk_label_new (_("Allowed file types:  "));
    GtkWidget *entry_filter         = gtk_entry_new ();

    GtkWidget *frame_bookmarks      = gtk_frame_new (_(" Bookmarks  "));
    GtkWidget *grid_bookmarks       = gtk_grid_new ();
    GtkWidget *check_show_bookmarks = gtk_check_button_new_with_mnemonic (_("Show GTK _bookmarks"));
    GtkWidget *lbl_bookmarks_file   = gtk_label_new (_("Custom bookmarks file:  "));
    GtkWidget *entry_bookmarks_file = gtk_entry_new ();

    GtkWidget *frame_icons          = gtk_frame_new (_(" Folder icons  "));
    GtkWidget *grid_icons           = gtk_grid_new ();
    GtkWidget *check_show_icons     = gtk_check_button_new_with_mnemonic (_("Show folder/file _icons"));
    GtkWidget *lbl_icon_size        = gtk_label_new (_("Icon size:  "));
    GtkWidget *spin_icon_size       = gtk_spin_button_new_with_range (24, 48, 2);

    GtkWidget *frame_coverart       = gtk_frame_new (_(" Coverart  "));
    GtkWidget *grid_coverart        = gtk_grid_new ();
    GtkWidget *check_show_coverart  = gtk_check_button_new_with_mnemonic (_("Show _coverart icons"));
    GtkWidget *lbl_coverart         = gtk_label_new (_("Coverart images:  "));
    GtkWidget *entry_coverart       = gtk_entry_new ();
    GtkWidget *lbl_coverart_size    = gtk_label_new (_("Coverart size:  "));
    GtkWidget *spin_coverart_size   = gtk_spin_button_new_with_range (24, 48, 2);
    GtkWidget *check_coverart_scale = gtk_check_button_new_with_mnemonic (_("_Scale coverart to fixed width (make icons square)"));

    GtkWidget *frame_tree            = gtk_frame_new (_(" Tree view  "));
    GtkWidget *grid_tree             = gtk_grid_new ();
    GtkWidget *check_sort_tree       = gtk_check_button_new_with_mnemonic (_("Sort tree by _name"));
    GtkWidget *check_show_treelines  = gtk_check_button_new_with_mnemonic (_("Show tree_lines"));

    GtkWidget *frame_colors          = gtk_frame_new (_(" Font & Colors  "));
    GtkWidget *grid_colors           = gtk_grid_new ();
    GtkWidget *lbl_font_size         = gtk_label_new (_("Font size (0=default):  "));
    GtkWidget *spin_font_size        = gtk_spin_button_new_with_range (0, 24, 1);
    GtkWidget *lbl_color_bg          = gtk_label_new (_("Background color (normal):  "));
    GtkWidget *button_color_bg       = gtk_color_button_new ();
    GtkWidget *lbl_color_fg          = gtk_label_new (_("Foreground color (normal):  "));
    GtkWidget *button_color_fg       = gtk_color_button_new ();
    GtkWidget *lbl_color_bg_sel      = gtk_label_new (_("Background color (selected):  "));
    GtkWidget *button_color_bg_sel   = gtk_color_button_new ();
    GtkWidget *lbl_color_fg_sel      = gtk_label_new (_("Foreground color (selected):  "));
    GtkWidget *button_color_fg_sel   = gtk_color_button_new ();

    gtk_widget_set_tooltip_text (check_enabled,          _("If disabled, the plugin will not be added to the player's interface."));
    gtk_widget_set_tooltip_text (check_save_view,        _("Save expanded paths and restore them whenever the path is visible in the treeview."));
    gtk_widget_set_tooltip_text (check_hidden,           _("Hide the complete sidebar with the treeview."));
    gtk_widget_set_tooltip_text (check_hide_nav,         _("Hide the navigation/address bar above the treeview."));
    gtk_widget_set_tooltip_text (check_hide_search,      _("Hide the search bar above the treeview."));
    gtk_widget_set_tooltip_text (check_hide_tools,       _("Hide the toolbar above the treeview."));
    gtk_widget_set_tooltip_text (spin_search_delay,      _("When typing inside the search bar, the treeview will not be updated until this time has passed to increase performance."));
    gtk_widget_set_tooltip_text (spin_fullsearch_wait,   _("When typing inside the search bar, the tree will be fully expanded (and searched recursively) if enough characters have been entered."));
    gtk_widget_set_tooltip_text (check_show_hidden,      _("Show hidden files (filenames starting with a dot) in the treeview."));
    gtk_widget_set_tooltip_text (check_filter_enabled,   _("Show only files with matching extension (e.g. mp3) in the treeview."));
    gtk_widget_set_tooltip_text (radio_filter_auto,      _("Use an automatic list of file extensions that is based on the active plugins (so only playable files will be shown)."));
    gtk_widget_set_tooltip_text (radio_filter_custom,    _("Use a custom list of file extensions. Most users would want to use the auto-filter instead."));
    gtk_widget_set_tooltip_text (entry_filter,           _("Enter a list of file extensions (separated by semicolon) to be shown in the treeview."));
    gtk_widget_set_tooltip_text (check_show_bookmarks,   _("Show GTK bookmarks on top of the treeview. These bookmarks are shared with other applications, e.g. file browsers."));
    gtk_widget_set_tooltip_text (entry_bookmarks_file,   _("Show user-defined bookmarks on top of the treeview. These bookmarks are stored in the given file, and are not shared with other applications by default."));
    gtk_widget_set_tooltip_text (check_show_icons,       _("Show folder/file icons in the treeview. This applies to icons in general, including coverart."));
    gtk_widget_set_tooltip_text (spin_icon_size,         _("Set the size for folder/file icons. Note that coverart icons can be set to a different size."));
    gtk_widget_set_tooltip_text (check_show_coverart,     _("Show coverart icons in the treeview. Default icons are used if this feature is disabled."));
    gtk_widget_set_tooltip_text (entry_coverart,         _("Enter a list of coverart images to search (separated by semicolon). The first matching file that is found inside a directory will be used as coverart icon."));
    gtk_widget_set_tooltip_text (spin_coverart_size,     _("Set the size for coverart icons."));
    gtk_widget_set_tooltip_text (check_coverart_scale,   _("If enabled, coverart icons will be scaled and padded if necessary to generate a square icon. If disabled, coverart icons can be wider than normal if the original image is non-square."));
    gtk_widget_set_tooltip_text (check_sort_tree,        _("Sort treeview contents by name. If disabled, contents will be sorted by modification date (recently-changed files on top)."));
    gtk_widget_set_tooltip_text (check_show_treelines,   _("Show lines in the treeview to indicate different levels of subdirectories."));
    gtk_widget_set_tooltip_text (spin_font_size,         _("Set the font size to be used in the treeview. May need a restart of the player."));
    gtk_widget_set_tooltip_text (button_color_bg,        _("Set the background color to be used in the treeview. May need a restart of the player."));
    gtk_widget_set_tooltip_text (button_color_fg,        _("Set the foregound/text color to be used in the treeview. May need a restart of the player."));
    gtk_widget_set_tooltip_text (button_color_bg_sel,    _("Set the background color of selected paths to be used in the treeview. May need a restart of the player."));
    gtk_widget_set_tooltip_text (button_color_fg_sel,    _("Set the foregound/text color of selected paths to be used in the treeview. May need a restart of the player. Does not seem to work with GTK 3."));

    gtk_container_set_border_width (GTK_CONTAINER (page1), 8);
    gtk_container_set_border_width (GTK_CONTAINER (page2), 8);
    gtk_container_set_border_width (GTK_CONTAINER (page3), 8);
    gtk_container_set_border_width (GTK_CONTAINER (page4), 8);
    gtk_container_set_border_width (GTK_CONTAINER (page5), 8);

    gtk_container_set_border_width (GTK_CONTAINER (grid_plugin),    8);
    gtk_container_set_border_width (GTK_CONTAINER (grid_layout),    8);
    gtk_container_set_border_width (GTK_CONTAINER (grid_paths),     8);
    gtk_container_set_border_width (GTK_CONTAINER (grid_search),    8);
    gtk_container_set_border_width (GTK_CONTAINER (grid_filter),    8);
    gtk_container_set_border_width (GTK_CONTAINER (grid_bookmarks), 8);
    gtk_container_set_border_width (GTK_CONTAINER (grid_icons),     8);
    gtk_container_set_border_width (GTK_CONTAINER (grid_coverart),  8);
    gtk_container_set_border_width (GTK_CONTAINER (grid_tree),      8);
    gtk_container_set_border_width (GTK_CONTAINER (grid_colors),    8);

    gtk_grid_set_row_spacing (GTK_GRID (grid_plugin),    2);
    gtk_grid_set_row_spacing (GTK_GRID (grid_layout),    2);
    gtk_grid_set_row_spacing (GTK_GRID (grid_paths),     0);
    gtk_grid_set_row_spacing (GTK_GRID (grid_search),    2);
    gtk_grid_set_row_spacing (GTK_GRID (grid_filter),    2);
    gtk_grid_set_row_spacing (GTK_GRID (grid_bookmarks), 2);
    gtk_grid_set_row_spacing (GTK_GRID (grid_icons),     2);
    gtk_grid_set_row_spacing (GTK_GRID (grid_coverart),  2);
    gtk_grid_set_row_spacing (GTK_GRID (grid_tree),      2);
    gtk_grid_set_row_spacing (GTK_GRID (grid_colors),    2);

    gtk_widget_set_size_request (lbl_search_delay,      300,    -1);
    gtk_widget_set_size_request (lbl_fullsearch_wait,   300,    -1);
    gtk_widget_set_size_request (lbl_filter,            200-48, -1);
    gtk_widget_set_size_request (lbl_bookmarks_file,    200,    -1);
    gtk_widget_set_size_request (lbl_icon_size,         200,    -1);
    gtk_widget_set_size_request (lbl_coverart,          200,    -1);
    gtk_widget_set_size_request (lbl_coverart_size,     200,    -1);
    gtk_widget_set_size_request (lbl_font_size,         200,    -1);
    gtk_widget_set_size_request (lbl_color_bg,          200,    -1);
    gtk_widget_set_size_request (lbl_color_fg,          200,    -1);
    gtk_widget_set_size_request (lbl_color_bg_sel,      200,    -1);
    gtk_widget_set_size_request (lbl_color_fg_sel,      200,    -1);

    gtk_widget_set_size_request (spin_icon_size,         50,    -1);
    gtk_widget_set_size_request (spin_coverart_size,     50,    -1);
    gtk_widget_set_size_request (spin_font_size,         50,    -1);
    gtk_widget_set_size_request (button_color_bg,        50,    -1);
    gtk_widget_set_size_request (button_color_fg,        50,    -1);
    gtk_widget_set_size_request (button_color_bg_sel,    50,    -1);
    gtk_widget_set_size_request (button_color_fg_sel,    50,    -1);

#if GTK_CHECK_VERSION(3,16,0)
    gtk_label_set_xalign (GTK_LABEL (lbl_search_delay),     0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_fullsearch_wait),  0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_filter),           0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_bookmarks_file),   0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_icon_size),        0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_coverart),         0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_coverart_size),    0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_font_size),        0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_color_bg),         0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_color_fg),         0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_color_bg_sel),     0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_color_fg_sel),     0.);
#endif

    gtk_radio_button_join_group (GTK_RADIO_BUTTON (radio_filter_custom), GTK_RADIO_BUTTON (radio_filter_auto));

    g_signal_connect (radio_filter_custom, "toggled", G_CALLBACK (on_settings_radio_filter_toggled), entry_filter);


    // page 1

    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page1, NULL);
    gtk_notebook_set_tab_label_text (GTK_NOTEBOOK (notebook), page1, _("Plugin"));

    gtk_container_add (GTK_CONTAINER (frame_plugin), grid_plugin);
    gtk_box_pack_start (GTK_BOX (page1), frame_plugin, FALSE, TRUE, 0);

    // CONFIG_ENABLED
    gtk_grid_attach (GTK_GRID (grid_plugin), check_enabled, 0, 0, 2, 1);

    // CONFIG_SAVE_TREEVIEW
    gtk_grid_attach (GTK_GRID (grid_plugin), check_save_view, 0, 1, 2, 1);

    gtk_container_add (GTK_CONTAINER (frame_layout), grid_layout);
    gtk_box_pack_start (GTK_BOX (page1), frame_layout, FALSE, TRUE, 0);

    // CONFIG_HIDDEN
    gtk_grid_attach (GTK_GRID (grid_layout), check_hidden, 0, 0, 2, 1);

    // CONFIG_HIDE_NAVIGATION
    gtk_grid_attach (GTK_GRID (grid_layout), check_hide_nav, 0, 1, 2, 1);

    // CONFIG_HIDE_SEARCH
    gtk_grid_attach (GTK_GRID (grid_layout), check_hide_search, 0, 2, 2, 1);

    // CONFIG_HIDE_TOOLBAR
    gtk_grid_attach (GTK_GRID (grid_layout), check_hide_tools, 0, 3, 2, 1);


    // page 2

    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page2, NULL);
    gtk_notebook_set_tab_label_text (GTK_NOTEBOOK (notebook), page2, _("Directories"));

    gtk_container_add (GTK_CONTAINER (frame_paths), grid_paths);
    gtk_box_pack_start (GTK_BOX (page2), frame_paths, FALSE, TRUE, 0);

    // CONFIG_DEFAULT_PATH


    // page 3

    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page3, NULL);
    gtk_notebook_set_tab_label_text (GTK_NOTEBOOK (notebook), page3, _("Filtering"));

    gtk_container_add (GTK_CONTAINER (frame_filter), grid_filter);
    gtk_box_pack_start (GTK_BOX (page3), frame_filter, FALSE, TRUE, 0);

    // CONFIG_SHOW_HIDDEN_FILES
    gtk_grid_attach (GTK_GRID (grid_filter), check_show_hidden, 0, 0, 2, 1);

    // CONFIG_FILTER_ENABLED
    gtk_grid_attach (GTK_GRID (grid_filter), check_filter_enabled, 0, 1, 2, 1);

    // CONFIG_FILTER_AUTO
    gtk_grid_attach (GTK_GRID (grid_filter), radio_filter_auto, 0, 2, 2, 1);
    gtk_widget_set_margin_start (radio_filter_auto, 16);

    // CONFIG_FILTER
    gtk_grid_attach (GTK_GRID (grid_filter), radio_filter_custom, 0, 3, 2, 1);
    gtk_grid_attach (GTK_GRID (grid_filter), lbl_filter, 0, 4, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_filter), entry_filter, 1, 4, 1, 1);
    gtk_widget_set_margin_start (radio_filter_custom, 16);
    gtk_widget_set_margin_start (lbl_filter, 40);
    gtk_widget_set_hexpand (entry_filter, TRUE);

    gtk_container_add (GTK_CONTAINER (frame_search), grid_search);
    gtk_box_pack_start (GTK_BOX (page3), frame_search, FALSE, TRUE, 0);

    // CONFIG_SEARCH_DELAY
    gtk_grid_attach (GTK_GRID (grid_search), lbl_search_delay, 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_search), spin_search_delay, 1, 0, 1, 1);

    // CONFIG_FULLSEARCH_WAIT
    gtk_grid_attach (GTK_GRID (grid_search), lbl_fullsearch_wait, 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_search), spin_fullsearch_wait, 1, 1, 1, 1);


    // page 4

    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page4, NULL);
    gtk_notebook_set_tab_label_text (GTK_NOTEBOOK (notebook), page4, _("Content"));

    gtk_container_add (GTK_CONTAINER (frame_bookmarks), grid_bookmarks);
    gtk_box_pack_start (GTK_BOX (page4), frame_bookmarks, FALSE, TRUE, 0);

    // CONFIG_SHOW_BOOKMARKS
    gtk_grid_attach (GTK_GRID (grid_bookmarks), check_show_bookmarks, 0, 0, 2, 1);

    // CONFIG_BOOKMARKS_FILE
    gtk_grid_attach (GTK_GRID (grid_bookmarks), lbl_bookmarks_file, 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_bookmarks), entry_bookmarks_file, 1, 1, 1, 1);
    gtk_widget_set_hexpand (entry_bookmarks_file, TRUE);

    gtk_container_add (GTK_CONTAINER (frame_icons), grid_icons);
    gtk_box_pack_start (GTK_BOX (page4), frame_icons, FALSE, TRUE, 0);

    // CONFIG_SHOW_ICONS
    gtk_grid_attach (GTK_GRID (grid_icons), check_show_icons, 0, 0, 2, 1);

    // CONFIG_ICON_SIZE
    gtk_grid_attach (GTK_GRID (grid_icons), lbl_icon_size, 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_icons), spin_icon_size, 1, 1, 1, 1);

    gtk_container_add (GTK_CONTAINER (frame_coverart), grid_coverart);
    gtk_box_pack_start (GTK_BOX (page4), frame_coverart, FALSE, TRUE, 0);

    // CONFIG_SHOW_COVERART
    gtk_grid_attach (GTK_GRID (grid_coverart), check_show_coverart, 0, 0, 2, 1);

    // CONFIG_COVERART
    gtk_grid_attach (GTK_GRID (grid_coverart), lbl_coverart, 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_coverart), entry_coverart, 1, 1, 1, 1);
    gtk_widget_set_hexpand (entry_coverart, TRUE);

    // CONFIG_COVERART_SIZE
    gtk_grid_attach (GTK_GRID (grid_coverart), lbl_coverart_size, 0, 2, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_coverart), spin_coverart_size, 1, 2, 1, 1);

    // CONFIG_COVERART_SCALE
    gtk_grid_attach (GTK_GRID (grid_coverart), check_coverart_scale, 0, 3, 2, 1);


    // page 5

    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page5, NULL);
    gtk_notebook_set_tab_label_text (GTK_NOTEBOOK (notebook), page5, _("Look & Feel"));

    gtk_container_add (GTK_CONTAINER (frame_tree), grid_tree);
    gtk_box_pack_start (GTK_BOX (page5), frame_tree, FALSE, TRUE, 0);

    // CONFIG_SORT_TREEVIEW
    gtk_grid_attach (GTK_GRID (grid_tree), check_sort_tree, 0, 0, 2, 1);

    // CONFIG_SHOW_TREE_LINES
    gtk_grid_attach (GTK_GRID (grid_tree), check_show_treelines, 0, 1, 2, 1);

    gtk_container_add (GTK_CONTAINER (frame_colors), grid_colors);
    gtk_box_pack_start (GTK_BOX (page5), frame_colors, FALSE, TRUE, 0);

    // CONFIG_FONT_SIZE
    gtk_grid_attach (GTK_GRID (grid_colors), lbl_font_size, 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_colors), spin_font_size, 1, 0, 1, 1);

    gtk_grid_attach (GTK_GRID (grid_colors), lbl_color_bg, 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_colors), button_color_bg, 1, 1, 1, 1);

    gtk_grid_attach (GTK_GRID (grid_colors), lbl_color_fg, 0, 2, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_colors), button_color_fg, 1, 2, 1, 1);

    gtk_grid_attach (GTK_GRID (grid_colors), lbl_color_bg_sel, 0, 3, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_colors), button_color_bg_sel, 1, 3, 1, 1);

    gtk_grid_attach (GTK_GRID (grid_colors), lbl_color_fg_sel, 0, 4, 1, 1);
    gtk_grid_attach (GTK_GRID (grid_colors), button_color_fg_sel, 1, 4, 1, 1);


    // notebook

    gtk_container_set_border_width (GTK_CONTAINER (notebook), 8);
    gtk_box_pack_start (GTK_BOX (content), notebook, TRUE, TRUE, 0);

    gtk_widget_show_all (settings);


    int response;
    do
    {
        GdkRGBA color;

        trace("update dialog\n");

        // update all widgets
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_enabled), CONFIG_ENABLED);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_save_view), CONFIG_SAVE_TREEVIEW);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_hidden), CONFIG_HIDDEN);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_hide_nav), CONFIG_HIDE_NAVIGATION);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_hide_search), CONFIG_HIDE_SEARCH);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_hide_tools), CONFIG_HIDE_TOOLBAR);

        settings_update_paths (GTK_GRID (grid_paths), g_strdup (CONFIG_DEFAULT_PATH));

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_show_hidden), CONFIG_SHOW_HIDDEN_FILES);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_filter_enabled), CONFIG_FILTER_ENABLED);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio_filter_auto), CONFIG_FILTER_AUTO);
        gtk_entry_set_text (GTK_ENTRY (entry_filter), CONFIG_FILTER);
        gtk_widget_set_sensitive (GTK_WIDGET (entry_filter), !CONFIG_FILTER_AUTO);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_search_delay), CONFIG_SEARCH_DELAY);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_fullsearch_wait), CONFIG_FULLSEARCH_WAIT);

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_show_bookmarks), CONFIG_SHOW_BOOKMARKS);
        gtk_entry_set_text (GTK_ENTRY (entry_bookmarks_file), CONFIG_BOOKMARKS_FILE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_show_icons), CONFIG_SHOW_ICONS);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_icon_size), CONFIG_ICON_SIZE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_show_coverart), CONFIG_SHOW_COVERART);
        gtk_entry_set_text (GTK_ENTRY (entry_coverart), CONFIG_COVERART);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_coverart_size), CONFIG_COVERART_SIZE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_coverart_scale), CONFIG_COVERART_SCALE);

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_sort_tree), CONFIG_SORT_TREEVIEW);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_show_treelines), CONFIG_SHOW_TREE_LINES);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin_font_size), CONFIG_FONT_SIZE);
        if (gdk_rgba_parse (&color, CONFIG_COLOR_BG))
            gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (button_color_bg), &color);
        if (gdk_rgba_parse (&color, CONFIG_COLOR_FG))
            gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (button_color_fg), &color);
        if (gdk_rgba_parse (&color, CONFIG_COLOR_BG_SEL))
            gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (button_color_bg_sel), &color);
        if (gdk_rgba_parse (&color, CONFIG_COLOR_FG_SEL))
            gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (button_color_fg_sel), &color);

        response = gtk_dialog_run (GTK_DIALOG (settings));
        if (response == GTK_RESPONSE_CANCEL)
            break;

        trace("read settings, response=%d\n", response);

        // read out settings
        g_free ((gchar*) CONFIG_DEFAULT_PATH);
        g_free ((gchar*) CONFIG_FILTER);
        g_free ((gchar*) CONFIG_COVERART);
        g_free ((gchar*) CONFIG_BOOKMARKS_FILE);
        g_free ((gchar*) CONFIG_COLOR_BG);
        g_free ((gchar*) CONFIG_COLOR_FG);
        g_free ((gchar*) CONFIG_COLOR_BG_SEL);
        g_free ((gchar*) CONFIG_COLOR_FG_SEL);

        deadbeef->conf_lock ();

        CONFIG_ENABLED              = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_enabled));
        CONFIG_SAVE_TREEVIEW        = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_save_view));
        CONFIG_HIDDEN               = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_hidden));
        CONFIG_HIDE_NAVIGATION      = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_hide_nav));
        CONFIG_HIDE_SEARCH          = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_hide_search));
        CONFIG_HIDE_TOOLBAR         = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_hide_tools));

        CONFIG_DEFAULT_PATH         = settings_get_paths (GTK_GRID (grid_paths));
        trace("defpath: %s\n",CONFIG_DEFAULT_PATH);

        CONFIG_SHOW_HIDDEN_FILES    = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_show_hidden));
        CONFIG_FILTER_ENABLED       = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_filter_enabled));
        CONFIG_FILTER_AUTO          = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (radio_filter_auto));
        CONFIG_FILTER               = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry_filter)));
        CONFIG_SEARCH_DELAY         = gtk_spin_button_get_value (GTK_SPIN_BUTTON (spin_search_delay));
        CONFIG_FULLSEARCH_WAIT      = gtk_spin_button_get_value (GTK_SPIN_BUTTON (spin_fullsearch_wait));

        CONFIG_SHOW_BOOKMARKS       = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_show_bookmarks));
        CONFIG_BOOKMARKS_FILE       = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry_bookmarks_file)));
        CONFIG_SHOW_ICONS           = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_show_icons));
        CONFIG_ICON_SIZE            = gtk_spin_button_get_value (GTK_SPIN_BUTTON (spin_icon_size));
        CONFIG_SHOW_COVERART        = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_show_coverart));
        CONFIG_COVERART             = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry_coverart)));
        CONFIG_COVERART_SIZE        = gtk_spin_button_get_value (GTK_SPIN_BUTTON (spin_coverart_size));
        CONFIG_COVERART_SCALE       = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_coverart_scale));

        CONFIG_SORT_TREEVIEW        = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_sort_tree));
        CONFIG_SHOW_TREE_LINES      = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check_show_treelines));
        CONFIG_FONT_SIZE            = gtk_spin_button_get_value (GTK_SPIN_BUTTON (spin_font_size));
        CONFIG_COLOR_BG             = gtk_color_chooser_get_hex (GTK_COLOR_CHOOSER (button_color_bg));
        CONFIG_COLOR_FG             = gtk_color_chooser_get_hex (GTK_COLOR_CHOOSER (button_color_fg));
        CONFIG_COLOR_BG_SEL         = gtk_color_chooser_get_hex (GTK_COLOR_CHOOSER (button_color_bg_sel));
        CONFIG_COLOR_FG_SEL         = gtk_color_chooser_get_hex (GTK_COLOR_CHOOSER (button_color_fg_sel));

        deadbeef->conf_unlock ();

        save_config ();
        update_rootdirs ();
        treeview_update (NULL);
        deadbeef->sendmessage (DB_EV_CONFIGCHANGED, 0, 0, 0);
    }
    while (response == GTK_RESPONSE_APPLY);

    gtk_widget_destroy (settings);
    return;
}
#endif  // GTK_CHECK_VERSION


/*----------------------------*/
/* Treebrowser core functions */
/*----------------------------*/


/* Add given URI to DeaDBeeF's current playlist */
static void
add_uri_to_playlist_worker (void *data)
{
    GList *uri_list = (GList*)data;

    ddb_playlist_t *plt = deadbeef->plt_get_curr ();

    if (deadbeef->plt_add_files_begin (plt, 0) < 0)
    {
        fprintf (stderr, _("could not add files to playlist (lock failed)\n"));
        goto error;
    }

    GList *node;
    for (node = uri_list->next; node; node = node->next)
    {
        gchar *uri = node->data;
        if (g_file_test (uri, G_FILE_TEST_IS_DIR))
        {
            //trace("trying to add folder %s\n", uri);
            if (deadbeef->plt_add_dir2 (0, plt, uri, NULL, NULL) < 0)
                fprintf (stderr, _("failed to add folder %s\n"), uri);
        }
        else
        {
            //trace("trying to add file %s\n", uri);
            if (deadbeef->plt_add_file2 (0, plt, uri, NULL, NULL) < 0)
                fprintf (stderr, _("failed to add file %s\n"), uri);
        }
        g_free (uri);
    }

    deadbeef->plt_add_files_end (plt, 0);
    deadbeef->plt_modified (plt);

    trace("finished adding files to playlist\n");

    deadbeef->plt_save_config (plt);
    deadbeef->conf_save ();

error:
    deadbeef->plt_unref (plt);
    g_list_free (uri_list);
}

static void
add_uri_to_playlist (GList *uri_list, int index, int append, int threaded)
{
    if (! uri_list)
        return;

    deadbeef->pl_lock ();

    ddb_playlist_t *plt;
    int count = deadbeef->plt_get_count ();

    if (index == PLT_CURRENT)
    {
        plt = deadbeef->plt_get_curr ();

        if (! append)
        {
            deadbeef->plt_select_all (plt);
            deadbeef->plt_delete_selected (plt);
        }
    }
    else
    {
        if ((index == PLT_NEW) || (index >= count))
        {
            const gchar *title = _("New Playlist");

            if (deadbeef->conf_get_int ("gtkui.name_playlist_from_folder", 0))
            {
                GString *title_str = g_string_new ("");
                GList *node;
                for (node = uri_list->next; node; node = node->next)  // first item is always NULL
                {
                    gchar *uri = node->data;
                    const gchar *folder = strrchr (uri, '/');
                    if (title_str->len > 0)
                        g_string_append (title_str, ", ");
                    if (folder)
                        g_string_append (title_str, folder+1);
                }
                title = g_string_free (title_str, FALSE);
            }

            index = deadbeef->plt_add (count, g_strdup (title));
        }

        plt = deadbeef->plt_get_for_idx (index);
    }

    deadbeef->pl_unlock ();

    if (plt == NULL)
    {
        fprintf (stderr, _("could not get playlist\n"));
        return;
    }

    deadbeef->plt_set_curr (plt);

    trace("starting thread for adding files to playlist\n");

    if (threaded)
    {
        intptr_t tid = deadbeef->thread_start (add_uri_to_playlist_worker, (void*)uri_list);
        deadbeef->thread_detach (tid);
    }
    else
    {
        add_uri_to_playlist_worker (uri_list);
    }
}

/* Check if file is filtered by extension (return FALSE if not shown) */
static gboolean
check_filtered (const gchar *base_name)
{
    if (! CONFIG_FILTER_ENABLED)
        return TRUE;

    const gchar *filter;
    if (! CONFIG_FILTER_AUTO)
        filter = CONFIG_FILTER;
    else
        filter = known_extensions;

    if (strlen (filter) == 0)
        return TRUE;

    // Use two filterstrings for upper- & lowercase matching
    gchar *filter_u = g_ascii_strup (filter, -1);
    gchar **filters_u = g_strsplit (filter_u, ";", 0);
    g_free (filter_u);

    gchar *filter_d = g_ascii_strdown (filter, -1);
    gchar **filters_d = g_strsplit (filter_d, ";", 0);
    g_free (filter_d);

    gboolean filtered = FALSE;
    for (gint i = 0; filters_u[i] && filters_d[i]; i++)
    {
        if (utils_str_equal (base_name, "*")
                    || g_pattern_match_simple (filters_u[i], base_name)
                    || g_pattern_match_simple (filters_d[i], base_name))
                    {
            filtered = TRUE;
            break;
        }
    }

    g_strfreev (filters_u);
    g_strfreev (filters_d);

    return filtered;
}

/* Check if file matches the search string (return FALSE if not shown) */
static gboolean
check_search (const gchar *filename)
{
    gboolean is_searched = TRUE;

    if (searchbar_text)
    {
        gchar **sub_path = g_strsplit (filename, addressbar_last_address, 2);

        gint n = strlen (searchbar_text);
        gint m = strlen (sub_path[1]);
        if (n > 0)
        {
            const gchar *path = g_utf8_casefold (sub_path[1], m);
            const gchar *text = g_utf8_casefold (searchbar_text, n);

            if (g_strstr_len (path, m, text) == NULL)
                is_searched = FALSE;

            g_free ((gpointer) path);
            g_free ((gpointer) text);
        }

        g_strfreev (sub_path);
    }

    return is_searched;
}

/* Check if file should be hidden (return FALSE if not shown) */
static gboolean
check_hidden (const gchar *filename)
{
    gboolean is_hidden = TRUE;

    if (! CONFIG_SHOW_HIDDEN_FILES)
    {
        const gchar *base_name = g_path_get_basename (filename);
        if (base_name[0] == '.')
            is_hidden = FALSE;
        g_free ((gpointer) base_name);
    }

    return is_hidden;
}

/* Check if directory is empty = does not contain anything we want to show (return FALSE if not shown) */
static gboolean
check_empty (gchar *directory)
{
    gboolean        is_dir;
    gchar           *utf8_name = NULL;
    GSList          *list, *node;
    gchar           *fname = NULL;
    gchar           *uri = NULL;

    list = utils_get_file_list (directory, NULL, CONFIG_SORT_TREEVIEW, NULL);
    if (list != NULL)
    {
        foreach_slist_free (node, list)
        {
            fname       = node->data;
            uri         = g_strconcat (directory, G_DIR_SEPARATOR_S, fname, NULL);
            is_dir      = g_file_test (uri, G_FILE_TEST_IS_DIR);
            utf8_name   = utils_get_utf8_from_locale (fname);

            if (check_hidden (uri))
            {
                if (is_dir)
                {
                    if (check_empty (uri))
                    {
                        g_free (utf8_name);
                        g_free (uri);
                        g_free (fname);
                        return TRUE;
                    }
                }
                else
                {
                    if (check_filtered (utf8_name) && check_search (uri))
                    {
                        g_free (utf8_name);
                        g_free (uri);
                        g_free (fname);
                        return TRUE;
                    }
                }
            }
        }
    }

    g_free (utf8_name);
    g_free (uri);
    g_free (fname);
    return FALSE;
}


/* Get default dir from config, use home as fallback */
static gchar *
get_default_dir (void)
{
    const gchar *path = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (addressbar));

    if (g_file_test (path, G_FILE_TEST_EXISTS))
        return g_strdup (path);

    return g_strdup (g_get_user_special_dir (G_USER_DIRECTORY_MUSIC));
}

/* Try to get icon from cache, update cache if not found or original is newer */
static GdkPixbuf *
get_icon_from_cache (const gchar *uri, const gchar *coverart)
{
    GdkPixbuf *icon = NULL;
    int size = ICON_SIZE (CONFIG_COVERART_SIZE);

    gchar *iconfile  = g_strconcat (uri, G_DIR_SEPARATOR_S, coverart, NULL);
    gchar *cachefile = utils_make_cache_path (uri, size, CONFIG_COVERART_SCALE);

    if (g_file_test (iconfile, G_FILE_TEST_EXISTS))
    {
        // Check if original file was updated
        if (g_file_test (cachefile, G_FILE_TEST_EXISTS))
        {
            struct stat cache_stat, icon_stat;
            stat (cachefile, &cache_stat);
            stat (iconfile, &icon_stat);

            if (icon_stat.st_mtime <= cache_stat.st_mtime)
            {
                //trace ("cached icon for %s\n", uri);
                icon = gdk_pixbuf_new_from_file (cachefile, NULL);
            }
        }

        if (! icon)
        {
            trace ("creating new icon for %s\n", uri);

            if (CONFIG_COVERART_SCALE)
            {
                // get image from file, scaling it to requested size
                GdkPixbuf *coverart = gdk_pixbuf_new_from_file_at_scale (iconfile, size, size, TRUE, NULL);
                if (coverart)
                {
                    // create square icon and put the down-scaled image in it
                    int bps = gdk_pixbuf_get_bits_per_sample (coverart);
                    int w = gdk_pixbuf_get_width (coverart);
                    int h = gdk_pixbuf_get_height (coverart);
                    int x = (size - w) / 2;
                    int y = (size - h) / 2;

                    icon = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, bps, size, size);  // add alpha channel
                    gdk_pixbuf_fill (icon, 0x00000000);  // make icon transparent
                    gdk_pixbuf_copy_area (coverart, 0, 0, w, h, icon, x, y);
                    g_object_unref (coverart);
                }
            }
            else
            {
                icon = gdk_pixbuf_new_from_file_at_scale (iconfile, -1, size, TRUE, NULL);  // do not constrain width
            }

            if (icon)
            {
                GError *err = NULL;
                gdk_pixbuf_save (icon, cachefile, "png", &err, NULL);
                if (err)
                {
                    fprintf (stderr, "Could not cache coverart image %s: %s\n", iconfile, err->message);
                    g_error_free (err);
                }
            }
            else
            {
                fprintf (stderr, "Coverart image is corrupted: %s\n", iconfile);
            }
        }
    }

    g_free (cachefile);
    g_free (iconfile);

    return icon;
}

/* Get icon for selected URI - default icon or folder image */
static GdkPixbuf *
get_icon_for_uri (gchar *uri)
{
    if (! CONFIG_SHOW_ICONS)
        return NULL;

    GdkPixbuf *icon = NULL;
    int size = ICON_SIZE (CONFIG_ICON_SIZE);

    if (! g_file_test (uri, G_FILE_TEST_IS_DIR))
    {
#if GLIB_CHECK_VERSION(2, 34, 0)
        gchar *content_type;
        gchar *icon_name;
        GtkIconTheme *icon_theme;

        content_type = g_content_type_guess (uri, NULL, 0, NULL);
        icon_name    = g_content_type_get_generic_icon_name (content_type);
        icon_theme   = gtk_icon_theme_get_default ();

        icon = gtk_icon_theme_load_icon (icon_theme, icon_name, size, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);

        g_free (content_type);
        g_free (icon_name);
#endif
        // Fallback to default icon
        if (! icon)
            icon = utils_pixbuf_from_stock ("gtk-file", size);

        return icon;
    }

    if (CONFIG_SHOW_COVERART)
    {
        // Check for cover art in folder, otherwise use default icon
        gchar **coverart = g_strsplit (CONFIG_COVERART, ";", 0);
        for (gint i = 0; coverart[i] && ! icon; i++)
            icon = get_icon_from_cache (uri, coverart[i]);

        g_strfreev (coverart);
    }

    // Fallback to default icon
    if (! icon)
        icon = utils_pixbuf_from_stock ("folder", size);

    return icon;
}

/* Check if row defined by iter is expanded or not */
static gboolean
treeview_row_expanded_iter (GtkTreeView *tree_view, GtkTreeIter *iter)
{
    GtkTreePath *path;
    gboolean expanded;

    path = gtk_tree_model_get_path (gtk_tree_view_get_model (tree_view), iter);
    expanded = gtk_tree_view_row_expanded (tree_view, path);
    gtk_tree_path_free (path);

    return expanded;
}

/* Check if row should be expanded, returns NULL if not */
static GSList *
treeview_check_expanded (gchar *uri)
{
    if (! expanded_rows || ! uri)
        return NULL;

    GSList *node;
    for (node = expanded_rows->next; node; node = node->next)  // first item is always NULL
    {
        gchar *enc_uri = g_filename_to_uri (uri, NULL, NULL);
        gboolean match = utils_str_equal (enc_uri, node->data);
        g_free (enc_uri);
        if (match)
            break;
    }
    return node;  // == NULL if last node was reached
}

static void
treeview_clear_expanded (void)
{
    trace("clear expanded rows\n");

    if (! expanded_rows)
        return;

    for (GSList *node = expanded_rows->next; node; node = node->next)  // first items is always NULL
    {
        if (node->data)
            g_free (node->data);
    }
    g_slist_free (expanded_rows);

    expanded_rows = g_slist_alloc ();  // make sure expanded_rows stays valid
}

/* Restore previously expanded nodes */
static void
treeview_restore_expanded (gpointer parent)
{
    GtkTreeIter i;
    gchar *uri;
    gboolean valid = gtk_tree_model_iter_children (GTK_TREE_MODEL (treestore), &i, parent);
    while (valid)
    {
        gtk_tree_model_get (GTK_TREE_MODEL (treestore), &i,
                        TREEBROWSER_COLUMN_URI, &uri, -1);
        if (treeview_check_expanded (uri))
        {
            gtk_tree_view_expand_row (GTK_TREE_VIEW (treeview),
                        gtk_tree_model_get_path (GTK_TREE_MODEL (treestore), &i),
                        FALSE);
            treebrowser_browse (uri, &i);
        }

        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (treestore), &i);
    }
}

static gboolean
treeview_separator_func (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    gint flag;
    gtk_tree_model_get (model, iter, TREEBROWSER_COLUMN_FLAG, &flag, -1);
    return (flag == TREEBROWSER_FLAGS_SEPARATOR);
}

/* Check if path entered in addressbar really is a directory */
static gboolean
treebrowser_checkdir (const gchar *directory)
{
    gboolean is_dir = g_file_test (directory, G_FILE_TEST_IS_DIR);
    return is_dir;
}

/* Change root directory of treebrowser */
static void
treebrowser_chroot (gchar *directory)
{
    trace("chroot to directory: %s\n", directory);

    if (! directory)
        directory = get_default_dir ();  // fallback

    if (g_str_has_suffix (directory, G_DIR_SEPARATOR_S))
        g_strlcpy (directory, directory, strlen (directory));

    gtk_entry_set_text (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (addressbar))), directory);

    if (! directory || (strlen (directory) == 0))
        directory = G_DIR_SEPARATOR_S;

    if (! treebrowser_checkdir (directory))
        return;

    setptr (addressbar_last_address, g_strdup (directory));

    treebrowser_browse_dir (NULL);  // use addressbar
    //trace("starting thread for adding files to playlist\n");
    //intptr_t tid = deadbeef->thread_start (treebrowser_browse_dir, NULL);
    //deadbeef->thread_detach (tid);
}

/* Browse given directory - update contents and fill in the treeview */
static void
treebrowser_browse_dir (gpointer directory)
{
    trace("browse directory: %s\n", (gchar*) directory);

    //deadbeef->mutex_lock (treebrowser_mutex);

    treebrowser_bookmarks_set_state ();
    gtk_tree_store_clear (treestore);

    // freeze the treeview during update to improve performance
    gtk_widget_freeze_child_notify (treeview);
    treebrowser_browse ((gchar*) directory, NULL);
    gtk_widget_thaw_child_notify (treeview);

    treebrowser_load_bookmarks ();
    treeview_restore_expanded (NULL);

    //deadbeef->mutex_unlock (treebrowser_mutex);
}

static gboolean
treebrowser_browse (gchar *directory, gpointer parent)
{
    GtkTreeIter     iter, iter_empty, *last_dir_iter = NULL;
    gboolean        is_dir;
    gboolean        expanded = FALSE;
    gboolean        has_parent;
    gchar           *utf8_name;
    GSList          *list, *node;

    gchar           *fname;
    gchar           *uri;
    gchar           *tooltip;

    if (! directory)
        directory = addressbar_last_address;

    if (! directory)
        directory = get_default_dir ();  // fallback

    directory = g_strconcat (directory, G_DIR_SEPARATOR_S, NULL);

    has_parent = parent ? gtk_tree_store_iter_is_valid (treestore, parent) : FALSE;
    if (has_parent && treeview_row_expanded_iter (GTK_TREE_VIEW (treeview), parent))
    {
        expanded = TRUE;
        treebrowser_bookmarks_set_state ();
    }

    tree_store_iter_clear_nodes (treestore, parent, FALSE);

    list = utils_get_file_list (directory, NULL, CONFIG_SORT_TREEVIEW, NULL);
    if (list != NULL)
    {
        gboolean all_hidden = TRUE;  // show "contents hidden" note if all files are hidden
        foreach_slist_free (node, list)
        {
            fname       = node->data;
            uri         = g_strconcat (directory, fname, NULL);
            is_dir      = g_file_test (uri, G_FILE_TEST_IS_DIR);
            utf8_name   = utils_get_utf8_from_locale (fname);
            tooltip     = utils_tooltip_from_uri (uri);

            if (check_hidden (uri))
            {
                GdkPixbuf *icon = NULL;

                if (is_dir && check_empty (uri))
                {
                    if (last_dir_iter == NULL)
                    {
                        gtk_tree_store_prepend (treestore, &iter, parent);
                    }
                    else
                    {
                        gtk_tree_store_insert_after (treestore, &iter, parent, last_dir_iter);
                        gtk_tree_iter_free (last_dir_iter);
                    }
                    last_dir_iter = gtk_tree_iter_copy (&iter);

                    icon = get_icon_for_uri (uri);
                    gtk_tree_store_set (treestore, &iter,
                                    TREEBROWSER_COLUMN_ICON,    icon,
                                    TREEBROWSER_COLUMN_NAME,    fname,
                                    TREEBROWSER_COLUMN_URI,     uri,
                                    TREEBROWSER_COLUMN_TOOLTIP, tooltip,
                                    -1);
                    gtk_tree_store_prepend (treestore, &iter_empty, &iter);
                    gtk_tree_store_set (treestore, &iter_empty,
                                    TREEBROWSER_COLUMN_ICON,    NULL,
                                    TREEBROWSER_COLUMN_NAME,    _("(Empty)"),
                                    TREEBROWSER_COLUMN_URI,     NULL,
                                    TREEBROWSER_COLUMN_TOOLTIP, NULL,
                                    -1);
                    all_hidden = FALSE;
                }
                else
                {
                    if (check_filtered (utf8_name) && check_search (uri))
                    {
                        icon = get_icon_for_uri (uri);
                        gtk_tree_store_append (treestore, &iter, parent);
                        gtk_tree_store_set (treestore, &iter,
                                        TREEBROWSER_COLUMN_ICON,    icon,
                                        TREEBROWSER_COLUMN_NAME,    fname,
                                        TREEBROWSER_COLUMN_URI,     uri,
                                        TREEBROWSER_COLUMN_TOOLTIP, tooltip,
                                        -1);
                        all_hidden = FALSE;
                    }
                }

                if (icon)
                    g_object_unref (icon);
            }

            g_free (utf8_name);
            g_free (uri);
            g_free (fname);
            g_free (tooltip);
        }

        if (all_hidden)
        {
            //  Directory with all contents hidden
            gtk_tree_store_prepend (treestore, &iter_empty, parent);
            gtk_tree_store_set (treestore, &iter_empty,
                            TREEBROWSER_COLUMN_ICON,    NULL,
                            TREEBROWSER_COLUMN_NAME,    _("(Contents hidden)"),
                            TREEBROWSER_COLUMN_URI,     NULL,
                            TREEBROWSER_COLUMN_TOOLTIP, _("This directory has files in it, but they are filtered out"),
                            -1);
        }
    }
    else
    {
        //  Empty directory
        gtk_tree_store_prepend (treestore, &iter_empty, parent);
        gtk_tree_store_set (treestore, &iter_empty,
                        TREEBROWSER_COLUMN_ICON,    NULL,
                        TREEBROWSER_COLUMN_NAME,    _("(Empty)"),
                        TREEBROWSER_COLUMN_URI,     NULL,
                        TREEBROWSER_COLUMN_TOOLTIP, _("This directory has nothing in it"),
                        -1);
    }

    if (has_parent)
    {
        if (expanded)
            gtk_tree_view_expand_row (GTK_TREE_VIEW (treeview),
                            gtk_tree_model_get_path (GTK_TREE_MODEL (treestore), parent),
                            FALSE);
    }

    g_free (directory);

    treeview_restore_expanded (parent);

    return FALSE;
}

/* Set "bookmarks expanded" flag according to treeview */
static void
treebrowser_bookmarks_set_state (void)
{
    if (gtk_tree_store_iter_is_valid (treestore, &bookmarks_iter))
        bookmarks_expanded = treeview_row_expanded_iter (GTK_TREE_VIEW (treeview),
                        &bookmarks_iter);
    else
        bookmarks_expanded = FALSE;
}

/* Load user's bookmarks into top of tree */
static void
treebrowser_load_bookmarks_helper (const gchar *filename)
{
    gchar           *bookmarks;
    gchar           *contents, *path_full, *basename, *tooltip;
    gchar           **lines, **line;
    GtkTreeIter     iter;
    gchar           *pos;
    GdkPixbuf       *icon = NULL;

    bookmarks = utils_expand_home_dir (filename);

    trace("loading bookmarks from file: %s\n", bookmarks);
    if (g_file_get_contents (bookmarks, &contents, NULL, NULL))
    {
        lines = g_strsplit (contents, "\n", 0);
        for (line = lines; *line; ++line)
        {
            if (**line)
            {
                pos = g_utf8_strchr (*line, -1, ' ');
                if (pos != NULL)
                {
                    *pos = '\0';
                }
            }
            path_full = g_filename_from_uri (*line, NULL, NULL);
            //trace("  loaded bookmark: %s\n", path_full);

            if (path_full != NULL)
            {
                basename  = g_path_get_basename (path_full);
                tooltip   = utils_tooltip_from_uri (path_full);

                if (g_file_test (path_full, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
                {
                    gtk_tree_store_prepend (treestore, &iter, NULL);
                    icon = NULL;
                    if (CONFIG_SHOW_ICONS)
                        icon = utils_pixbuf_from_stock ("user-bookmarks", ICON_SIZE (CONFIG_ICON_SIZE));

                    gtk_tree_store_set (treestore, &iter,
                                    TREEBROWSER_COLUMN_ICON,    icon,
                                    TREEBROWSER_COLUMN_NAME,    basename,
                                    TREEBROWSER_COLUMN_URI,     path_full,
                                    TREEBROWSER_COLUMN_TOOLTIP, tooltip,
                                    TREEBROWSER_COLUMN_FLAG,    TREEBROWSER_FLAGS_BOOKMARK,
                                    -1);
                    if (icon)
                        g_object_unref (icon);
                }

                g_free (path_full);
                g_free (basename);
                g_free (tooltip);
            }
        }

        g_strfreev (lines);
        g_free (contents);
    }

    g_free (bookmarks);
}

static void
treebrowser_load_bookmarks (void)
{
    treebrowser_clear_bookmarks ();

    // GTK bookmarks
    if (CONFIG_SHOW_BOOKMARKS)
    {
#if !GTK_CHECK_VERSION(3,0,0)
        treebrowser_load_bookmarks_helper ("$HOME/.gtk-bookmarks");
#else
        treebrowser_load_bookmarks_helper ("$HOME/.config/gtk-3.0/bookmarks");
#endif
    }

    // extra bookmarks defined by user
    if (CONFIG_BOOKMARKS_FILE)
    {
        treebrowser_load_bookmarks_helper (CONFIG_BOOKMARKS_FILE);
    }
}

static void
treebrowser_clear_bookmarks (void)
{
    GList *bookmarks_list = NULL;
    gtk_tree_model_foreach (GTK_TREE_MODEL (treestore), (GtkTreeModelForeachFunc) bookmarks_foreach_func, &bookmarks_list);

    for (GList *node = bookmarks_list; node != NULL; node = node->next)
    {
        GtkTreePath *path;
        path = gtk_tree_row_reference_get_path ((GtkTreeRowReference*) node->data);

        if (path)
        {
            GtkTreeIter  iter;

            if (gtk_tree_model_get_iter (GTK_TREE_MODEL (treestore), &iter, path))
                gtk_tree_store_remove (GTK_TREE_STORE (treestore), &iter);
        }
    }

    g_list_foreach (bookmarks_list, (GFunc) gtk_tree_row_reference_free, NULL);
    g_list_free (bookmarks_list);
}


/*------------------*/
/* MAIN MENU EVENTS */
/*------------------*/


static void
on_mainmenu_toggle (GtkMenuItem *menuitem, gpointer *user_data)
{
    CONFIG_HIDDEN = ! gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem));
    if (CONFIG_HIDDEN)
        gtk_widget_hide (sidebar);
    else
        gtk_widget_show (sidebar);
}

/*------------------------*/
/* RIGHTCLICK MENU EVENTS */
/*------------------------*/


static void
on_menu_add (GtkMenuItem *menuitem, GList *uri_list)
{
    int plt = PLT_NEW;

    // Some magic to get the requested playlist id
    if (menuitem)
    {
        const gchar *label = gtk_menu_item_get_label (menuitem);
        gchar **slabel = g_strsplit (label, ":", 2);
        gchar *s = slabel[0];
        if (*s == '_')   // Handle mnemonics
            s++;
        plt = atoi (s) - 1;  // automatically selects PLT_CURRENT (= -1) on conversion failure
        g_free ((gpointer *) label);
        g_strfreev (slabel);
    }

    add_uri_to_playlist (uri_list, plt, TRUE, TRUE);  // append
}

static void
on_menu_add_current (GtkMenuItem *menuitem, GList *uri_list)
{
    add_uri_to_playlist (uri_list, PLT_CURRENT, TRUE, TRUE);  // append
}

static void
on_menu_replace_current (GtkMenuItem *menuitem, GList *uri_list)
{
    add_uri_to_playlist (uri_list, PLT_CURRENT, FALSE, TRUE);  // replace
}

static void
on_menu_add_new (GtkMenuItem *menuitem, GList *uri_list)
{
    add_uri_to_playlist (uri_list, PLT_NEW, TRUE, TRUE);
}

static void
on_menu_enter_directory (GtkMenuItem *menuitem, gchar *uri)
{
    treebrowser_chroot (uri);
}

static void
on_menu_go_up (GtkMenuItem *menuitem, gpointer *user_data)
{
    on_button_go_up ();
}

static void
on_menu_refresh (GtkMenuItem *menuitem, gpointer *user_data)
{
    treebrowser_chroot (addressbar_last_address);
}

static void
on_menu_expand_one (GtkMenuItem *menuitem, gpointer *user_data)
{
    trace("signal: menu -> expand one\n");

    if (user_data == NULL)
    {
        // apply to all items on first level
        GtkTreeIter iter;
        gtk_tree_model_get_iter_first (GTK_TREE_MODEL (treestore), &iter);

        GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL (treestore), &iter);
        while (tree_view_expand_rows_recursive (GTK_TREE_MODEL (treestore), GTK_TREE_VIEW (treeview), path, 2))  // use depth 2 here
            gtk_tree_path_next (path);
    }
    else
    {
        GtkTreePath *path = gtk_tree_path_copy ((GtkTreePath *) user_data);
        gint depth = gtk_tree_path_get_depth (path);
        tree_view_expand_rows_recursive (GTK_TREE_MODEL (treestore), GTK_TREE_VIEW (treeview), path, depth+1);
        gtk_tree_path_free (path);
    }
}

static void
on_menu_expand_all (GtkMenuItem *menuitem, gpointer *user_data)
{
    trace("signal: menu -> expand all\n");

    if (user_data == NULL)
    {
        // apply to all items on first level
        expand_all ();
    }
    else
    {
        GtkTreePath *path = gtk_tree_path_copy ((GtkTreePath *) user_data);
        tree_view_expand_rows_recursive (GTK_TREE_MODEL (treestore), GTK_TREE_VIEW (treeview), path, 0);  // expand all
        gtk_tree_path_free (path);
    }
}

static void
on_menu_collapse_all (GtkMenuItem *menuitem, gpointer *user_data)
{
    trace("signal: menu -> collapse all\n");

    if (user_data == NULL)
    {
        // apply to all items on first level
        collapse_all ();
    }
    else
    {
        GtkTreePath *path = gtk_tree_path_copy ((GtkTreePath *) user_data);
        tree_view_collapse_rows_recursive (GTK_TREE_MODEL (treestore), GTK_TREE_VIEW (treeview), path, 0);
        gtk_tree_path_free (path);
    }
}

static void
on_menu_copy_uri (GtkMenuItem *menuitem, GList *uri_list)
{
    if (! uri_list)
        return;

    GString *uri_str = g_string_new ("");
    GList *node;
    for (node = uri_list->next; node; node = node->next)
    {
        gchar *enc_uri = g_filename_to_uri (node->data, NULL, NULL);
        uri_str = g_string_append_c (uri_str, ' ');
        uri_str = g_string_append (uri_str, enc_uri);
    }

    gchar *uri = g_string_free (uri_str, FALSE);
    GtkClipboard *cb = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text (cb, uri, -1);
    g_free (uri);
}

static void
on_menu_show_bookmarks (GtkMenuItem *menuitem, gpointer *user_data)
{
    CONFIG_SHOW_BOOKMARKS = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem));
    treebrowser_chroot (addressbar_last_address);
}

static void
on_menu_show_hidden_files (GtkMenuItem *menuitem, gpointer *user_data)
{
    CONFIG_SHOW_HIDDEN_FILES = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem));
    treebrowser_chroot (addressbar_last_address);
}

static void
on_menu_use_filter (GtkMenuItem *menuitem, gpointer *user_data)
{
    CONFIG_FILTER_ENABLED = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem));
    treebrowser_chroot (addressbar_last_address);
}

static void
on_menu_hide_navigation (GtkMenuItem *menuitem, gpointer *user_data)
{
    CONFIG_HIDE_NAVIGATION = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem));
    if (CONFIG_HIDE_NAVIGATION)
        gtk_widget_hide (sidebar_addressbox);
    else
        gtk_widget_show (sidebar_addressbox);
}

static void
on_menu_hide_search (GtkMenuItem *menuitem, gpointer *user_data)
{
    CONFIG_HIDE_SEARCH = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem));
    if (CONFIG_HIDE_SEARCH)
        gtk_widget_hide (sidebar_searchbox);
    else
        gtk_widget_show (sidebar_searchbox);
}

static void
on_menu_hide_toolbar (GtkMenuItem *menuitem, gpointer *user_data)
{
    CONFIG_HIDE_TOOLBAR = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem));
    if (CONFIG_HIDE_TOOLBAR)
        gtk_widget_hide (sidebar_toolbar);
    else
        gtk_widget_show (sidebar_toolbar);
}

#if GTK_CHECK_VERSION(3,16,0)
static void
on_menu_rename (GtkMenuItem *menuitem, GList *uri_list)
{
    if (! uri_list)
        return;

    gchar *source_uri = uri_list->next->data;  // use only first item
    gboolean is_dir = g_file_test (source_uri, G_FILE_TEST_IS_DIR);

    gchar *path   = g_path_get_dirname (source_uri);
    gchar *source = g_path_get_basename (source_uri);

    GtkWidget *dialog = gtk_dialog_new_with_buttons (
            is_dir ? _("Rename directory") : _("Rename file"),
            GTK_WINDOW (gtkui_plugin->get_mainwin ()),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            _("_Cancel"), GTK_RESPONSE_CANCEL,
            _("_OK"), GTK_RESPONSE_OK,
            NULL);

    GtkWidget *content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

    GtkWidget *grid         = gtk_grid_new ();
    GtkWidget *lbl_source   = gtk_label_new (_("Source:"));
    GtkWidget *lbl_target   = gtk_label_new (_("Target:"));
    GtkWidget *entry_source = gtk_entry_new ();
    GtkWidget *entry_target = gtk_entry_new ();

    gtk_widget_set_margin_start (lbl_source, 40);
    gtk_widget_set_margin_start (lbl_target, 40);
    gtk_widget_set_hexpand (entry_source, TRUE);
    gtk_widget_set_hexpand (entry_target, TRUE);

    gtk_grid_attach (GTK_GRID (grid), lbl_source,   0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), entry_source, 1, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), lbl_target,   0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), entry_target, 1, 1, 1, 1);

    gtk_grid_set_row_spacing (GTK_GRID (grid), 2);

    gtk_widget_set_size_request (lbl_source, 100, -1);
    gtk_widget_set_size_request (lbl_target, 100, -1);

#if GTK_CHECK_VERSION(3,16,0)
    gtk_label_set_xalign (GTK_LABEL (lbl_source), 0.);
    gtk_label_set_xalign (GTK_LABEL (lbl_target), 0.);
#endif

    gtk_container_set_border_width (GTK_CONTAINER (grid), 8);
    gtk_box_pack_start (GTK_BOX (content), grid, TRUE, TRUE, 0);

    gtk_entry_set_text (GTK_ENTRY (entry_source), source);
    gtk_entry_set_text (GTK_ENTRY (entry_target), source);

    gtk_widget_set_sensitive (GTK_WIDGET (entry_source), FALSE);  // read-only

    gtk_widget_show_all (dialog);

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
    {
        gchar *target = g_path_get_basename (gtk_entry_get_text (GTK_ENTRY (entry_target)));
        gchar *target_uri = g_build_filename (path, target, NULL);

        trace("rename %s -> %s\n", source_uri, target_uri);
        gint success = g_rename (source_uri, target_uri);

        if (success != 0)
        {
            GtkWidget *error = gtk_message_dialog_new (
                    GTK_WINDOW (gtkui_plugin->get_mainwin ()),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_CLOSE,
                    _("Failed to rename %s!\n\n%s\n\t==>\n%s"),
                    is_dir ? _("directory") : _("file"),
                    source_uri, target_uri);

            gtk_dialog_run (GTK_DIALOG (error));
            gtk_widget_destroy (error);
        }
        else
        {
            treebrowser_chroot (addressbar_last_address);  // update treeview
        }

        g_free (target_uri);
        g_free (target);
    }

    g_free (source);
    g_free (path);

    gtk_widget_destroy (dialog);
}
#endif

#if GTK_CHECK_VERSION(3,16,0)
static void
on_menu_config (GtkMenuItem *menuitem, gpointer user_data)
{
    create_settings_dialog ();
}
#endif


/*----------------*/
/* TOOLBAR EVENTS */
/*----------------*/


static void
on_button_add_current (void)
{
    GtkTreeSelection *selection;
    GList *rows, *uri_list;

    // Get URI for current selection
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
    rows = gtk_tree_selection_get_selected_rows (selection, NULL);

    uri_list = g_list_alloc ();
    g_list_foreach (rows, (GFunc) get_uris_from_selection, uri_list);
    g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
    g_list_free (rows);

    add_uri_to_playlist (uri_list, PLT_CURRENT, TRUE, TRUE);  // append
}

static void
on_button_replace_current (void)
{
    GtkTreeSelection *selection;
    GList *rows, *uri_list;

    // Get URI for current selection
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
    rows = gtk_tree_selection_get_selected_rows (selection, NULL);

    uri_list = g_list_alloc ();
    g_list_foreach (rows, (GFunc) get_uris_from_selection, uri_list);
    g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
    g_list_free (rows);

    add_uri_to_playlist (uri_list, PLT_CURRENT, FALSE, TRUE);  // replace
}

static void
on_button_refresh (void)
{
    treebrowser_chroot (addressbar_last_address);
}

static void
on_button_go_up (void)
{
    gchar *uri = g_path_get_dirname (addressbar_last_address);
    treebrowser_chroot (uri);
    g_free (uri);
}

static void
on_button_go_home (void)
{
    gchar *path = g_strdup (g_getenv ("HOME"));
    treebrowser_chroot (path);
    g_free (path);
}

static void
on_button_go_default (void)
{
    gchar *path = get_default_dir ();
    treebrowser_chroot (path);
    g_free (path);
}

static void
on_addressbar_changed ()
{
    trace("signal: adressbar changed\n");

    gchar *uri = g_strdup( gtk_entry_get_text (GTK_ENTRY (gtk_bin_get_child( GTK_BIN (addressbar)))));
    treebrowser_chroot (uri);
    g_free (uri);
}

static gboolean
on_searchbar_timeout ()
{
    if (last_searchbar_change == 0)
        return FALSE;

    // avoid calling this function too often as it is quite expensive
#if GLIB_CHECK_VERSION(2, 28, 0)
    gint64 now = g_get_monotonic_time ();
#else
    GTimeVal time_now;
    g_get_current_time (&time_now);
    gint64 now = 1000000L * time_now.tv_sec + time_now.tv_usec;
#endif
    if (now - last_searchbar_change < 1000*CONFIG_SEARCH_DELAY)  // time given in usec
        return TRUE;
    last_searchbar_change = 0;

    // make the current search text public
    if (searchbar_text)
        g_free (searchbar_text);
    searchbar_text = g_strdup (gtk_entry_get_text (GTK_ENTRY (searchbar)));

    trace("starting search: %s (%lu chars, full search at %d chars)\n", searchbar_text, strlen (searchbar_text), CONFIG_FULLSEARCH_WAIT);

    if (strlen (searchbar_text) >= CONFIG_FULLSEARCH_WAIT) {
        // expand all tree items to search everywhere (this can take a loooooong time)
        if (! all_expanded)
        {
            expand_all ();
        }
        all_expanded = TRUE;
    }
    else
    {
        if (all_expanded)
        {
            treeview_clear_expanded ();
            collapse_all ();
            load_config_expanded_rows ();  // to make things easy we just load the config setting again
            treeview_restore_expanded (NULL);
        }
        all_expanded = FALSE;
    }

    treebrowser_chroot (addressbar_last_address);
    return FALSE;  // stop timeout
}

static void
on_searchbar_changed ()
{
    if (last_searchbar_change == 0)
    {
#if GLIB_CHECK_VERSION(2, 28, 0)
        last_searchbar_change = g_get_monotonic_time ();
#else
        GTimeVal time_now;
        g_get_current_time (&time_now);
        last_searchbar_change = 1000000L * time_now.tv_sec + time_now.tv_usec;
#endif
    }

    g_timeout_add (100, on_searchbar_timeout, NULL);
}

#if !GTK_CHECK_VERSION(3,6,0)
static void
on_searchbar_cleared ()
{
    gtk_entry_set_text (GTK_ENTRY (searchbar), "");
}
#endif


/*-----------------*/
/* TREEVIEW EVENTS */
/*-----------------*/


static void
treeview_activate (GtkTreePath *path, GtkTreeViewColumn *column,
GtkTreeSelection *selection, gboolean create, gboolean append, gboolean play)
{
    gint selected_rows = gtk_tree_selection_count_selected_rows (selection);
    gboolean is_selected = path ? gtk_tree_selection_path_is_selected (selection, path) : FALSE;

    if (path)
    {
        if (selected_rows < 1)
            gtk_tree_selection_select_path (selection, path);
        if (! is_selected)
            gtk_tree_view_set_cursor (GTK_TREE_VIEW (treeview), path, column, FALSE);
    }

    GList *rows, *uri_list;
    uri_list = g_list_alloc ();
    rows = gtk_tree_selection_get_selected_rows (selection, NULL);
    g_list_foreach (rows, (GFunc)get_uris_from_selection, uri_list);
    g_list_foreach (rows, (GFunc)gtk_tree_path_free, NULL);
    g_list_free (rows);

    gint plt = create ? PLT_NEW : PLT_CURRENT;

    if (! play)
    {
        add_uri_to_playlist (uri_list, plt, append, TRUE);  // append
    }
    else
    {
        add_uri_to_playlist (uri_list, plt, FALSE, FALSE);  // append/replace - no threads here!
        deadbeef->sendmessage (DB_EV_PLAY_NUM, 0, 0, 0);
    }
}

static gboolean
on_treeview_key_press (GtkWidget *widget, GdkEventKey *event,
                GtkTreeSelection *selection)
{
    if (gtkui_plugin->w_get_design_mode ())
    {
        return FALSE;
    }

    GtkTreePath         *path;
    GtkTreeViewColumn   *column;
    gtk_tree_view_get_cursor (GTK_TREE_VIEW (treeview), &path, &column);

    gboolean is_expanded = path ? gtk_tree_view_row_expanded (GTK_TREE_VIEW (treeview), path) : FALSE;

    if (event->keyval == GDK_KEY_Return)
    {
        treeview_activate(path, column, selection,
            (event->state & GDK_SHIFT_MASK),  // shift=create
            FALSE,  // always replace
            ! (event->state & GDK_CONTROL_MASK));  // ctrl=silent

        return TRUE;
    }
    else if (event->keyval == GDK_KEY_Left)
    {
        if (is_expanded)
            gtk_tree_view_collapse_row (GTK_TREE_VIEW (treeview), path);
        else if (gtk_tree_path_get_depth (path) > 1)
            gtk_tree_path_up (path);
        gtk_tree_view_set_cursor (GTK_TREE_VIEW (treeview), path, column, FALSE);

        return TRUE;
    }
    else if (event->keyval == GDK_KEY_Right)
    {
        if (! is_expanded)
            gtk_tree_view_expand_row (GTK_TREE_VIEW (treeview), path, FALSE);
        else
            gtk_tree_path_down (path);
        gtk_tree_view_set_cursor (GTK_TREE_VIEW (treeview), path, column, FALSE);

        return TRUE;
    }

    return FALSE;
}

static gboolean
on_treeview_mouseclick_press (GtkWidget *widget, GdkEventButton *event,
                GtkTreeSelection *selection)
{
    if (gtkui_plugin->w_get_design_mode ())
    {
        return FALSE;
    }

    GtkTreePath         *path;
    GtkTreeViewColumn   *column;
    gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview), event->x, event->y,
                    &path, &column, NULL, NULL);

    mouseclick_lastpos[0] = event->x;
    mouseclick_lastpos[1] = event->y;
    mouseclick_dragwait = FALSE;

    gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

    gint selected_rows = gtk_tree_selection_count_selected_rows (selection);
    gboolean is_selected = path ? gtk_tree_selection_path_is_selected (selection, path) : FALSE;
    gboolean is_expanded = path ? gtk_tree_view_row_expanded (GTK_TREE_VIEW (treeview), path) : FALSE;

    gtk_widget_grab_focus(treeview);

    if (event->button == 1)
    {
        if (! path)
        {
            gtk_tree_selection_unselect_all (selection);

            return TRUE;
        }

        // expand/collapse by single-click on icon/expander
        if (event->type == GDK_BUTTON_PRESS && column == treeview_column_icon)
        {
            // toggle expand/collapse
            if (is_expanded)
                gtk_tree_view_collapse_row (GTK_TREE_VIEW (treeview), path);
            else
                gtk_tree_view_expand_row (GTK_TREE_VIEW (treeview), path, FALSE);
            gtk_tree_view_set_cursor (GTK_TREE_VIEW (treeview), path, column, FALSE);

            return TRUE;
        }
        // add items by double-click on item
        else if (event->type == GDK_2BUTTON_PRESS)
        {
            gtk_tree_selection_select_path (selection, path);

            treeview_activate(path, column, selection,
                FALSE, FALSE,  // replace current
                TRUE);  // play

            return TRUE;
        }
        // select + drag/drop by click on item
        else if (event->type == GDK_BUTTON_PRESS)
        {
            mouseclick_dragwait = TRUE;
            if (! (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))
            {
                if (selected_rows <= 1 || ! is_selected)
                {
                    // select row
                    gtk_tree_view_set_cursor (GTK_TREE_VIEW (treeview), path, column, FALSE);
                }

                return TRUE;
            }
            else if (event->state & GDK_SHIFT_MASK)
            {
                // add to selection
                if (mouseclick_lastpath != NULL)
                {
                    gint depth = gtk_tree_path_get_depth (path);
                    gint last_depth = gtk_tree_path_get_depth (mouseclick_lastpath);
                    if (depth == last_depth)
                    {
                        // FIXME: selecting over different depths leads to segfault!
                        gtk_tree_selection_select_range (selection, mouseclick_lastpath, path);
                    }
                }

                return TRUE;
            }
            else if (event->state & GDK_CONTROL_MASK)
            {
                trace("mouse_press[ctrl]: is_selected=%d\n", is_selected);
                // toggle selection
                if (is_selected)
                    gtk_tree_selection_unselect_path (selection, path);
                else
                    gtk_tree_selection_select_path (selection, path);

                return TRUE;
            }
        }
    }
    else if (event->button == 2)
    {
        treeview_activate(path, column, selection,
            (event->state & GDK_SHIFT_MASK),  // shift=create
            TRUE,  // always append
            (event->state & GDK_CONTROL_MASK));  // ctrl=play

        return TRUE;
    }
    else if (event->button == 3)
    {
        // show popup menu by right-click
        if (event->type == GDK_BUTTON_PRESS)
        {
            if (path)
            {
                if (selected_rows < 1)
                    gtk_tree_selection_select_path (selection, path);
                if (! is_selected)
                    gtk_tree_view_set_cursor (GTK_TREE_VIEW (treeview), path, column, FALSE);
            }

            GList *rows, *uri_list;
            uri_list = g_list_alloc ();
            rows = gtk_tree_selection_get_selected_rows (selection, NULL);
            g_list_foreach (rows, (GFunc)get_uris_from_selection, uri_list);
            g_list_foreach (rows, (GFunc)gtk_tree_path_free, NULL);
            g_list_free (rows);

            GtkWidget *menu;
            menu = create_popup_menu (path, "", uri_list);

            // create new accel group to avoid conflicts with main window
            gtk_menu_set_accel_group (GTK_MENU (menu), gtk_accel_group_new ());
#if GTK_CHECK_VERSION(3,22,0)
            gtk_menu_popup_at_pointer (GTK_MENU (menu), NULL);
#else
            gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, event->button, event->time);
#endif

            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
on_treeview_mouseclick_release (GtkWidget *widget, GdkEventButton *event,
                GtkTreeSelection *selection)
{
    if (gtkui_plugin->w_get_design_mode ())
    {
        return FALSE;
    }

    GtkTreePath         *path;
    GtkTreeViewColumn   *column;
    gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview), event->x, event->y,
                    &path, &column, NULL, NULL);

    //gint selected_rows = gtk_tree_selection_count_selected_rows (selection);

    if (event->button == 1)
    {
        if (! path)
        {
            mouseclick_lastpath = NULL;
            gtk_tree_selection_unselect_all (selection);

            return TRUE;
        }

        if (mouseclick_dragwait)
        {
            mouseclick_dragwait = FALSE;
            if (! (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))
            {
                // select row (abort drag)
                gtk_tree_view_set_cursor (GTK_TREE_VIEW (treeview), path, column, FALSE);

                return TRUE;
            }
        }

        mouseclick_lastpath = path;
    }

    return FALSE;
}

static gboolean
on_treeview_mousemove (GtkWidget *widget, GdkEventButton *event)
{
    if (gtkui_plugin->w_get_design_mode ())
    {
        return FALSE;
    }

    if (mouseclick_dragwait)
    {
        if (gtk_drag_check_threshold (widget, mouseclick_lastpos[0], mouseclick_lastpos[1], event->x, event->y))
        {
            mouseclick_dragwait = FALSE;
            GtkTargetEntry entry =
            {
                .target = "text/uri-list",
                .flags = GTK_TARGET_SAME_APP,
                .info = 0
            };
            GtkTargetList *target = gtk_target_list_new (&entry, 1);
#if !GTK_CHECK_VERSION(3,10,0)
            gtk_drag_begin (widget, target, GDK_ACTION_COPY | GDK_ACTION_MOVE, 1, (GdkEvent *)event);
#else
            gtk_drag_begin_with_coordinates (widget, target, GDK_ACTION_COPY | GDK_ACTION_MOVE, 1, (GdkEvent *)event, -1, -1);
#endif
        }
    }

    return TRUE;
}

static void
on_treeview_changed (GtkWidget *widget, gpointer user_data)
{
    gboolean has_selection = FALSE;

    if (gtk_tree_selection_count_selected_rows (GTK_TREE_SELECTION (widget)) > 0)
        has_selection = TRUE;

    if (toolbar_button_add)
        gtk_widget_set_sensitive (GTK_WIDGET (toolbar_button_add), has_selection);
    if (toolbar_button_replace)
        gtk_widget_set_sensitive (GTK_WIDGET (toolbar_button_replace), has_selection);
}

static void
on_treeview_row_expanded (GtkWidget *widget, GtkTreeIter *iter,
                GtkTreePath *path, gpointer user_data)
{
    gchar *uri;

    gtk_tree_model_get (GTK_TREE_MODEL (treestore), iter,
                    TREEBROWSER_COLUMN_URI, &uri, -1);
    if (uri == NULL)
        return;

    if (flag_on_expand_refresh == FALSE)
    {
        flag_on_expand_refresh = TRUE;
        treebrowser_browse (uri, iter);
        gtk_tree_view_expand_row (GTK_TREE_VIEW (treeview), path, FALSE);
        flag_on_expand_refresh = FALSE;
    }

    GSList *node = treeview_check_expanded (uri);
    if (! node)
    {
        gchar *enc_uri = g_filename_to_uri (uri, NULL, NULL);
        expanded_rows = g_slist_append (expanded_rows, enc_uri);
    }

    g_free (uri);
}

static void
on_treeview_row_collapsed (GtkWidget *widget, GtkTreeIter *iter,
                GtkTreePath *path, gpointer user_data)
{
    gchar *uri;

    gtk_tree_model_get (GTK_TREE_MODEL (treestore), iter,
                    TREEBROWSER_COLUMN_URI, &uri, -1);
    if (! uri)
        return;

    GSList *node = treeview_check_expanded (uri);
    if (node)
    {
        g_free (node->data);
        expanded_rows = g_slist_delete_link (expanded_rows, node);
    }

    g_free (uri);
}


/*-------------------------------*/
/* TREEBROWSER INITIAL FUNCTIONS */
/*-------------------------------*/


static gboolean
treeview_update (void *ctx)
{
    trace("update treeview\n");

    update_rootdirs ();
    treebrowser_chroot (addressbar_last_address);  // update treeview

    // This function MUST return false because it's called from g_idle_add ()
    return FALSE;
}

static gboolean
libbrowser_init (void *ctx)
{
    trace("init libbrowser\n");

    if (CONFIG_ENABLED)
        libbrowser_startup ((GtkWidget *)ctx);

    // This function MUST return false because it's called from g_idle_add ()
    return FALSE;
}

static int
plugin_init (void)
{
    trace ("init\n");

    if (! expanded_rows)
        expanded_rows = g_slist_alloc ();

    //treebrowser_mutex = deadbeef->mutex_create ();

    create_autofilter ();
    treeview_update (NULL);

    utils_construct_style (treeview, CONFIG_COLOR_BG, CONFIG_COLOR_FG, CONFIG_COLOR_BG_SEL, CONFIG_COLOR_FG_SEL);

    return 0;
}

static int
plugin_cleanup (void)
{
    trace ("cleanup\n");

    treeview_clear_expanded ();

    //if (treebrowser_mutex)
    //    deadbeef->mutex_free (treebrowser_mutex);

    if (expanded_rows)
        g_slist_free (expanded_rows);

    g_free (addressbar_last_address);
    g_free (known_extensions);

    expanded_rows = NULL;
    addressbar_last_address = NULL;
    known_extensions = NULL;

    return 0;
}


/*---------------------------*/
/* EXPORTED PUBLIC FUNCTIONS */
/*---------------------------*/


int
libbrowser_start (void)
{
    trace("start\n");

    load_config ();

    return 0;
}

int
libbrowser_stop (void)
{
    trace("stop\n");

    save_config ();

    if (CONFIG_DEFAULT_PATH)
        g_free ((gchar*) CONFIG_DEFAULT_PATH);
    if (CONFIG_FILTER)
        g_free ((gchar*) CONFIG_FILTER);
    if (CONFIG_COVERART)
        g_free ((gchar*) CONFIG_COVERART);

    return 0;
}

int
libbrowser_startup (GtkWidget *cont)
{
#if DDB_GTKUI_API_VERSION_MAJOR >= 2
    if (! cont)
        return -1;
#endif

    trace("startup\n");

    if (create_interface (cont) < 0)
        return -1;

    if (plugin.plugin.message)
        create_menu_entry ();  // don't disable plugin in case menu couldn't be created

    setup_dragdrop ();

    return plugin_init ();
}

int
libbrowser_shutdown (GtkWidget *cont)
{
#if DDB_GTKUI_API_VERSION_MAJOR >= 2
    if (! cont)
        return -1;
#endif

    trace("shutdown\n");

    if (restore_interface (cont) < 0)
        return -1;

    if (mainmenuitem)
        gtk_widget_destroy (mainmenuitem);

    return plugin_cleanup ();
}

#if DDB_GTKUI_API_VERSION_MAJOR >= 2
typedef struct
{
    ddb_gtkui_widget_t base;
} w_libbrowser_t;

static int
w_handle_message (ddb_gtkui_widget_t *w, uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2)
{
    return handle_message (id, ctx, p1, p2);
}

static ddb_gtkui_widget_t *
w_libbrowser_create (void)
{
    w_libbrowser_t *w = malloc (sizeof (w_libbrowser_t));
    memset (w, 0, sizeof (w_libbrowser_t));
    w->base.widget = gtk_event_box_new ();
    w->base.message = w_handle_message;
    gtk_widget_set_can_focus (w->base.widget, FALSE);

    CONFIG_ENABLED = 1;
    libbrowser_init (w->base.widget);

    gtkui_plugin->w_override_signals (w->base.widget, w);

    return (ddb_gtkui_widget_t *)w;
}
#endif

int
libbrowser_connect (void)
{
    trace("connect\n");

#if DDB_GTKUI_API_VERSION_MAJOR >= 2
    gtkui_plugin = (ddb_gtkui_t *) deadbeef->plug_get_for_id (DDB_GTKUI_PLUGIN_ID);
    if (gtkui_plugin)
    {
        trace("using '%s' plugin %d.%d\n", DDB_GTKUI_PLUGIN_ID, gtkui_plugin->gui.plugin.version_major, gtkui_plugin->gui.plugin.version_minor);

        if (gtkui_plugin->gui.plugin.version_major == 2)
        {
            // 0.6+, use the new widget API
            gtkui_plugin->w_reg_widget (_("Library browser"), DDB_WF_SINGLE_INSTANCE, w_libbrowser_create, "libbrowser", NULL);
            return 0;
        }
    }
    else
    {
        trace("error: could not find '%s' plugin (gtkui api version %d.%d)!\n", DDB_GTKUI_PLUGIN_ID, DDB_GTKUI_API_VERSION_MAJOR, DDB_GTKUI_API_VERSION_MINOR);
    }
#endif  // DDB_GTKUI_API_VERSION_MAJOR

    // 0.5 compatibility
    trace("trying to fall back to 0.5 api!\n");

    gtkui_plugin = (ddb_gtkui_t *) deadbeef->plug_get_for_id ("gtkui3");
    if (gtkui_plugin)
    {
        trace("using '%s' plugin %d.%d\n", DDB_GTKUI_PLUGIN_ID, gtkui_plugin->gui.plugin.version_major, gtkui_plugin->gui.plugin.version_minor);
        if (gtkui_plugin->gui.plugin.version_major == 1)
        {
            printf ("fb api1\n");
            plugin.plugin.message = handle_message;
            g_idle_add (libbrowser_init, NULL);
            return 0;
        }
    }

    return -1;
}

int
libbrowser_disconnect (void)
{
    trace("disconnect\n");

    if (gtkui_plugin && gtkui_plugin->gui.plugin.version_major == 1)
    {
        if (CONFIG_ENABLED)
            plugin_cleanup ();
    }

    gtkui_plugin = NULL;
    return 0;
}


static const char settings_dlg[] =
    "property \"Default path: \"                entry "                 CONFSTR_FB_DEFAULT_PATH         " \"" DEFAULT_FB_DEFAULT_PATH   "\" ;\n"
    "property \"Filter files by extension\"     checkbox "              CONFSTR_FB_FILTER_ENABLED       " 1 ;\n"
    "property \"Filter for shown files: \"      entry "                 CONFSTR_FB_FILTER               " \"" DEFAULT_FB_FILTER         "\" ;\n"
    "property \"Use auto-filter instead (based on active decoder plugins)\" "
                                               "checkbox "              CONFSTR_FB_FILTER_AUTO          " 1 ;\n"
    "property \"Extra bookmarks file (GTK format): \""
                                               "entry "                 CONFSTR_FB_BOOKMARKS_FILE       " \"" DEFAULT_FB_BOOKMARKS_FILE "\" ;\n"
    "property \"Search delay (do not update tree while typing)\" "
                                               "spinbtn[100,5000,100] " CONFSTR_FB_SEARCH_DELAY         " 1000 ;\n"
    "property \"Wait for N chars until full search (fully expand tree)\" "
                                               "spinbtn[1,10,1]       " CONFSTR_FB_FULLSEARCH_WAIT      " 5 ;\n"
    "property \"Sort contents by name (otherwise by modification date) \" "
                                               "checkbox "              CONFSTR_FB_SORT_TREEVIEW        " 1 ;\n"
    "property \"Show tree lines\"               checkbox "              CONFSTR_FB_SHOW_TREE_LINES      " 0 ;\n"
    "property \"Font size: \"                   spinbtn[0,32,1] "       CONFSTR_FB_FONT_SIZE            " 0 ;\n"
    "property \"Icon size (non-coverart): \"    spinbtn[24,48,2] "      CONFSTR_FB_ICON_SIZE            " 24 ;\n"
    "property \"Show coverart icons: \"         checkbox "              CONFSTR_FB_SHOW_COVERART        " 1 ;\n"
    "property \"Coverart size: \"               spinbtn[24,48,2] "      CONFSTR_FB_COVERART_SIZE        " 24 ;\n"
    "property \"Filter for coverart files: \"   entry "                 CONFSTR_FB_COVERART             " \"" DEFAULT_FB_COVERART       "\" ;\n"
    "property \"Sidebar width: \"               spinbtn[150,300,1] "    CONFSTR_FB_WIDTH                " 200 ;\n"
    "property \"Save treeview over sessions (restore previously expanded items)\" "
                                               "checkbox "              CONFSTR_FB_SAVE_TREEVIEW        " 1 ;\n"
    "property \"Background color: \"            entry "                 CONFSTR_FB_COLOR_BG             " \"\" ;\n"
    "property \"Foreground color: \"            entry "                 CONFSTR_FB_COLOR_FG             " \"\" ;\n"
    "property \"Background color (selected): \" entry "                 CONFSTR_FB_COLOR_BG_SEL         " \"\" ;\n"
    "property \"Foreground color (selected): \" entry "                 CONFSTR_FB_COLOR_FG_SEL         " \"\" ;\n"
;

static DB_misc_t plugin =
{
    DDB_REQUIRE_API_VERSION(1,0)
    .plugin.type            = DB_PLUGIN_MISC,
    .plugin.version_major   = 0,
    .plugin.version_minor   = 1,
#if GTK_CHECK_VERSION(3,0,0)
    .plugin.id              = "libbrowser-gtk3",
#else
    .plugin.id              = "libbrowser",
#endif
    .plugin.name            = "Library Browser",
    .plugin.descr           =
       //0.........1.........2.........3.........4.........5.........6.........7.......::8
        "Simple library browser, based on Geany's treebrowser plugin\n"
        "\n"
        "Project homepage: http://sourceforge.net/projects/deadbeef-fb\n"
        "Issue tracker: https://gitlab.com/zykure/deadbeef-fb/issues\n"
        "\n"
        "BOOKMARKS:\n"
        "If you don't want to use GTK bookmarks, you can create your own\n"
        "bookmarks file to be used only by the libbrowser\n"
        "(default file: ~/.config/deadbeef/bookmarks).\n"
        "\n"
        "SEARCH BEHAVIOR:\n"
        "By default, the search bar filters the items visible in the tree by their full\n"
        "path. When a search text is entered, only those items that contain the text\n"
        "in their path are shown. When the search is cleared, all items are shown\n"
        "again. The tree is expanded fully after some number of characters have\n"
        "been entered into the search bar. Then the libbrowser will traverse the\n"
        "full directory tree and check every file inside it. Note that this can take\n"
        "a long time on large trees. The number of characters to trigger this behavior\n"
        "can be adjusted in the options (default is 5 characters).\n"
        "\n"
        "COVERART ICONS:\n"
        "Each directory that is shown in the tree is searched for a coverart image\n"
        "must have a specific name (like 'cover.jpg'). If it is found, it is shown\n"
        "instead of the default folder icon. To speed things up, the thumbnail image\n"
        "is stored to disk inside DeaDBeeF's cache directory.\n"
        "\n"
        "MOUSECLICK ACTIONS:\n"
        "There are different mouse-button actions when you click on a tree item:\n"
        "\t* left-click to select (drag&drop to add to playlist)\n"
        "\t\t* ctrl + left-click to multi-select\n"
        "\t\t* shift + left-click to range-select (only works on same tree level)\n"
        "\t* middle-click to replace append to playlist\n"
        "\t\t* ctrl + middle-click to replace current playlist & start playing\n"
        "\t\t* shift + middle-click to create new playlist\n"
        "\t\t* ctrl + shift + middle-click to create new playlist & start playing\n"
        "\t* right-click for popup menu\n"
        "\t* double-click to replace current playlist & start playing\n"
        "\t\t* ctrl + double-click to replace current playlist\n"
        "\n"
        "KEYPRESS ACTIONS:\n"
        "It is also possible to execute key-press actions inside the tree view:\n"
        "\t* left/right to navigate along tree hierarchy (move & expand/collapse)\n"
        "\t* return to replace current playlist & start playing\n"
        "\t\t* ctrl + return to replace current playlist\n"
        "\t\t* shift + return to create new playlist & start playing\n"

       //0.........1.........2.........3.........4.........5.........6.........7.......::8
    ,
    .plugin.copyright       =
        "Copyright (C) 2011-2016 Jan D. Behrens <zykure@web.de>\n"
        "\n"
        "Contributions by:\n"
        "  Tobias Bengfort <tobias.bengfort@posteo.de>\n"
        "  420MuNkEy\n"
        "\n"
        "Based on the Geany treebrowser plugin by Adrian Dimitrov.\n"
        "\n"
        "\n"
        "This program is free software; you can redistribute it and/or\n"
        "modify it under the terms of the GNU General Public License\n"
        "as published by the Free Software Foundation; either version 2\n"
        "of the License, or (at your option) any later version.\n"
        "\n"
        "This program is distributed in the hope that it will be useful,\n"
        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
        "GNU General Public License for more details.\n"
        "\n"
        "You should have received a copy of the GNU General Public License\n"
        "along with this program; if not, write to the Free Software\n"
        "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n"
    ,
    .plugin.website         = "https://github.com/mctofu/deadbeef-libbrowser/",
    .plugin.start           = libbrowser_start,
    .plugin.stop            = libbrowser_stop,
    .plugin.connect         = libbrowser_connect,
    .plugin.disconnect      = libbrowser_disconnect,
    .plugin.configdialog    = settings_dlg,
};

DB_plugin_t *
ddb_misc_libbrowser_GTK3_load (DB_functions_t *ddb)
{
    trace("load attempt");
    deadbeef = ddb;
    return &plugin.plugin;
}

/*-------------*/
/* END OF FILE */
/*-------------*/
