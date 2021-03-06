/*  $Id$
 *
 *  Copyright 2006-2012 Enrico Tröger <enrico(at)xfce(dot)org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <gtk/gtk.h>

#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4panel/xfce-panel-plugin.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

#include "libdict.h"


typedef struct
{
	DictData *dd;
	XfcePanelPlugin *plugin;

	GtkTooltips *tooltips;

	GtkWidget *panel_button;
	GtkWidget *panel_button_image;
	GtkWidget *box;
} DictPanelData;


static gboolean entry_is_dirty = FALSE;

static GdkPixbuf *dict_plugin_load_and_scale(const guint8 *data, gint dstw, gint dsth)
{
	GdkPixbuf *pb, *pb_scaled;
	gint pb_w, pb_h;

	pb = gdk_pixbuf_new_from_inline(-1, data, FALSE, NULL);
	pb_w = gdk_pixbuf_get_width(pb);
	pb_h = gdk_pixbuf_get_height(pb);

	if (dstw == pb_w && dsth == pb_h)
		return(pb);
	else if (dstw < 0)
		dstw = (dsth * pb_w) / pb_h;
	else if (dsth < 0)
		dsth = (dstw * pb_h) / pb_w;

	pb_scaled = gdk_pixbuf_scale_simple(pb, dstw, dsth, GDK_INTERP_HYPER);
	g_object_unref(G_OBJECT(pb));

	return pb_scaled;
}


static gboolean dict_plugin_panel_set_size(XfcePanelPlugin *plugin, gint wsize, DictPanelData *dpd)
{
	gint size;
	gint bsize = wsize;

#if defined(LIBXFCE4PANEL_CHECK_VERSION) && LIBXFCE4PANEL_CHECK_VERSION(4,9,0)
	bsize /= xfce_panel_plugin_get_nrows(plugin);
#endif

	size = bsize - 2 - (2 * MAX(dpd->panel_button->style->xthickness,
									 dpd->panel_button->style->ythickness));

	dpd->dd->icon = dict_plugin_load_and_scale(dict_gui_get_icon_data(), size, -1);

	gtk_image_set_from_pixbuf(GTK_IMAGE(dpd->panel_button_image), dpd->dd->icon);

#if defined(LIBXFCE4PANEL_CHECK_VERSION) && LIBXFCE4PANEL_CHECK_VERSION(4,9,0)
	if (dpd->dd->show_panel_entry &&
		xfce_panel_plugin_get_mode(dpd->plugin) != XFCE_PANEL_PLUGIN_MODE_VERTICAL)
	{
		xfce_panel_plugin_set_small(plugin, FALSE);
		if (xfce_panel_plugin_get_mode(dpd->plugin) == XFCE_PANEL_PLUGIN_MODE_HORIZONTAL)
			gtk_widget_set_size_request(dpd->dd->panel_entry, dpd->dd->panel_entry_size, -1);
		else
			gtk_widget_set_size_request(dpd->dd->panel_entry, -1, -1);
		gtk_orientable_set_orientation(GTK_ORIENTABLE(dpd->box), xfce_panel_plugin_get_orientation(dpd->plugin));
		gtk_widget_show(dpd->dd->panel_entry);
	}
	else
	{
		gtk_widget_hide(dpd->dd->panel_entry);
		xfce_panel_plugin_set_small(plugin, TRUE);
	}
#else
	if (dpd->dd->show_panel_entry &&
		xfce_panel_plugin_get_orientation(plugin) == GTK_ORIENTATION_HORIZONTAL)
	{
		gtk_widget_show(dpd->dd->panel_entry);
		gtk_widget_set_size_request(dpd->dd->panel_entry, dpd->dd->panel_entry_size, -1);
	}
	else
		gtk_widget_hide(dpd->dd->panel_entry);
#endif

	gtk_widget_set_size_request(dpd->panel_button, bsize, bsize);

	return TRUE;
}


/* TODO remove me, unused
static void dict_toggle_main_window(GtkWidget *button, DictData *dd)
{
	if (GTK_WIDGET_VISIBLE(dd->window))
		gtk_widget_hide(dd->window);
	else
	{
		const gchar *panel_text = "";

		if (dd->panel_entry != NULL)
			panel_text = gtk_entry_get_text(GTK_ENTRY(dd->panel_entry));

		dict_show_main_window(dd);
		if (NZV(panel_text))
		{
			dict_search_word(dd, panel_text);
			gtk_entry_set_text(GTK_ENTRY(dd->main_entry), panel_text);
		}
		gtk_widget_grab_focus(dd->main_entry);
	}
}
*/


static void dict_plugin_panel_button_clicked(GtkWidget *button, DictPanelData *dpd)
{
	if (GTK_WIDGET_VISIBLE(dpd->dd->window))
	{
		/* we must query geometry settings here because position and maximized state
		 * doesn't work when the window is hidden */
		dict_gui_query_geometry(dpd->dd);

		gtk_widget_hide(dpd->dd->window);
	}
	else
	{
		dict_gui_show_main_window(dpd->dd);
		/* Do not search the panel text if it is still the default text */
		if (dpd->dd->show_panel_entry &&
			xfce_panel_plugin_get_orientation(dpd->plugin) == GTK_ORIENTATION_HORIZONTAL &&
			entry_is_dirty)
		{
			const gchar *panel_text = gtk_entry_get_text(GTK_ENTRY(dpd->dd->panel_entry));

			if (NZV(panel_text))
			{
				dict_search_word(dpd->dd, panel_text);
				gtk_entry_set_text(GTK_ENTRY(dpd->dd->main_entry), panel_text);
			}
		}
		gtk_widget_grab_focus(dpd->dd->main_entry);
	}
}


/* Handle user messages (xfce4-dict) */
static gboolean dict_plugin_message_received(GtkWidget *w, GdkEventClient *ev, DictPanelData *dpd)
{
	if (ev->data_format == 8 && strncmp(ev->data.b, "xfdict", 6) == 0)
	{
		gchar flags = ev->data.b[6];
		gchar *tts = ev->data.b + 7;

		dpd->dd->mode_in_use = dict_set_search_mode_from_flags(dpd->dd->mode_in_use, flags);

		if (NZV(tts))
		{
			gtk_entry_set_text(GTK_ENTRY(dpd->dd->main_entry), tts);
			dict_search_word(dpd->dd, tts);
		}
		else if (flags & DICT_FLAGS_FOCUS_PANEL_ENTRY && dpd->dd->show_panel_entry)
		{
			xfce_panel_plugin_focus_widget(dpd->plugin, dpd->dd->panel_entry);
		}
		else
		{
			dict_plugin_panel_button_clicked(NULL, dpd);
		}

		return TRUE;
	}

	return FALSE;
}


static gboolean dict_plugin_set_selection(DictPanelData *dpd)
{
	GdkScreen *gscreen;
	gchar      selection_name[32];
	Atom       selection_atom;
	GtkWidget *win;
	Window     xwin;

	win = gtk_invisible_new();
	gtk_widget_realize(win);
	xwin = GDK_WINDOW_XID(GTK_WIDGET(win)->window);

	gscreen = gtk_widget_get_screen(win);
	g_snprintf(selection_name, sizeof (selection_name),
		XFCE_DICT_SELECTION"%d", gdk_screen_get_number(gscreen));
	selection_atom = XInternAtom(GDK_DISPLAY(), selection_name, False);

	if (XGetSelectionOwner(GDK_DISPLAY(), selection_atom))
	{
		gtk_widget_destroy(win);
		return FALSE;
	}

	XSelectInput(GDK_DISPLAY(), xwin, PropertyChangeMask);
	XSetSelectionOwner(GDK_DISPLAY(), selection_atom, xwin, GDK_CURRENT_TIME);

	g_signal_connect(win, "client-event", G_CALLBACK(dict_plugin_message_received), dpd);

	return TRUE;
}


static void dict_plugin_close_button_clicked(GtkWidget *button, DictPanelData *dpd)
{
	gtk_widget_hide(dpd->dd->window);
}


static void dict_plugin_free_data(XfcePanelPlugin *plugin, DictPanelData *dpd)
{
	/* Destroy the setting dialog, if this open */
	GtkWidget *dialog = g_object_get_data(G_OBJECT(dpd->plugin), "dialog");

	/* if the main window is visible, query geometry as usual, if it is hidden the geometry
	 * was queried when it was hidden */
	if (GTK_WIDGET_VISIBLE(dpd->dd->window))
	{
		dict_gui_query_geometry(dpd->dd);
	}

	if (dialog != NULL)
		gtk_widget_destroy(dialog);

	gtk_object_sink(GTK_OBJECT(dpd->tooltips));

	dict_free_data(dpd->dd);
	g_free(dpd);
}


#if defined(LIBXFCE4PANEL_CHECK_VERSION) && LIBXFCE4PANEL_CHECK_VERSION(4,9,0)
static void dict_plugin_panel_change_mode(XfcePanelPlugin *plugin,
												 XfcePanelPluginMode mode, DictPanelData *dpd)
{
	dict_plugin_panel_set_size(plugin, xfce_panel_plugin_get_size(plugin), dpd);
}

#else
static void dict_plugin_panel_change_orientation(XfcePanelPlugin *plugin,
												 GtkOrientation orientation, DictPanelData *dpd)
{
	dict_plugin_panel_set_size(plugin, xfce_panel_plugin_get_size(plugin), dpd);
}
#endif


static void dict_plugin_style_set(XfcePanelPlugin *plugin, gpointer unused, DictPanelData *dpd)
{
	dict_plugin_panel_set_size(plugin, xfce_panel_plugin_get_size(plugin), dpd);
}


static void dict_plugin_write_rc_file(XfcePanelPlugin *plugin, DictPanelData *dpd)
{
	dict_write_rc_file(dpd->dd);
}


static void dict_plugin_panel_save_settings(DictPanelData *dpd)
{
	dict_plugin_panel_set_size(dpd->plugin, xfce_panel_plugin_get_size(dpd->plugin), dpd);
}


static void dict_plugin_properties_dialog_response(GtkWidget *dlg, gint response, DictPanelData *dpd)
{
	/* first run the real response handler which reads the settings from the dialog */
	dict_prefs_dialog_response(dlg, response, dpd->dd);

	dict_plugin_panel_save_settings(dpd);

	g_object_set_data(G_OBJECT(dpd->plugin), "dialog", NULL);
	xfce_panel_plugin_unblock_menu(dpd->plugin);
}


static void dict_plugin_properties_dialog(GtkWidget *widget, DictPanelData *dpd)
{
	GtkWidget *dlg;
	XfcePanelPlugin *plugin = dpd->plugin;

	xfce_panel_plugin_block_menu(plugin);

	dlg = dict_prefs_dialog_show(gtk_widget_get_toplevel(GTK_WIDGET(plugin)), dpd->dd);

	g_object_set_data(G_OBJECT(dpd->plugin), "dialog", dlg);

	g_signal_connect(dlg, "response", G_CALLBACK(dict_plugin_properties_dialog_response), dpd);

	gtk_widget_show(dlg);
}


static void entry_activate_cb(GtkEntry *entry, DictPanelData *dpd)
{
	const gchar *entered_text = gtk_entry_get_text(GTK_ENTRY(dpd->dd->panel_entry));

	gtk_entry_set_text(GTK_ENTRY(dpd->dd->main_entry), entered_text);

	dict_search_word(dpd->dd, entered_text);
}


static void entry_icon_release_cb(GtkEntry *entry, GtkEntryIconPosition icon_pos,
		GdkEventButton *event, DictPanelData *dpd)
{
	if (event->button != 1)
		return;

	if (icon_pos == GTK_ENTRY_ICON_PRIMARY)
	{
		entry_activate_cb(NULL, dpd);
		gtk_widget_grab_focus(dpd->dd->main_entry);
	}
	else if (icon_pos == GTK_ENTRY_ICON_SECONDARY)
	{
		dict_gui_clear_text_buffer(dpd->dd);
		gtk_entry_set_text(GTK_ENTRY(dpd->dd->main_entry), "");
		dict_gui_set_panel_entry_text(dpd->dd, "");
		dict_gui_status_add(dpd->dd, _("Ready"));
	}
}


static gboolean entry_buttonpress_cb(GtkWidget *entry, GdkEventButton *event, DictPanelData *dpd)
{
	GtkWidget *toplevel;

	if (! entry_is_dirty)
	{
		entry_is_dirty = TRUE;
		if (event->button == 1)
			gtk_entry_set_text(GTK_ENTRY(entry), "");
	}

	/* Determine toplevel parent widget */
	toplevel = gtk_widget_get_toplevel(entry);

	/* Grab entry focus if possible */
	if (event->button != 3 && toplevel && toplevel->window)
		xfce_panel_plugin_focus_widget(dpd->plugin, entry);

	return FALSE;
}


static void entry_changed_cb(GtkEditable *editable, DictPanelData *dpd)
{
	entry_is_dirty = TRUE;
}


static void dict_plugin_drag_data_received(GtkWidget *widget, GdkDragContext *drag_context,
		gint x, gint y, GtkSelectionData *data, guint info, guint ltime, DictPanelData *dpd)
{
	if ((data != NULL) && (data->length >= 0) && (data->format == 8))
	{
		if (widget == dpd->panel_button || widget == dpd->dd->panel_entry)
		{
			gtk_entry_set_text(GTK_ENTRY(dpd->dd->main_entry), (const gchar*) data->data);
		}

		dict_drag_data_received(widget, drag_context, x, y, data, info, ltime, dpd->dd);
	}
}


static void dict_plugin_construct(XfcePanelPlugin *plugin)
{
	DictPanelData *dpd = g_new0(DictPanelData, 1);

	xfce_textdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

	g_thread_init(NULL);

	dpd->dd = dict_create_dictdata();
	dpd->dd->is_plugin = TRUE;
	dpd->plugin = plugin;

	dict_read_rc_file(dpd->dd);

	dpd->panel_button = xfce_create_panel_button();

	dpd->tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(dpd->tooltips, dpd->panel_button, _("Look up a word"), NULL);

	dpd->panel_button_image = gtk_image_new();
	gtk_container_add(GTK_CONTAINER(dpd->panel_button), GTK_WIDGET(dpd->panel_button_image));

	gtk_widget_show_all(dpd->panel_button);

	g_signal_connect(dpd->panel_button, "clicked", G_CALLBACK(dict_plugin_panel_button_clicked), dpd);

	dict_gui_create_main_window(dpd->dd);

	g_signal_connect(dpd->dd->window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
	g_signal_connect(dpd->dd->close_button, "clicked", G_CALLBACK(dict_plugin_close_button_clicked), dpd);
	g_signal_connect(plugin, "free-data", G_CALLBACK(dict_plugin_free_data), dpd);
	g_signal_connect(plugin, "size-changed", G_CALLBACK(dict_plugin_panel_set_size), dpd);
#if defined(LIBXFCE4PANEL_CHECK_VERSION) && LIBXFCE4PANEL_CHECK_VERSION(4,9,0)
	g_signal_connect(plugin, "mode-changed", G_CALLBACK(dict_plugin_panel_change_mode), dpd);
#else
	g_signal_connect(plugin, "orientation-changed", G_CALLBACK(dict_plugin_panel_change_orientation), dpd);
#endif
	g_signal_connect(plugin, "style-set", G_CALLBACK(dict_plugin_style_set), dpd);
	g_signal_connect(plugin, "save", G_CALLBACK(dict_plugin_write_rc_file), dpd);
	g_signal_connect(plugin, "configure-plugin", G_CALLBACK(dict_plugin_properties_dialog), dpd);
	g_signal_connect(plugin, "about", G_CALLBACK(dict_gui_about_dialog), dpd->dd);

	xfce_panel_plugin_menu_show_configure(plugin);
	xfce_panel_plugin_menu_show_about(plugin);

	/* file menu */
	g_signal_connect(dpd->dd->close_menu_item, "activate", G_CALLBACK(dict_plugin_close_button_clicked), dpd);
	g_signal_connect(dpd->dd->pref_menu_item, "activate", G_CALLBACK(dict_plugin_properties_dialog), dpd);

	/* panel entry */
	dpd->dd->panel_entry = gtk_entry_new();
	gtk_entry_set_icon_from_stock(GTK_ENTRY(dpd->dd->panel_entry), GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_CLEAR);
	gtk_entry_set_width_chars(GTK_ENTRY(dpd->dd->panel_entry), 25);
	gtk_entry_set_text(GTK_ENTRY(dpd->dd->panel_entry), _("Search term"));
	g_signal_connect(dpd->dd->panel_entry, "icon-release", G_CALLBACK(entry_icon_release_cb), dpd);
	g_signal_connect(dpd->dd->panel_entry, "activate", G_CALLBACK(entry_activate_cb), dpd);
	g_signal_connect(dpd->dd->panel_entry, "button-press-event", G_CALLBACK(entry_buttonpress_cb), dpd);
	g_signal_connect(dpd->dd->panel_entry, "changed", G_CALLBACK(entry_changed_cb), dpd);

	dpd->box = gtk_hbox_new(FALSE, 3);
	gtk_widget_show(dpd->box);

	gtk_box_pack_start(GTK_BOX(dpd->box), dpd->panel_button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(dpd->box), dpd->dd->panel_entry, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(plugin), dpd->box);

	dict_plugin_panel_set_size(dpd->plugin, xfce_panel_plugin_get_size(dpd->plugin), dpd);

	xfce_panel_plugin_add_action_widget(plugin, dpd->panel_button);
	dict_plugin_set_selection(dpd);

	/* DnD stuff */
	gtk_drag_dest_set(GTK_WIDGET(dpd->panel_button), GTK_DEST_DEFAULT_ALL,
		NULL, 0, GDK_ACTION_COPY | GDK_ACTION_MOVE);
	gtk_drag_dest_add_text_targets(GTK_WIDGET(dpd->panel_button));
	g_signal_connect(dpd->panel_button, "drag-data-received", G_CALLBACK(dict_plugin_drag_data_received), dpd);
	g_signal_connect(dpd->dd->panel_entry, "drag-data-received", G_CALLBACK(dict_plugin_drag_data_received), dpd);

	dict_gui_status_add(dpd->dd, _("Ready"));
}
XFCE_PANEL_PLUGIN_REGISTER(dict_plugin_construct);
