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
 * GNU General Public License for more device.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:gcm-device
 * @short_description: Color managed device object
 *
 * This object represents a device that can be colour managed.
 */

#include "config.h"

#include <glib-object.h>
#include <math.h>
#include <gio/gio.h>
#include <gconf/gconf-client.h>
#include <libgnomeui/gnome-rr.h>

#include "gcm-device.h"
#include "gcm-profile.h"

#include "egg-debug.h"

static void     gcm_device_finalize	(GObject     *object);

#define GCM_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GCM_TYPE_DEVICE, GcmDevicePrivate))

/**
 * GcmDevicePrivate:
 *
 * Private #GcmDevice data
 **/
struct _GcmDevicePrivate
{
	gfloat				 gamma;
	gfloat				 brightness;
	gfloat				 contrast;
	GcmDeviceType			 type;
	gchar				*id;
	gchar				*serial;
	gchar				*manufacturer;
	gchar				*model;
	gchar				*profile_filename;
	gchar				*profile_description;
	gchar				*title;
	gchar				*profile_copyright;
	gchar				*profile_vendor;
	GConfClient			*gconf_client;
	gchar				*native_device_xrandr;
	gchar				*native_device_sysfs;
};

enum {
	PROP_0,
	PROP_TYPE,
	PROP_ID,
	PROP_SERIAL,
	PROP_MODEL,
	PROP_MANUFACTURER,
	PROP_GAMMA,
	PROP_BRIGHTNESS,
	PROP_CONTRAST,
	PROP_PROFILE_FILENAME,
	PROP_PROFILE_COPYRIGHT,
	PROP_PROFILE_VENDOR,
	PROP_PROFILE_DESCRIPTION,
	PROP_TITLE,
	PROP_NATIVE_DEVICE_XRANDR,
	PROP_NATIVE_DEVICE_SYSFS,
	PROP_LAST
};

G_DEFINE_TYPE (GcmDevice, gcm_device, G_TYPE_OBJECT)

/**
 * gcm_device_get_default_config_location:
 **/
static gchar *
gcm_device_get_default_config_location (void)
{
	gchar *filename;

	/* create default path */
	filename = g_build_filename (g_get_user_config_dir (), "gnome-color-manager", "config.dat", NULL);

	return filename;
}

/**
 * gcm_device_load_from_profile:
 **/
static gboolean
gcm_device_load_from_profile (GcmDevice *device, GError **error)
{
	gboolean ret = TRUE;
	GcmProfile *profile = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (GCM_IS_DEVICE (device), FALSE);

	/* no profile to load */
	if (device->priv->profile_filename == NULL) {
		g_free (device->priv->profile_copyright);
		g_free (device->priv->profile_vendor);
		g_free (device->priv->profile_description);
		device->priv->profile_copyright = NULL;
		device->priv->profile_vendor = NULL;
		device->priv->profile_description = NULL;
		goto out;
	}

	/* load the profile if it's set */
	if (device->priv->profile_filename != NULL) {

		/* if the profile was deleted */
		ret = g_file_test (device->priv->profile_filename, G_FILE_TEST_EXISTS);
		if (!ret) {
			egg_warning ("the file was deleted and can't be loaded: %s", device->priv->profile_filename);
			/* this is not fatal */
			ret = TRUE;
			goto out;
		}

		/* create new profile instance */
		profile = gcm_profile_new ();
		ret = gcm_profile_parse (profile, device->priv->profile_filename, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to set from profile: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* copy the profile_description */
		g_free (device->priv->profile_copyright);
		g_free (device->priv->profile_vendor);
		g_free (device->priv->profile_description);
		g_object_get (profile,
			      "copyright", &device->priv->profile_copyright,
			      "vendor", &device->priv->profile_vendor,
			      "description", &device->priv->profile_description,
			      NULL);
	}
out:
	if (profile != NULL)
		g_object_unref (profile);
	return ret;
}

/**
 * gcm_device_get_id:
 **/
const gchar *
gcm_device_get_id (GcmDevice *device)
{
	g_return_val_if_fail (GCM_IS_DEVICE (device), NULL);
	return device->priv->id;
}

/**
 * gcm_device_load:
 **/
gboolean
gcm_device_load (GcmDevice *device, GError **error)
{
	gboolean ret;
	GKeyFile *file = NULL;
	GError *error_local = NULL;
	gchar *filename = NULL;

	g_return_val_if_fail (GCM_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (device->priv->id != NULL, FALSE);

	/* get default config */
	filename = gcm_device_get_default_config_location ();

	/* check we have a config, or is this first start */
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		/* we have no profile to load from */
		ret = TRUE;
		goto out;
	}

	/* load existing file */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, filename, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load from file: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* load data */
	g_free (device->priv->profile_filename);
	device->priv->profile_filename = g_key_file_get_string (file, device->priv->id, "profile", NULL);
	device->priv->gamma = g_key_file_get_double (file, device->priv->id, "gamma", &error_local);
	if (error_local != NULL) {
		device->priv->gamma = gconf_client_get_float (device->priv->gconf_client, "/apps/gnome-color-manager/default_gamma", NULL);
		if (device->priv->gamma < 0.1f)
			device->priv->gamma = 1.0f;
		g_clear_error (&error_local);
	}
	device->priv->brightness = g_key_file_get_double (file, device->priv->id, "brightness", &error_local);
	if (error_local != NULL) {
		device->priv->brightness = 0.0f;
		g_clear_error (&error_local);
	}
	device->priv->contrast = g_key_file_get_double (file, device->priv->id, "contrast", &error_local);
	if (error_local != NULL) {
		device->priv->contrast = 100.0f;
		g_clear_error (&error_local);
	}

	/* load this */
	ret = gcm_device_load_from_profile (device, &error_local);
	if (!ret) {

		/* just print a warning, this is not fatal */
		egg_warning ("failed to load profile %s: %s", device->priv->profile_filename, error_local->message);
		g_error_free (error_local);

		/* recover as the file might have been corrupted */
		g_free (device->priv->profile_filename);
		device->priv->profile_filename = NULL;
		ret = TRUE;
	}
out:
	g_free (filename);
	if (file != NULL)
		g_key_file_free (file);
	return ret;
}

/**
 * gcm_device_save:
 **/
gboolean
gcm_device_save (GcmDevice *device, GError **error)
{
	GKeyFile *keyfile = NULL;
	gboolean ret;
	gchar *data;
	gchar *dirname;
	GFile *file = NULL;
	gchar *filename = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (GCM_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (device->priv->id != NULL, FALSE);

	/* get default config */
	filename = gcm_device_get_default_config_location ();

	/* directory exists? */
	dirname = g_path_get_dirname (filename);
	ret = g_file_test (dirname, G_FILE_TEST_IS_DIR);
	if (!ret) {
		file = g_file_new_for_path (dirname);
		ret = g_file_make_directory_with_parents (file, NULL, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to create config directory: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* if not ever created, then just create a dummy file */
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		ret = g_file_set_contents (filename, "#created today", -1, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to create dummy header: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* load existing file */
	keyfile = g_key_file_new ();
	ret = g_key_file_load_from_file (keyfile, filename, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load existing config: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* save data */
	if (device->priv->profile_filename == NULL)
		g_key_file_remove_key (keyfile, device->priv->id, "profile", NULL);
	else
		g_key_file_set_string (keyfile, device->priv->id, "profile", device->priv->profile_filename);
	g_key_file_set_double (keyfile, device->priv->id, "gamma", device->priv->gamma);
	g_key_file_set_double (keyfile, device->priv->id, "brightness", device->priv->brightness);
	g_key_file_set_double (keyfile, device->priv->id, "contrast", device->priv->contrast);

	/* convert to string */
	data = g_key_file_to_data (keyfile, NULL, &error_local);
	if (data == NULL) {
		ret = FALSE;
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to convert config: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* save contents */
	ret = g_file_set_contents (filename, data, -1, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to save config: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_free (filename);
	g_free (dirname);
	if (file != NULL)
		g_object_unref (file);
	if (keyfile != NULL)
		g_key_file_free (keyfile);
	return ret;
}

/**
 * gcm_device_get_property:
 **/
static void
gcm_device_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GcmDevice *device = GCM_DEVICE (object);
	GcmDevicePrivate *priv = device->priv;

	switch (prop_id) {
	case PROP_TYPE:
		g_value_set_uint (value, priv->type);
		break;
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	case PROP_SERIAL:
		g_value_set_string (value, priv->serial);
		break;
	case PROP_MODEL:
		g_value_set_string (value, priv->model);
		break;
	case PROP_MANUFACTURER:
		g_value_set_string (value, priv->manufacturer);
		break;
	case PROP_GAMMA:
		g_value_set_float (value, priv->gamma);
		break;
	case PROP_BRIGHTNESS:
		g_value_set_float (value, priv->brightness);
		break;
	case PROP_CONTRAST:
		g_value_set_float (value, priv->contrast);
		break;
	case PROP_PROFILE_FILENAME:
		g_value_set_string (value, priv->profile_filename);
		break;
	case PROP_PROFILE_COPYRIGHT:
		g_value_set_string (value, priv->profile_copyright);
		break;
	case PROP_PROFILE_VENDOR:
		g_value_set_string (value, priv->profile_vendor);
		break;
	case PROP_PROFILE_DESCRIPTION:
		g_value_set_string (value, priv->profile_description);
		break;
	case PROP_TITLE:
		g_value_set_string (value, priv->title);
		break;
	case PROP_NATIVE_DEVICE_XRANDR:
		g_value_set_string (value, priv->native_device_xrandr);
		break;
	case PROP_NATIVE_DEVICE_SYSFS:
		g_value_set_string (value, priv->native_device_sysfs);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * gcm_device_set_property:
 **/
static void
gcm_device_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GcmDevice *device = GCM_DEVICE (object);
	GcmDevicePrivate *priv = device->priv;

	switch (prop_id) {
	case PROP_TYPE:
		priv->type = g_value_get_uint (value);
		break;
	case PROP_ID:
		g_free (priv->id);
		priv->id = g_strdup (g_value_get_string (value));
		break;
	case PROP_SERIAL:
		g_free (priv->serial);
		priv->serial = g_strdup (g_value_get_string (value));
		break;
	case PROP_MODEL:
		g_free (priv->model);
		priv->model = g_strdup (g_value_get_string (value));
		break;
	case PROP_MANUFACTURER:
		g_free (priv->manufacturer);
		priv->manufacturer = g_strdup (g_value_get_string (value));
		break;
	case PROP_TITLE:
		g_free (priv->title);
		priv->title = g_strdup (g_value_get_string (value));
		break;
	case PROP_PROFILE_FILENAME:
		g_free (priv->profile_filename);
		priv->profile_filename = g_strdup (g_value_get_string (value));
		break;
	case PROP_GAMMA:
		priv->gamma = g_value_get_float (value);
		break;
	case PROP_BRIGHTNESS:
		priv->brightness = g_value_get_float (value);
		break;
	case PROP_CONTRAST:
		priv->contrast = g_value_get_float (value);
		break;
	case PROP_NATIVE_DEVICE_XRANDR:
		g_free (priv->native_device_xrandr);
		priv->native_device_xrandr = g_strdup (g_value_get_string (value));
		break;
	case PROP_NATIVE_DEVICE_SYSFS:
		g_free (priv->native_device_sysfs);
		priv->native_device_sysfs = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * gcm_device_class_init:
 **/
static void
gcm_device_class_init (GcmDeviceClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gcm_device_finalize;
	object_class->get_property = gcm_device_get_property;
	object_class->set_property = gcm_device_set_property;

	/**
	 * GcmDevice:type:
	 */
	pspec = g_param_spec_uint ("type", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TYPE, pspec);

	/**
	 * GcmDevice:id:
	 */
	pspec = g_param_spec_string ("id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * GcmCalibrate:serial:
	 */
	pspec = g_param_spec_string ("serial", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SERIAL, pspec);

	/**
	 * GcmCalibrate:model:
	 */
	pspec = g_param_spec_string ("model", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_MODEL, pspec);

	/**
	 * GcmCalibrate:manufacturer:
	 */
	pspec = g_param_spec_string ("manufacturer", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_MANUFACTURER, pspec);

	/**
	 * GcmDevice:gamma:
	 */
	pspec = g_param_spec_float ("gamma", NULL, NULL,
				    0.0, G_MAXFLOAT, 1.01,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_GAMMA, pspec);

	/**
	 * GcmDevice:brightness:
	 */
	pspec = g_param_spec_float ("brightness", NULL, NULL,
				    0.0, G_MAXFLOAT, 1.02,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_BRIGHTNESS, pspec);

	/**
	 * GcmDevice:contrast:
	 */
	pspec = g_param_spec_float ("contrast", NULL, NULL,
				    0.0, G_MAXFLOAT, 1.03,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CONTRAST, pspec);

	/**
	 * GcmDevice:profile-filename:
	 */
	pspec = g_param_spec_string ("profile-filename", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PROFILE_FILENAME, pspec);

	/**
	 * GcmDevice:profile-copyright:
	 */
	pspec = g_param_spec_string ("profile-copyright", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_PROFILE_COPYRIGHT, pspec);

	/**
	 * GcmDevice:profile-vendor:
	 */
	pspec = g_param_spec_string ("profile-vendor", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_PROFILE_VENDOR, pspec);

	/**
	 * GcmDevice:profile-description:
	 */
	pspec = g_param_spec_string ("profile-description", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_PROFILE_DESCRIPTION, pspec);

	/**
	 * GcmDevice:title:
	 */
	pspec = g_param_spec_string ("title", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TITLE, pspec);

	/**
	 * GcmDevice:native-device-xrandr:
	 */
	pspec = g_param_spec_string ("native-device-xrandr", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_NATIVE_DEVICE_XRANDR, pspec);

	/**
	 * GcmDevice:native-device-sysfs:
	 */
	pspec = g_param_spec_string ("native-device-sysfs", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_NATIVE_DEVICE_SYSFS, pspec);

	g_type_class_add_private (klass, sizeof (GcmDevicePrivate));
}

/**
 * gcm_device_init:
 **/
static void
gcm_device_init (GcmDevice *device)
{
	device->priv = GCM_DEVICE_GET_PRIVATE (device);
	device->priv->id = NULL;
	device->priv->serial = NULL;
	device->priv->manufacturer = NULL;
	device->priv->model = NULL;
	device->priv->native_device_xrandr = NULL;
	device->priv->native_device_sysfs = NULL;
	device->priv->profile_filename = NULL;
	device->priv->gconf_client = gconf_client_get_default ();
	device->priv->gamma = gconf_client_get_float (device->priv->gconf_client, "/apps/gnome-color-manager/default_gamma", NULL);
	if (device->priv->gamma < 0.1f) {
		egg_warning ("failed to get setup parameters");
		device->priv->gamma = 1.0f;
	}
	device->priv->brightness = 0.0f;
	device->priv->contrast = 100.f;
}

/**
 * gcm_device_finalize:
 **/
static void
gcm_device_finalize (GObject *object)
{
	GcmDevice *device = GCM_DEVICE (object);
	GcmDevicePrivate *priv = device->priv;

	g_free (priv->profile_description);
	g_free (priv->profile_copyright);
	g_free (priv->profile_vendor);
	g_free (priv->title);
	g_free (priv->id);
	g_free (priv->serial);
	g_free (priv->manufacturer);
	g_free (priv->model);
	g_free (priv->native_device_xrandr);
	g_free (priv->native_device_sysfs);
	g_object_unref (priv->gconf_client);

	G_OBJECT_CLASS (gcm_device_parent_class)->finalize (object);
}

/**
 * gcm_device_new:
 *
 * Return value: a new GcmDevice object.
 **/
GcmDevice *
gcm_device_new (void)
{
	GcmDevice *device;
	device = g_object_new (GCM_TYPE_DEVICE, NULL);
	return GCM_DEVICE (device);
}

