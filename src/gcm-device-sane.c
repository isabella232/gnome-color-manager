/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2010 Richard Hughes <richard@hughsie.com>
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
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib-object.h>

#include "gcm-device-sane.h"
#include "gcm-enum.h"
#include "gcm-utils.h"

#include "egg-debug.h"

#define GCM_DEVICE_SANE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GCM_TYPE_DEVICE_SANE, GcmDeviceSanePrivate))

/**
 * GcmDeviceSanePrivate:
 *
 * Private #GcmDeviceSane data
 **/
struct _GcmDeviceSanePrivate
{
	gchar				*native_device;
};

enum {
	PROP_0,
	PROP_LAST
};

G_DEFINE_TYPE (GcmDeviceSane, gcm_device_sane, GCM_TYPE_DEVICE_UDEV)

typedef struct {
	gchar	*key;
	gchar	*value;
} GcmDeviceSaneKeyPair;

/**
 * gcm_device_sane_free_key_pair:
 **/
static void
gcm_device_sane_free_key_pair (GcmDeviceSaneKeyPair *key_pair)
{
	g_free (key_pair->key);
	g_free (key_pair->value);
	g_free (key_pair);
}

/**
 * gcm_device_sane_get_key_pairs_from_filename:
 **/
static GPtrArray *
gcm_device_sane_get_key_pairs_from_filename (const gchar *filename, GError **error)
{
	gboolean ret;
	gchar *contents = NULL;
	GPtrArray *array = NULL;
	guint i;
	gchar **split = NULL;
	GcmDeviceSaneKeyPair *key_pair;

	/* get contents */
	ret = g_file_get_contents (filename, &contents, NULL, error);
	if (!ret)
		goto out;

	/* parse */
	split = g_strsplit (contents, "\n", -1);
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) gcm_device_sane_free_key_pair);
	for (i=0; split[i] != NULL; i+=2) {
		key_pair = g_new0 (GcmDeviceSaneKeyPair, 1);
		if (split[i] == NULL || split[i+1] == NULL)
			break;
		key_pair->key = g_strdup (split[i]);
		key_pair->value = g_strdup (split[i+1]);
		g_ptr_array_add (array, key_pair);
	}
out:
	g_free (contents);
	g_strfreev (split);
	return array;
}

/**
 * gcm_device_sane_get_key_pairs_to_filename:
 **/
static gboolean
gcm_device_sane_get_key_pairs_to_filename (const gchar *filename, GPtrArray *array, GError **error)
{
	gboolean ret;
	GString *string;
	guint i;
	GcmDeviceSaneKeyPair *key_pair;

	/* turn the array into a string */
	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		key_pair = g_ptr_array_index (array, i);
		g_string_append (string, key_pair->key);
		g_string_append_c (string, '\n');
		g_string_append (string, key_pair->value);
		g_string_append_c (string, '\n');
	}

	/* save to file */
	ret = g_file_set_contents (filename, string->str, -1, error);
	if (!ret)
		goto out;
out:
	g_string_free (string, TRUE);
	return ret;
}

/**
 * gcm_device_sane_set_key_pair_value:
 **/
static void
gcm_device_sane_set_key_pair_value (GPtrArray *array, const gchar *key, const gchar *value)
{
	guint i;
	GcmDeviceSaneKeyPair *key_pair;

	/* find and replace */
	for (i=0; i<array->len; i++) {
		key_pair = g_ptr_array_index (array, i);
		if (g_strcmp0 (key_pair->key, key) == 0) {
			g_free (key_pair->value);
			key_pair->value = g_strdup (value);
			goto out;
		}
	}

	/* not found, create new */
	key_pair = g_new0 (GcmDeviceSaneKeyPair, 1);
	key_pair->key = g_strdup (key);
	key_pair->value = g_strdup (value);
	g_ptr_array_add (array, key_pair);
out:
	return;
}

/**
 * gcm_device_sane_apply_global:
 *
 * Return value: %TRUE for success;
 **/
static gboolean
gcm_device_sane_apply_global (GcmDeviceSane *device_sane, GError **error)
{
	gboolean ret = FALSE;
	gchar *filename = NULL;
	GPtrArray *array;

	filename = g_build_filename (g_get_home_dir (), ".sane", "xsane", "xsane.rc", NULL);

	/* get existing file, if it exists */
	array = gcm_device_sane_get_key_pairs_from_filename (filename, error);
	if (array == NULL)
		goto out;

	/* set some keys */
	gcm_device_sane_set_key_pair_value (array, "\"display-icm-profile\"", "\"\"");
	gcm_device_sane_set_key_pair_value (array, "\"working-color-space-icm-profile\"", "\"\"");
	gcm_device_sane_set_key_pair_value (array, "\"auto-correct-colors\"", "1");

	/* ensure directory exists */
	gcm_utils_mkdir_for_filename  (filename, NULL);

	/* save new file */
	ret = gcm_device_sane_get_key_pairs_to_filename (filename, array, error);
	if (!ret)
		goto out;
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	g_free (filename);
	return ret;
}

/**
 * gcm_device_sane_remove_spaces:
 *
 * Return value: %TRUE for success;
 **/
static void
gcm_device_sane_remove_spaces (gchar *text)
{
	guint i;
	guint j = 0;

	for (i=0; text[i] != '\0'; i++) {
		if (i != j)
			text[j] = text[i];
		if (text[i] != ' ')
			j++;
	}
	text[j] = '\0';
}

/**
 * gcm_device_sane_apply_device:
 *
 * Return value: %TRUE for success;
 **/
static gboolean
gcm_device_sane_apply_device (GcmDeviceSane *device_sane, GError **error)
{
	gboolean ret = FALSE;
	gchar *filename = NULL;
	gchar *device_filename = NULL;
	gchar *profile_filename = NULL;
	gchar *profile_filename_quoted = NULL;
	GPtrArray *array;
	gchar *manufacturer = NULL;
	gchar *model = NULL;

	/* get properties from device */
	g_object_get (device_sane,
		      "model", &model,
		      "manufacturer", &manufacturer,
		      "profile-filename", &profile_filename,
		      NULL);
	profile_filename_quoted = g_strdup_printf ("\"%s\"", profile_filename);

	device_filename = g_strdup_printf ("%s:%s.drc", manufacturer, model);
	g_strdelimit (device_filename, "/", '_');
	gcm_device_sane_remove_spaces (device_filename);
	egg_debug ("device_filename=%s", device_filename);

	filename = g_build_filename (g_get_home_dir (), ".sane", "xsane", device_filename, NULL);

	/* get existing file, if it exists */
	array = gcm_device_sane_get_key_pairs_from_filename (filename, error);
	if (array == NULL)
		goto out;

	/* set some keys */
	gcm_device_sane_set_key_pair_value (array, "\"xsane-scanner-default-color-icm-profile\"", profile_filename_quoted);
	gcm_device_sane_set_key_pair_value (array, "\"xsane-scanner-default-gray-icm-profile\"", profile_filename_quoted);
	gcm_device_sane_set_key_pair_value (array, "\"xsane-enable-color-management\"", "1");

	/* ensure directory exists */
	gcm_utils_mkdir_for_filename  (filename, NULL);

	/* save new file */
	ret = gcm_device_sane_get_key_pairs_to_filename (filename, array, error);
	if (!ret)
		goto out;
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	g_free (manufacturer);
	g_free (model);
	g_free (filename);
	g_free (device_filename);
	g_free (profile_filename);
	return ret;
}

/**
 * gcm_device_sane_apply:
 *
 * Return value: %TRUE for success;
 **/
static gboolean
gcm_device_sane_apply (GcmDevice *device, GError **error)
{
	gboolean ret;

	/* apply global settings for xsane */
	ret = gcm_device_sane_apply_global (GCM_DEVICE_SANE (device), error);
	if (!ret)
		goto out;

	/* apply device specific settings for xsane */
	ret = gcm_device_sane_apply_device (GCM_DEVICE_SANE (device), error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * gcm_device_sane_class_init:
 **/
static void
gcm_device_sane_class_init (GcmDeviceSaneClass *klass)
{
	GcmDeviceClass *device_class = GCM_DEVICE_CLASS (klass);
	device_class->apply = gcm_device_sane_apply;
	g_type_class_add_private (klass, sizeof (GcmDeviceSanePrivate));
}

/**
 * gcm_device_sane_init:
 **/
static void
gcm_device_sane_init (GcmDeviceSane *device_sane)
{
	device_sane->priv = GCM_DEVICE_SANE_GET_PRIVATE (device_sane);
}

/**
 * gcm_device_sane_new:
 *
 * Return value: a new #GcmDevice object.
 **/
GcmDevice *
gcm_device_sane_new (void)
{
	GcmDevice *device;
	device = g_object_new (GCM_TYPE_DEVICE_SANE, NULL);
	return GCM_DEVICE (device);
}
