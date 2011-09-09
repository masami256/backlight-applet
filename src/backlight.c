/**
 * Copyright (c) 2011  Masami Ichikawa <masami256@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * I use xbackligth and lxpanel-plugins as a referrence.
 * [1] xbacklight: http://cgit.freedesktop.org/xorg/app/xbacklight
 * [2] lxde: http://lxde.git.sourceforge.net/git/gitweb.cgi?p=lxde/lxpanel-plugins;a=summary
 */

#include <gtk/gtk.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <lxpanel/plugin.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#define BACKLIGHT_ICON "/usr/share/icons/hicolor/22x22/apps/bklight.png"

typedef struct {
     Plugin* plugin;
     GtkWidget *mainw;
     GtkWidget *tray_icon;
     GtkWidget *dlg;
     GtkWidget *vscale;
     guint vscale_handler;
     int show;
     Display *display;
     Atom backlight_new;
     Atom backlight_legacy;
     Atom backlight_cur;
     double brightness;
     double next_value;
} backlight_t;

enum {
     GetBrightness = 0,
     SetBrightness,
};

struct backlight_property {
     Atom actual_type;
     int actual_format;
     unsigned long nitems;
     unsigned long bytes_after;
     unsigned char *prop;
};
     
static inline int set_property(backlight_t *bklight, RROutput output, 
			struct backlight_property *property)
{
     return XRRGetOutputProperty(bklight->display, output, bklight->backlight_cur,
				 0, 4, False, False, None,
				 &property->actual_type, &property->actual_format,
				 &property->nitems, &property->bytes_after, 
				 &property->prop);
}

static long backlight_get(backlight_t *bklight, RROutput output)
{
     struct backlight_property prop = { 0 };
     long value = 0;
    
     bklight->backlight_cur = bklight->backlight_new;
     if (!bklight->backlight_cur ||  set_property(bklight, output, &prop) != Success) {
	  bklight->backlight_cur = bklight->backlight_legacy;
	  if (!bklight->backlight_cur || set_property(bklight, output, &prop) != Success)
	       return -1;
     }

     if (prop.actual_type != XA_INTEGER || prop.nitems != 1 || prop.actual_format != 32)
	  value = -1;
     else
	  value = *((long *) prop.prop);
     XFree(prop.prop);
     return value;
}

static void backlight_set(backlight_t *bklight, RROutput output, long value)
{
     XRRChangeOutputProperty(bklight->display, output, bklight->backlight_cur, XA_INTEGER, 32,
			     PropModeReplace, (unsigned char *) &value, 1);
}

static void property_operation(backlight_t *bklight, double cur, RROutput output, int operation)
{
     XRRPropertyInfo *info;
     double min, max;

     if ((info = XRRQueryOutputProperty(bklight->display, output, bklight->backlight_cur)) != NULL) {
	  if (info->range && info->num_values == 2) {
	       min = info->values[0];
	       max = info->values[1];
	       if (operation == SetBrightness) {
		    double tmp = bklight->next_value * (max - min) / 100;
		    double new = min + tmp;

		    if (new > max) 
			 new = max;
		    if (new < min) 
			 new = min;

		    backlight_set (bklight, output, (long) new);
		    XFlush(bklight->display);
		    usleep (200);

	       }
	       bklight->brightness = (cur - min) * 100 / (max - min);
	  }
	  XFree(info);	  
     }
		 		      
}

static void walk_each_screen(backlight_t *bklight, XRRScreenResources *resources, int operation)
{
     int i = 0;

     for (i = 0; i < resources->noutput; i++) {
	    RROutput output = resources->outputs[i];
	    double cur = 0.0;

	    cur = backlight_get(bklight, output);

	    if (cur != -1) {
		 property_operation(bklight, cur, output, operation);
	    }
     }
}

static void screen_walker(backlight_t *bklight, int operation)
{
     int i = 0;
     int count = ScreenCount(bklight->display);

     for (i = 0; i < count; i++) {
	  Window root = RootWindow(bklight->display, i);
	  XRRScreenResources *resources = XRRGetScreenResources(bklight->display, root);

	  if (resources) {
	       walk_each_screen(bklight, resources, operation);
	       XRRFreeScreenResources(resources);
	    
	  }
     }
     XSync(bklight->display, False);
}

static gboolean get_display(backlight_t *bklight)
{
     bklight->display = XOpenDisplay(NULL);
     if (!bklight->display)
	  g_print("Cannot open display.\n");
     
     return bklight->display ? TRUE : FALSE;
}

static gboolean check_version(backlight_t *bklight)
{
     int major, minor;
     major = minor = 0;
     
     if (!XRRQueryVersion (bklight->display, &major, &minor)) {
	  g_print("Your system doesn't have render extension\n");
	  return FALSE;
     }
     
     if (major < 1 || (major == 1 && minor < 2)) {
	  g_print("your render extension version %d.%d too old\n", major, minor);
	  return FALSE;
     }     
     return TRUE;
}

static gboolean has_backlight_property(backlight_t *bklight)
{
     gboolean ret = TRUE;
     bklight->backlight_new = XInternAtom (bklight->display, "Backlight", True);
     bklight->backlight_legacy = XInternAtom (bklight->display, "BACKLIGHT", True);

     if (bklight->backlight_new == None && bklight->backlight_legacy == None) {
	  g_print("no outputs have backlight property\n");
	  ret = FALSE;
     }     
     return ret;
}

static gboolean open_display_and_check(backlight_t *bklight)
{
     if (!get_display(bklight))
	  return FALSE;

     if (!check_version(bklight))
	  return FALSE;

     if (!has_backlight_property(bklight))
	  return FALSE;

     return TRUE;

}

static int get_brightness_value(backlight_t *bklight)
{
     open_display_and_check(bklight);
     screen_walker(bklight, GetBrightness);
     return (int) bklight->brightness;
}

static void set_tooltip_text(backlight_t *bklight)
{
     char s[64] = { 0 };
     g_snprintf(s, sizeof(s), _("Brightness %.1f%%"), bklight->brightness);

     gtk_widget_set_tooltip_text(bklight->mainw, s);
}

static gboolean focus_out_event(GtkWidget *widget, GdkEvent *event, backlight_t *bklight)
{
     gtk_widget_hide(bklight->dlg);
     bklight->show = 0;
     get_brightness_value(bklight);
     set_tooltip_text(bklight);
     return FALSE;
}

static gboolean tray_icon_press(GtkWidget *widget, GdkEventButton *event, backlight_t *bklight)
{
     if( event->button == 3 ) { /* right button */
	  GtkMenu  *popup = lxpanel_get_panel_menu(bklight->plugin->panel, bklight->plugin, FALSE);
	  gtk_menu_popup(popup, NULL, NULL, NULL, NULL, event->button, event->time);
	  return TRUE;
     }
     
     if (bklight->show == 0) {
	  open_display_and_check(bklight);
	  gtk_window_set_position(GTK_WINDOW(bklight->dlg), GTK_WIN_POS_MOUSE);
	  gtk_widget_show_all(bklight->dlg);
	  bklight->show = 1;
     } else {
	  gtk_widget_hide(bklight->dlg);
	  bklight->show = 0;
     }
     get_brightness_value(bklight);
     set_tooltip_text(bklight);
     return TRUE;
}

static void on_vscale_value_changed(GtkRange *range, backlight_t *bklight)
{
     // brightness_change
     bklight->next_value = gtk_range_get_value(range);
     screen_walker(bklight, SetBrightness);
}

static void panel_init(Plugin *p)
{
     backlight_t *bklight = p->priv;
     GtkWidget *scrolledwindow;
     GtkWidget *viewport;
     GtkWidget *box;
     GtkWidget *frame;

     /* set show flags */
     bklight->show = 0;

     /* create a new window */
     bklight->dlg = gtk_window_new(GTK_WINDOW_TOPLEVEL);
     gtk_window_set_decorated(GTK_WINDOW(bklight->dlg), FALSE);
     gtk_container_set_border_width(GTK_CONTAINER(bklight->dlg), 5);
     gtk_window_set_default_size(GTK_WINDOW(bklight->dlg), 80, 140);
     gtk_window_set_skip_taskbar_hint(GTK_WINDOW(bklight->dlg), TRUE);
     gtk_window_set_skip_pager_hint(GTK_WINDOW(bklight->dlg), TRUE);
     gtk_window_set_type_hint(GTK_WINDOW(bklight->dlg), GDK_WINDOW_TYPE_HINT_DIALOG);

     /* Focus-out signal */
      g_signal_connect (G_OBJECT (bklight->dlg), "focus_out_event",
		       G_CALLBACK (focus_out_event), bklight);

     scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
     gtk_container_set_border_width (GTK_CONTAINER (scrolledwindow), 0);
     gtk_widget_show (scrolledwindow);
     gtk_container_add (GTK_CONTAINER (bklight->dlg), scrolledwindow);
     GTK_WIDGET_UNSET_FLAGS (scrolledwindow, GTK_CAN_FOCUS);
     gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scrolledwindow), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
     gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolledwindow), GTK_SHADOW_NONE);

     viewport = gtk_viewport_new (NULL, NULL);
     gtk_container_add (GTK_CONTAINER (scrolledwindow), viewport);
     gtk_viewport_set_shadow_type (GTK_VIEWPORT (viewport), GTK_SHADOW_NONE);
     gtk_widget_show(viewport);

     /* create frame */
     frame = gtk_frame_new(_("Brightness"));
     gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
     gtk_container_add(GTK_CONTAINER(viewport), frame);

     /* create box */
     box = gtk_vbox_new(FALSE, 0);

     /* create controller */
     get_brightness_value(bklight);

     bklight->vscale = gtk_vscale_new(GTK_ADJUSTMENT(gtk_adjustment_new(get_brightness_value(bklight), 0, 100, 1.0, 10.0, 0)));
     gtk_scale_set_draw_value(GTK_SCALE(bklight->vscale), FALSE);
//     gtk_scale_set_value_pos(GTK_SCALE(bklight->vscale), GTK_POS_TOP);
     gtk_range_set_inverted(GTK_RANGE(bklight->vscale), TRUE);

     bklight->vscale_handler = g_signal_connect ((gpointer) bklight->vscale, "value_changed",
						 G_CALLBACK (on_vscale_value_changed),
						 bklight);

     gtk_box_pack_start(GTK_BOX(box), bklight->vscale, TRUE, TRUE, 0);
     gtk_container_add(GTK_CONTAINER(frame), box);

     /* setting background to default */
     gtk_widget_set_style(viewport, p->panel->defstyle);
}

static void backlight_destructor(Plugin *p)
{
     backlight_t *bklight = (backlight_t *) p->priv;

     ENTER;

     if (bklight->display)
	  XSync(bklight->display, False);

     if (bklight->dlg)
	  gtk_widget_destroy(bklight->dlg);

     bklight->display = NULL;

     g_free(bklight);
     RET();
}

static int backlight_constructor(Plugin *p, char **fp)
{
     backlight_t *bklight;
     GdkPixbuf *icon;
     GtkWidget *image;
     GtkIconTheme* theme;
     GtkIconInfo* info;

     ENTER;
     bklight = g_new0(backlight_t, 1);
     bklight->plugin = p;
     g_return_val_if_fail(bklight != NULL, 0);
     p->priv = bklight;

     /* initializing */
     panel_init(p);

     /* main */
     bklight->mainw = gtk_event_box_new();

     gtk_widget_add_events(bklight->mainw, GDK_BUTTON_PRESS_MASK);
     gtk_widget_set_size_request( bklight->mainw, 24, 24 );

     g_signal_connect(G_OBJECT(bklight->mainw), "button-press-event",
		      G_CALLBACK(tray_icon_press), bklight);

     /* tray icon */
     bklight->tray_icon = gtk_image_new();
     gtk_image_set_from_file(GTK_IMAGE(bklight->tray_icon), BACKLIGHT_ICON);

     gtk_container_add(GTK_CONTAINER(bklight->mainw), bklight->tray_icon);

     gtk_widget_show_all(bklight->mainw);

     set_tooltip_text(bklight);

     /* store the created plugin widget in plugin->pwid */
     p->pwid = bklight->mainw;

     RET(1);
}


PluginClass backlight_plugin_class = {
     PLUGINCLASS_VERSIONING,
     
     type : "backlight",
     name : N_("Backlight Control"),
     version: "0.1",
     description : N_("Change backlight brightness"),
     
     constructor : backlight_constructor,
     destructor  : backlight_destructor,
     config : NULL,
     save : NULL,
     panel_configuration_changed : NULL
};
