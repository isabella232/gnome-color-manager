/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more profile.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <math.h>
#include <unique/unique.h>
#include <glib/gstdio.h>
#include <gudev/gudev.h>
#include <libgnomeui/gnome-rr.h>

#include "egg-debug.h"

#include "gcm-utils.h"
#include "gcm-profile.h"
#include "gcm-calibrate.h"
#include "gcm-brightness.h"
#include "gcm-client.h"

static GtkBuilder *builder = NULL;
static GtkListStore *list_store_devices = NULL;
static GcmDevice *current_device = NULL;
static GnomeRRScreen *rr_screen = NULL;
static GPtrArray *profiles_array = NULL;
static GUdevClient *client = NULL;
static GcmClient *gcm_client = NULL;
gboolean setting_up_device = FALSE;

enum {
	GPM_DEVICES_COLUMN_ID,
	GPM_DEVICES_COLUMN_ICON,
	GPM_DEVICES_COLUMN_TITLE,
	GPM_DEVICES_COLUMN_LAST
};

/**
 * gcm_prefs_close_cb:
 **/
static void
gcm_prefs_close_cb (GtkWidget *widget, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	g_main_loop_quit (loop);
}

/**
 * gcm_prefs_delete_event_cb:
 **/
static gboolean
gcm_prefs_delete_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	gcm_prefs_close_cb (widget, data);
	return FALSE;
}

/**
 * gcm_prefs_help_cb:
 **/
static void
gcm_prefs_help_cb (GtkWidget *widget, gpointer data)
{
	gcm_gnome_help ("preferences");
}

/**
 * gcm_prefs_calibrate_cb:
 **/
static void
gcm_prefs_calibrate_cb (GtkWidget *widget, gpointer data)
{
	GcmCalibrate *calib = NULL;
	GcmBrightness *brightness = NULL;
	gboolean ret;
	GError *error = NULL;
	GtkWindow *window;
	GnomeRROutput *output;
	const gchar *output_name;
	const gchar *name;
	gchar *filename = NULL;
	gchar *destination = NULL;
	guint i;
	guint percentage = G_MAXUINT;

	/* get the device */
	g_object_get (current_device,
		      "native-device-xrandr", &output,
		      NULL);
	if (output == NULL) {
		egg_warning ("failed to get output");
		goto out;
	}

	/* create new calibration and brightness objects */
	calib = gcm_calibrate_new ();
	brightness = gcm_brightness_new ();

	/* set the proper output name */
	output_name = gnome_rr_output_get_name (output);
	g_object_set (calib,
		      "output-name", output_name,
		      NULL);

	/* run each task in order */
	window = GTK_WINDOW(gtk_builder_get_object (builder, "dialog_prefs"));
	ret = gcm_calibrate_setup (calib, window, &error);
	if (!ret) {
		egg_warning ("failed to setup: %s", error->message);
		g_error_free (error);
		goto finish_calibrate;
	}

	/* if we are an internal LCD, we need to set the brightness to maximum */
	ret = gcm_utils_output_is_lcd_internal (output_name);
	if (ret) {
		/* get the old brightness so we can restore state */
		ret = gcm_brightness_get_percentage (brightness, &percentage, &error);
		if (!ret) {
			egg_warning ("failed to get brightness: %s", error->message);
			g_error_free (error);
			/* not fatal */
			error = NULL;
		}

		/* set the new brightness */
		ret = gcm_brightness_set_percentage (brightness, 100, &error);
		if (!ret) {
			egg_warning ("failed to set brightness: %s", error->message);
			g_error_free (error);
			/* not fatal */
			error = NULL;
		}
	}

	/* step 1 */
	ret = gcm_calibrate_task (calib, GCM_CALIBRATE_TASK_NEUTRALISE, &error);
	if (!ret) {
		egg_warning ("failed to calibrate: %s", error->message);
		g_error_free (error);
		goto finish_calibrate;
	}

	/* step 2 */
	ret = gcm_calibrate_task (calib, GCM_CALIBRATE_TASK_GENERATE_PATCHES, &error);
	if (!ret) {
		egg_warning ("failed to calibrate: %s", error->message);
		g_error_free (error);
		goto finish_calibrate;
	}

	/* step 3 */
	ret = gcm_calibrate_task (calib, GCM_CALIBRATE_TASK_DRAW_AND_MEASURE, &error);
	if (!ret) {
		egg_warning ("failed to calibrate: %s", error->message);
		g_error_free (error);
		goto finish_calibrate;
	}

	/* step 4 */
	ret = gcm_calibrate_task (calib, GCM_CALIBRATE_TASK_GENERATE_PROFILE, &error);
	if (!ret) {
		egg_warning ("failed to calibrate: %s", error->message);
		g_error_free (error);
		goto finish_calibrate;
	}

finish_calibrate:
	/* restore brightness */
	if (percentage != G_MAXUINT) {
		/* set the new brightness */
		ret = gcm_brightness_set_percentage (brightness, percentage, &error);
		if (!ret) {
			egg_warning ("failed to restore brightness: %s", error->message);
			g_error_free (error);
			/* not fatal */
			error = NULL;
		}
	}

	/* step 4 */
	filename = gcm_calibrate_finish (calib, &error);
	if (filename == NULL) {
		egg_warning ("failed to finish calibrate: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* copy the ICC file to the proper location */
	destination = gcm_utils_get_profile_destination (filename);
	ret = gcm_utils_mkdir_and_copy (filename, destination, &error);
	if (!ret) {
		egg_warning ("failed to calibrate: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find an existing profile of this name */
	for (i=0; i<profiles_array->len; i++) {
		name = g_ptr_array_index (profiles_array, i);
		if (g_strcmp0 (name, destination) == 0) {
			egg_debug ("found existing profile: %s", destination);
			break;
		}
	}

	/* we didn't find an existing profile */
	if (i == profiles_array->len) {
		egg_debug ("adding: %s", destination);
		g_ptr_array_add (profiles_array, g_strdup (destination));
		i = profiles_array->len - 1;
	}

	/* set the new profile and save config */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "combobox_profile"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), i);

	/* remove temporary file */
	g_unlink (filename);

out:
	if (calib != NULL)
		g_object_unref (calib);
	if (brightness != NULL)
		g_object_unref (brightness);

	/* need to set the gamma back to the default after calibration */
	ret = gcm_utils_set_gamma_for_device (current_device, &error);
	if (!ret) {
		egg_warning ("failed to set output gamma: %s", error->message);
		g_error_free (error);
	}
	g_free (filename);
	g_free (destination);
}

/**
 * gcm_prefs_reset_cb:
 **/
static void
gcm_prefs_reset_cb (GtkWidget *widget, gpointer data)
{
	setting_up_device = TRUE;
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_gamma"));
	gtk_range_set_value (GTK_RANGE (widget), 1.0f);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_brightness"));
	gtk_range_set_value (GTK_RANGE (widget), 0.0f);
	setting_up_device = FALSE;
	/* we only want one save, not three */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_contrast"));
	gtk_range_set_value (GTK_RANGE (widget), 100.0f);
}

/**
 * gcm_prefs_message_received_cb
 **/
static UniqueResponse
gcm_prefs_message_received_cb (UniqueApp *app, UniqueCommand command, UniqueMessageData *message_data, guint time_ms, gpointer data)
{
	GtkWindow *window;
	if (command == UNIQUE_ACTIVATE) {
		window = GTK_WINDOW (gtk_builder_get_object (builder, "dialog_prefs"));
		gtk_window_present (window);
	}
	return UNIQUE_RESPONSE_OK;
}

/**
 * gcm_window_set_parent_xid:
 **/
static void
gcm_window_set_parent_xid (GtkWindow *window, guint32 xid)
{
	GdkDisplay *display;
	GdkWindow *parent_window;
	GdkWindow *our_window;

	display = gdk_display_get_default ();
	parent_window = gdk_window_foreign_new_for_display (display, xid);
	our_window = gtk_widget_get_window (GTK_WIDGET (window));

	/* set this above our parent */
	gtk_window_set_modal (window, TRUE);
	gdk_window_set_transient_for (our_window, parent_window);
}

/**
 * gcm_prefs_add_devices_columns:
 **/
static void
gcm_prefs_add_devices_columns (GtkTreeView *treeview)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* image */
	renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_DIALOG, NULL);
	column = gtk_tree_view_column_new_with_attributes (_("Screen"), renderer,
							   "icon-name", GPM_DEVICES_COLUMN_ICON, NULL);
	gtk_tree_view_append_column (treeview, column);

	/* column for text */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Label"), renderer,
							   "markup", GPM_DEVICES_COLUMN_TITLE, NULL);
	gtk_tree_view_column_set_sort_column_id (column, GPM_DEVICES_COLUMN_TITLE);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_expand (column, TRUE);
}

/**
 * gcm_prefs_devices_treeview_clicked_cb:
 **/
static void
gcm_prefs_devices_treeview_clicked_cb (GtkTreeSelection *selection, gboolean data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *profile = NULL;
	GtkWidget *widget;
	gfloat localgamma;
	gfloat brightness;
	gfloat contrast;
	const gchar *filename;
	guint i;
	gchar *id;
	GcmDeviceType type;

	/* This will only work in single or browse selection mode! */
	if (!gtk_tree_selection_get_selected (selection, &model, &iter)) {
		egg_debug ("no row selected");
		return;
	}

	/* get id */
	gtk_tree_model_get (model, &iter,
			    GPM_DEVICES_COLUMN_ID, &id,
			    -1);

	/* show transaction_id */
	egg_debug ("selected device is: %s", id);
	if (current_device != NULL)
		g_object_unref (current_device);
	current_device = gcm_client_get_device_by_id (gcm_client, id);
	g_object_get (current_device,
		      "type", &type,
		      NULL);

	/* not a xrandr device */
	if (type == GCM_DEVICE_TYPE_SCANNER) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "expander1"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_reset"));
		gtk_widget_hide (widget);
	} else {

		/* show more UI */
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "expander1"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_reset"));
		gtk_widget_show (widget);
	}

	g_object_get (current_device,
		      "profile", &profile,
		      "gamma", &localgamma,
		      "brightness", &brightness,
		      "contrast", &contrast,
		      NULL);

	/* set adjustments */
	setting_up_device = TRUE;
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_gamma"));
	gtk_range_set_value (GTK_RANGE (widget), localgamma);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_brightness"));
	gtk_range_set_value (GTK_RANGE (widget), brightness);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_contrast"));
	gtk_range_set_value (GTK_RANGE (widget), contrast);
	setting_up_device = FALSE;

	/* set correct profile */
	if (profile == NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "combobox_profile"));
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), profiles_array->len);
	} else {
		profiles_array = gcm_utils_get_profile_filenames ();
		for (i=0; i<profiles_array->len; i++) {
			filename = g_ptr_array_index (profiles_array, i);
			if (g_strcmp0 (filename, profile) == 0) {
				widget = GTK_WIDGET (gtk_builder_get_object (builder, "combobox_profile"));
				gtk_combo_box_set_active (GTK_COMBO_BOX (widget), i);
				break;
			}
		}
	}

	g_free (id);
	g_free (profile);
}

/**
 * gcm_prefs_add_device_xrandr:
 **/
static void
gcm_prefs_add_device_xrandr (GcmDevice *device)
{
	GtkTreeIter iter;
	gchar *title;
	gchar *id;
	gboolean ret;
	GError *error = NULL;

	/* get details */
	g_object_get (device,
		      "id", &id,
		      "title", &title,
		      NULL);

	/* set the gamma on the device */
	ret = gcm_utils_set_gamma_for_device (device, &error);
	if (!ret) {
		egg_warning ("failed to set output gamma: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* add to list */
	egg_debug ("add %s to device list", id);
	gtk_list_store_append (list_store_devices, &iter);
	gtk_list_store_set (list_store_devices, &iter,
			    GPM_DEVICES_COLUMN_ID, id,
			    GPM_DEVICES_COLUMN_TITLE, title,
			    GPM_DEVICES_COLUMN_ICON, "video-display", -1);
out:
	g_free (id);
	g_free (title);
}

/**
 * gcm_prefs_set_combo_simple_text:
 **/
static void
gcm_prefs_set_combo_simple_text (GtkWidget *combo_box)
{
	GtkCellRenderer *cell;
	GtkListStore *store;

	store = gtk_list_store_new (1, G_TYPE_STRING);
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo_box), GTK_TREE_MODEL (store));
	g_object_unref (store);

	cell = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), cell, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), cell,
					"text", 0,
					NULL);
}

/**
 * gcm_prefs_add_profiles:
 **/
static void
gcm_prefs_add_profiles (GtkWidget *widget)
{
	const gchar *filename;
	guint i;
	gchar *displayname;
	GcmProfile *profile;
	gboolean ret;
	GError *error = NULL;

	/* get profiles */
	profiles_array = gcm_utils_get_profile_filenames ();
	for (i=0; i<profiles_array->len; i++) {
		filename = g_ptr_array_index (profiles_array, i);
		profile = gcm_profile_new ();
		ret = gcm_profile_parse (profile, filename, &error);
		if (!ret) {
			egg_warning ("failed to add profile: %s", error->message);
			g_error_free (error);
			g_object_unref (profile);
			continue;
		}
		g_object_get (profile,
			      "description", &displayname,
			      NULL);
		gtk_combo_box_append_text (GTK_COMBO_BOX (widget), displayname);
		g_free (displayname);
		g_object_unref (profile);
	}

	/* add a clear entry */
	gtk_combo_box_append_text (GTK_COMBO_BOX (widget), _("None"));
}

/**
 * gcm_prefs_profile_combo_changed_cb:
 **/
static void
gcm_prefs_profile_combo_changed_cb (GtkWidget *widget, gpointer data)
{
	guint active;
	gchar *copyright = NULL;
	gchar *description = NULL;
	gchar *profile_old = NULL;
	const gchar *filename = NULL;
	gboolean ret;
	GError *error = NULL;
	GcmProfile *profile = NULL;
	gboolean changed;
	GcmDeviceType type;

	active = gtk_combo_box_get_active (GTK_COMBO_BOX(widget));
	egg_debug ("now %i", active);

	if (active < profiles_array->len)
		filename = g_ptr_array_index (profiles_array, active);
	egg_debug ("profile=%s", filename);

	/* see if it's changed */
	g_object_get (current_device,
		      "profile", &profile_old,
		      "type", &type,
		      NULL);
	egg_warning ("old: %s, new:%s", profile_old, filename);
	changed = ((g_strcmp0 (profile_old, filename) != 0));

	/* set new profile */
	if (changed) {
		g_object_set (current_device,
			      "profile", filename,
			      NULL);
	}

	/* get new data */
	if (filename != NULL) {
		profile = gcm_profile_new ();
		ret = gcm_profile_parse (profile, filename, &error);
		if (!ret) {
			egg_warning ("failed to load profile: %s", error->message);
			g_error_free (error);
			goto out;
		}
		/* get the new details from the profile */
		g_object_get (profile,
			      "copyright", &copyright,
			      "description", &description,
			      NULL);
	}

	/* set new descriptions */
	if (copyright == NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_title_copyright"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_copyright"));
		gtk_widget_hide (widget);
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_title_copyright"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_copyright"));
		gtk_label_set_label (GTK_LABEL(widget), copyright);
		gtk_widget_show (widget);
	}

	/* set new descriptions */
	if (description == NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_title_description"));
		gtk_widget_hide (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_description"));
		gtk_widget_hide (widget);
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_title_description"));
		gtk_widget_show (widget);
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "label_description"));
		gtk_label_set_label (GTK_LABEL(widget), description);
		gtk_widget_show (widget);
	}

	/* set new descriptions */
	if (copyright == NULL && description == NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "table_details"));
		gtk_widget_hide (widget);
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (builder, "table_details"));
		gtk_widget_show (widget);
	}

	/* only save and set if changed */
	if (changed) {

		/* save new profile */
		ret = gcm_device_save (current_device, &error);
		if (!ret) {
			egg_warning ("failed to save config: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* set the gamma for display types */
		if (type == GCM_DEVICE_TYPE_DISPLAY) {
			ret = gcm_utils_set_gamma_for_device (current_device, &error);
			if (!ret) {
				egg_warning ("failed to set output gamma: %s", error->message);
				g_error_free (error);
				goto out;
			}
		}
	}
out:
	if (profile != NULL)
		g_object_unref (profile);
	g_free (copyright);
	g_free (description);
}

/**
 * gcm_prefs_slider_changed_cb:
 **/
static void
gcm_prefs_slider_changed_cb (GtkRange *range, gpointer *user_data)
{
	gfloat localgamma;
	gfloat brightness;
	gfloat contrast;
	GtkWidget *widget;
	gboolean ret;
	GError *error = NULL;

	/* we're just setting up the device, not moving the slider */
	if (setting_up_device)
		return;

	/* get values */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_gamma"));
	localgamma = gtk_range_get_value (GTK_RANGE (widget));
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_brightness"));
	brightness = gtk_range_get_value (GTK_RANGE (widget));
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_contrast"));
	contrast = gtk_range_get_value (GTK_RANGE (widget));

	g_object_set (current_device,
		      "gamma", localgamma,
		      "brightness", brightness,
		      "contrast", contrast,
		      NULL);

	/* save new profile */
	ret = gcm_device_save (current_device, &error);
	if (!ret) {
		egg_warning ("failed to save config: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* actually set the new profile */
	ret = gcm_utils_set_gamma_for_device (current_device, &error);
	if (!ret) {
		egg_warning ("failed to set output gamma: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return;
}

/**
 * gcm_prefs_has_hardware_device_attached:
 **/
static gboolean
gcm_prefs_has_hardware_device_attached (void)
{
	GList *devices;
	GList *l;
	GUdevDevice *device;
	gboolean ret = FALSE;

	/* get all USB devices */
	devices = g_udev_client_query_by_subsystem (client, "usb");
	for (l = devices; l != NULL; l = l->next) {
		device = l->data;
		ret = g_udev_device_get_property_as_boolean (device, "COLOR_MEASUREMENT_DEVICE");
		if (ret) {
			egg_debug ("found color management device: %s", g_udev_device_get_sysfs_path (device));
			break;
		}
	}

	g_list_foreach (devices, (GFunc) g_object_unref, NULL);
	g_list_free (devices);
	return ret;
}

/**
 * gcm_prefs_check_calibration_hardware:
 **/
static void
gcm_prefs_check_calibration_hardware (void)
{
	gboolean ret;
#ifdef GCM_HARDWARE_DETECTION
	GtkWidget *widget;
#endif

	/* find whether we have hardware installed */
	ret = gcm_prefs_has_hardware_device_attached ();

#ifdef GCM_HARDWARE_DETECTION
	/* disable the button if no supported hardware is found */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_calibrate"));
	gtk_widget_set_sensitive (widget, ret);
#else
	egg_debug ("not setting calibrate button %s as not compiled with hardware detection", ret ? "sensitive" : "insensitive");
#endif
}

/**
 * gcm_prefs_uevent_cb:
 **/
static void
gcm_prefs_uevent_cb (GUdevClient *client_, const gchar *action, GUdevDevice *device, gpointer user_data)
{
	egg_debug ("uevent %s", action);
	gcm_prefs_check_calibration_hardware ();
}

/**
 * gcm_prefs_add_device_scanner:
 **/
static void
gcm_prefs_add_device_scanner (GcmDevice *device)
{
	GtkTreeIter iter;
	gchar *title;
	gchar *id;

	/* get details */
	g_object_get (device,
		      "id", &id,
		      "title", &title,
		      NULL);

	/* add to list */
	gtk_list_store_append (list_store_devices, &iter);
	gtk_list_store_set (list_store_devices, &iter,
			    GPM_DEVICES_COLUMN_ID, id,
			    GPM_DEVICES_COLUMN_TITLE, title,
			    GPM_DEVICES_COLUMN_ICON, "scanner", -1);
	g_free (id);
	g_free (title);
}

/**
 * gcm_prefs_added_cb:
 **/
static void
gcm_prefs_added_cb (GcmClient *gcm_client_, GcmDevice *gcm_device, gpointer user_data)
{
	GcmDeviceType type;
	egg_debug ("added: %s", gcm_device_get_id (gcm_device));

	/* get the type of the device */
	g_object_get (gcm_device,
		      "type", &type,
		      NULL);

	/* add the device */
	if (type == GCM_DEVICE_TYPE_DISPLAY)
		gcm_prefs_add_device_xrandr (gcm_device);
	else if (type == GCM_DEVICE_TYPE_SCANNER)
		gcm_prefs_add_device_scanner (gcm_device);
}

/**
 * gcm_prefs_removed_cb:
 **/
static void
gcm_prefs_removed_cb (GcmClient *gcm_client_, GcmDevice *gcm_device, gpointer user_data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	const gchar *id;
	gchar *id_tmp;
	gboolean ret;

	/* remove */
	id = gcm_device_get_id (gcm_device);
	egg_debug ("removed: %s", id);

	/* get first element */
	model = GTK_TREE_MODEL (list_store_devices);
	ret = gtk_tree_model_get_iter_first (model, &iter);
	if (!ret)
		return;

	/* get the other elements */
	do {
		gtk_tree_model_get (model, &iter,
				    GPM_DEVICES_COLUMN_ID, &id_tmp,
				    -1);
		if (g_strcmp0 (id_tmp, id) == 0) {
			gtk_list_store_remove (GTK_LIST_STORE(model), &iter);
			g_free (id_tmp);
			break;
		}
		g_free (id_tmp);
	} while (gtk_tree_model_iter_next (model, &iter));
}

/**
 * main:
 **/
int
main (int argc, char **argv)
{
	gboolean verbose = FALSE;
	guint retval = 0;
	GOptionContext *context;
	GtkWidget *main_window;
	GtkWidget *widget;
	UniqueApp *unique_app;
	guint xid = 0;
	GError *error = NULL;
	GMainLoop *loop;
	GtkTreeSelection *selection;
	const gchar *subsystems[] = {"usb", NULL};
	gboolean ret;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "parent-window", 'p', 0, G_OPTION_ARG_INT, &xid,
		  /* TRANSLATORS: we can make this modal (stay on top of) another window */
		  _("Set the parent window to make this modal"), NULL },
		{ NULL}
	};

	gtk_init (&argc, &argv);

	context = g_option_context_new ("gnome-color-manager prefs program");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);

	/* block in a loop */
	loop = g_main_loop_new (NULL, FALSE);

	/* are we already activated? */
	unique_app = unique_app_new ("org.gnome.ColorManager.Prefs", NULL);
	if (unique_app_is_running (unique_app)) {
		egg_debug ("You have another instance running. This program will now close");
		unique_app_send_message (unique_app, UNIQUE_ACTIVATE, NULL);
		goto out;
	}
	g_signal_connect (unique_app, "message-received",
			  G_CALLBACK (gcm_prefs_message_received_cb), NULL);

	/* get UI */
	builder = gtk_builder_new ();
	retval = gtk_builder_add_from_file (builder, GCM_DATA "/gcm-prefs.ui", &error);
	if (retval == 0) {
		egg_warning ("failed to load ui: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* use GUdev to find the calibration device */
	client = g_udev_client_new (subsystems);
	g_signal_connect (client, "uevent",
			  G_CALLBACK (gcm_prefs_uevent_cb), NULL);

	/* set calibrate button sensitivity */
	gcm_prefs_check_calibration_hardware ();

	/* create list stores */
	list_store_devices = gtk_list_store_new (GPM_DEVICES_COLUMN_LAST, G_TYPE_STRING,
						 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);

	/* create transaction_id tree view */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "treeview_devices"));
	gtk_tree_view_set_model (GTK_TREE_VIEW (widget),
				 GTK_TREE_MODEL (list_store_devices));
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gcm_prefs_devices_treeview_clicked_cb), NULL);

	/* add columns to the tree view */
	gcm_prefs_add_devices_columns (GTK_TREE_VIEW (widget));
	gtk_tree_view_columns_autosize (GTK_TREE_VIEW (widget)); /* show */

	main_window = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_prefs"));

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), GCM_STOCK_ICON);
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (gcm_prefs_delete_event_cb), loop);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_close"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gcm_prefs_close_cb), loop);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_help"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gcm_prefs_help_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_reset"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gcm_prefs_reset_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_calibrate"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gcm_prefs_calibrate_cb), NULL);

	/* setup icc profiles list */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "combobox_profile"));
	gcm_prefs_set_combo_simple_text (widget);
	gcm_prefs_add_profiles (widget);
	g_signal_connect (G_OBJECT (widget), "changed",
			  G_CALLBACK (gcm_prefs_profile_combo_changed_cb), NULL);

	/* set ranges */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_gamma"));
	gtk_range_set_range (GTK_RANGE (widget), 0.1f, 5.0f);
	gtk_scale_add_mark (GTK_SCALE (widget), 1.0f, GTK_POS_TOP, "");
	gtk_scale_add_mark (GTK_SCALE (widget), 1.8f, GTK_POS_TOP, "");
	gtk_scale_add_mark (GTK_SCALE (widget), 2.2f, GTK_POS_TOP, "");

	/* set ranges */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_brightness"));
	gtk_range_set_range (GTK_RANGE (widget), 0.0f, 99.0f);

	/* set ranges */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_contrast"));
	gtk_range_set_range (GTK_RANGE (widget), 1.0f, 100.0f);

	/* get screen */
	rr_screen = gnome_rr_screen_new (gdk_screen_get_default (), NULL, NULL, &error);
	if (rr_screen == NULL) {
		egg_warning ("failed to get rr screen: %s", error->message);
		goto out;
	}

	/* use a device client array */
	gcm_client = gcm_client_new ();
	g_signal_connect (gcm_client, "added", G_CALLBACK (gcm_prefs_added_cb), NULL);
	g_signal_connect (gcm_client, "removed", G_CALLBACK (gcm_prefs_removed_cb), NULL);

	/* coldplug devices */
	ret = gcm_client_coldplug (gcm_client, &error);
	if (!ret) {
		egg_warning ("failed to coldplug: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set the parent window if it is specified */
	if (xid != 0) {
		egg_debug ("Setting xid %i", xid);
		gcm_window_set_parent_xid (GTK_WINDOW (main_window), xid);
	}

	/* show main UI */
	gtk_widget_show (main_window);

	/* connect up sliders */
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_contrast"));
	g_signal_connect (widget, "value-changed",
			  G_CALLBACK (gcm_prefs_slider_changed_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_brightness"));
	g_signal_connect (widget, "value-changed",
			  G_CALLBACK (gcm_prefs_slider_changed_cb), NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "hscale_gamma"));
	g_signal_connect (widget, "value-changed",
			  G_CALLBACK (gcm_prefs_slider_changed_cb), NULL);

	/* wait */
	g_main_loop_run (loop);

out:
	g_object_unref (unique_app);
	g_main_loop_unref (loop);
	if (current_device != NULL)
		g_object_unref (current_device);
	if (rr_screen != NULL)
		gnome_rr_screen_destroy (rr_screen);
	if (client != NULL)
		g_object_unref (client);
	if (builder != NULL)
		g_object_unref (builder);
	if (profiles_array != NULL)
		g_ptr_array_unref (profiles_array);
	if (gcm_client != NULL)
		g_object_unref (gcm_client);
	return retval;
}

