#include <gtk/gtk.h>

#  define _(String) (String)
/*
 * Public Functions.
 */

/*
 * This function returns a widget in a component created by Glade.
 * Call it with the toplevel widget in the component (i.e. a window/dialog),
 * or alternatively any widget in the component, and the name of the widget
 * you want returned.
 */
GtkWidget*  lookup_widget              (GtkWidget       *widget,
                                        const gchar     *widget_name);


/*
 * Private Functions.
 */

#if GTK_CHECK_VERSION(3,2,0)
#define gtk_vbox_new(homogeneous,spacing) g_object_new(GTK_TYPE_VBOX,"spacing",spacing,"homogeneous",homogeneous?TRUE:FALSE,NULL)
#define gtk_hbox_new(homogeneous,spacing) g_object_new(GTK_TYPE_HBOX,"spacing",spacing,"homogeneous",homogeneous?TRUE:FALSE,NULL)
#define gtk_hpaned_new() gtk_paned_new(GTK_ORIENTATION_HORIZONTAL)
#define gtk_vpaned_new() gtk_paned_new(GTK_ORIENTATION_VERTICAL)
#define gtk_hbutton_box_new() gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL)
#define gtk_vbutton_box_new() gtk_button_box_new(GTK_ORIENTATION_VERTICAL)
#define gtk_hscale_new(adj) gtk_scale_new(GTK_ORIENTATION_HORIZONTAL,adj)
#define gtk_vscale_new(adj) gtk_scale_new(GTK_ORIENTATION_VERTICAL,adj)
#define gtk_hseparator_new() gtk_separator_new(GTK_ORIENTATION_HORIZONTAL)
#define gtk_vseparator_new() gtk_separator_new(GTK_ORIENTATION_VERTICAL)
#define gtk_hscrollbar_new(adj) gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL,adj)
#define gtk_vscrollbar_new(adj) gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL,adj)
#endif
