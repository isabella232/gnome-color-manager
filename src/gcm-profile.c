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

/**
 * SECTION:gcm-profile
 * @short_description: A parser object that understands the ICC profile data format.
 *
 * This object is a simple parser for the ICC binary profile data. If only understands
 * a subset of the ICC profile, just enought to get some metadata and the LUT.
 */

#include "config.h"

#include <glib-object.h>
#include <glib/gi18n.h>

#include "egg-debug.h"

#include "gcm-profile.h"
#include "gcm-utils.h"
#include "gcm-xyz.h"
#include "gcm-profile-lcms1.h"

static void     gcm_profile_finalize	(GObject     *object);

#define GCM_PROFILE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GCM_TYPE_PROFILE, GcmProfilePrivate))

/**
 * GcmProfilePrivate:
 *
 * Private #GcmProfile data
 **/
struct _GcmProfilePrivate
{
	guint				 profile_type;
	guint				 colorspace;
	guint				 size;
	gchar				*description;
	gchar				*filename;
	gchar				*copyright;
	gchar				*manufacturer;
	gchar				*model;
	gchar				*datetime;
	GcmXyz				*white_point;
	GcmXyz				*black_point;
	GcmXyz				*luminance_red;
	GcmXyz				*luminance_green;
	GcmXyz				*luminance_blue;
};

enum {
	PROP_0,
	PROP_COPYRIGHT,
	PROP_MANUFACTURER,
	PROP_MODEL,
	PROP_DATETIME,
	PROP_DESCRIPTION,
	PROP_FILENAME,
	PROP_TYPE,
	PROP_COLORSPACE,
	PROP_SIZE,
	PROP_WHITE_POINT,
	PROP_BLACK_POINT,
	PROP_LUMINANCE_RED,
	PROP_LUMINANCE_GREEN,
	PROP_LUMINANCE_BLUE,
	PROP_LAST
};

G_DEFINE_TYPE (GcmProfile, gcm_profile, G_TYPE_OBJECT)

/**
 * gcm_profile_parse_data:
 **/
gboolean
gcm_profile_parse_data (GcmProfile *profile, const guint8 *data, gsize length, GError **error)
{
	gboolean ret = FALSE;
	GcmProfilePrivate *priv = profile->priv;
	GcmProfileClass *klass = GCM_PROFILE_GET_CLASS (profile);

	/* save the length */
	priv->size = length;

	/* do we have support */
	if (klass->parse_data == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "no support");
		goto out;
	}

	/* proxy */
	ret = klass->parse_data (profile, data, length, error);
out:
	return ret;
}

/**
 * gcm_profile_parse:
 **/
gboolean
gcm_profile_parse (GcmProfile *profile, const gchar *filename, GError **error)
{
	gchar *data = NULL;
	gboolean ret;
	guint length;
	GError *error_local = NULL;
	GcmProfilePrivate *priv = profile->priv;

	g_return_val_if_fail (GCM_IS_PROFILE (profile), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	egg_debug ("loading '%s'", filename);

	/* save */
	g_free (priv->filename);
	priv->filename = g_strdup (filename);

	/* load files */
	ret = g_file_get_contents (filename, &data, (gsize *) &length, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to load profile: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* parse the data */
	ret = gcm_profile_parse_data (profile, (const guint8*)data, length, error);
	if (!ret)
		goto out;
out:
	g_free (data);
	return ret;
}

/**
 * gcm_profile_generate_vcgt:
 *
 * Free with g_object_unref();
 **/
GcmClut *
gcm_profile_generate_vcgt (GcmProfile *profile, guint size)
{
	GcmClut *clut = NULL;
	GcmProfileClass *klass = GCM_PROFILE_GET_CLASS (profile);

	/* do we have support */
	if (klass->generate_vcgt == NULL)
		goto out;

	/* proxy */
	clut = klass->generate_vcgt (profile, size);
out:
	return clut;
}

/**
 * gcm_profile_generate_curve:
 *
 * Free with g_object_unref();
 **/
GcmClut *
gcm_profile_generate_curve (GcmProfile *profile, guint size)
{
	GcmClut *clut = NULL;
	GcmProfileClass *klass = GCM_PROFILE_GET_CLASS (profile);

	/* do we have support */
	if (klass->generate_curve == NULL)
		goto out;

	/* proxy */
	clut = klass->generate_curve (profile, size);
out:
	return clut;
}

/**
 * gcm_profile_type_to_text:
 **/
const gchar *
gcm_profile_type_to_text (GcmProfileType type)
{
	if (type == GCM_PROFILE_TYPE_INPUT_DEVICE)
		return "input-device";
	if (type == GCM_PROFILE_TYPE_DISPLAY_DEVICE)
		return "display-device";
	if (type == GCM_PROFILE_TYPE_OUTPUT_DEVICE)
		return "output-device";
	if (type == GCM_PROFILE_TYPE_DEVICELINK)
		return "devicelink";
	if (type == GCM_PROFILE_TYPE_COLORSPACE_CONVERSION)
		return "colorspace-conversion";
	if (type == GCM_PROFILE_TYPE_ABSTRACT)
		return "abstract";
	if (type == GCM_PROFILE_TYPE_NAMED_COLOR)
		return "named-color";
	return "unknown";
}

/**
 * gcm_profile_colorspace_to_text:
 **/
const gchar *
gcm_profile_colorspace_to_text (GcmProfileColorspace type)
{
	if (type == GCM_PROFILE_COLORSPACE_XYZ)
		return "xyz";
	if (type == GCM_PROFILE_COLORSPACE_LAB)
		return "lab";
	if (type == GCM_PROFILE_COLORSPACE_LUV)
		return "luv";
	if (type == GCM_PROFILE_COLORSPACE_YCBCR)
		return "ycbcr";
	if (type == GCM_PROFILE_COLORSPACE_YXY)
		return "yxy";
	if (type == GCM_PROFILE_COLORSPACE_RGB)
		return "rgb";
	if (type == GCM_PROFILE_COLORSPACE_GRAY)
		return "gray";
	if (type == GCM_PROFILE_COLORSPACE_HSV)
		return "hsv";
	if (type == GCM_PROFILE_COLORSPACE_CMYK)
		return "cmyk";
	if (type == GCM_PROFILE_COLORSPACE_CMY)
		return "cmy";
	return "unknown";
}

/**
 * gcm_profile_get_property:
 **/
static void
gcm_profile_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GcmProfile *profile = GCM_PROFILE (object);
	GcmProfilePrivate *priv = profile->priv;

	switch (prop_id) {
	case PROP_COPYRIGHT:
		g_value_set_string (value, priv->copyright);
		break;
	case PROP_MANUFACTURER:
		g_value_set_string (value, priv->manufacturer);
		break;
	case PROP_MODEL:
		g_value_set_string (value, priv->model);
		break;
	case PROP_DATETIME:
		g_value_set_string (value, priv->datetime);
		break;
	case PROP_DESCRIPTION:
		g_value_set_string (value, priv->description);
		break;
	case PROP_FILENAME:
		g_value_set_string (value, priv->filename);
		break;
	case PROP_TYPE:
		g_value_set_uint (value, priv->profile_type);
		break;
	case PROP_COLORSPACE:
		g_value_set_uint (value, priv->colorspace);
		break;
	case PROP_SIZE:
		g_value_set_uint (value, priv->size);
		break;
	case PROP_WHITE_POINT:
		g_value_set_object (value, priv->white_point);
		break;
	case PROP_BLACK_POINT:
		g_value_set_object (value, priv->black_point);
		break;
	case PROP_LUMINANCE_RED:
		g_value_set_object (value, priv->luminance_red);
		break;
	case PROP_LUMINANCE_GREEN:
		g_value_set_object (value, priv->luminance_green);
		break;
	case PROP_LUMINANCE_BLUE:
		g_value_set_object (value, priv->luminance_blue);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * gcm_profile_set_property:
 **/
static void
gcm_profile_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GcmProfile *profile = GCM_PROFILE (object);
	GcmProfilePrivate *priv = profile->priv;

	switch (prop_id) {
	case PROP_COPYRIGHT:
		g_free (priv->copyright);
		priv->copyright = g_strdup (g_value_get_string (value));
		if (priv->copyright != NULL)
			gcm_utils_ensure_printable (priv->copyright);
		break;
	case PROP_MANUFACTURER:
		g_free (priv->manufacturer);
		priv->manufacturer = g_strdup (g_value_get_string (value));
		if (priv->manufacturer != NULL)
			gcm_utils_ensure_printable (priv->manufacturer);
		break;
	case PROP_MODEL:
		g_free (priv->model);
		priv->model = g_strdup (g_value_get_string (value));
		if (priv->model != NULL)
			gcm_utils_ensure_printable (priv->model);
		break;
	case PROP_DATETIME:
		g_free (priv->datetime);
		priv->datetime = g_strdup (g_value_get_string (value));
		break;
	case PROP_DESCRIPTION:
		g_free (priv->description);
		priv->description = g_strdup (g_value_get_string (value));
		if (priv->description != NULL)
			gcm_utils_ensure_printable (priv->description);

		/* some profile_lcms1s have _really_ long titles - Microsoft, I'm looking at you... */
		if (priv->description != NULL)
			gcm_utils_ensure_sane_length (priv->description, 80);

		/* there's nothing sensible to display */
		if (priv->description == NULL || priv->description[0] == '\0') {
			g_free (priv->description);
			if (priv->filename != NULL) {
				priv->description = g_path_get_basename (priv->filename);
			} else {
				/* TRANSLATORS: this is where the ICC profile_lcms1 has no description */
				priv->description = _("Missing description");
			}
		}
		break;
	case PROP_FILENAME:
		g_free (priv->filename);
		priv->filename = g_strdup (g_value_get_string (value));
		break;
	case PROP_TYPE:
		priv->profile_type = g_value_get_uint (value);
		break;
	case PROP_COLORSPACE:
		priv->colorspace = g_value_get_uint (value);
		break;
	case PROP_SIZE:
		priv->size = g_value_get_uint (value);
		break;
	case PROP_WHITE_POINT:
		priv->white_point = g_value_dup_object (value);
		break;
	case PROP_BLACK_POINT:
		priv->black_point = g_value_dup_object (value);
		break;
	case PROP_LUMINANCE_RED:
		priv->luminance_red = g_value_dup_object (value);
		break;
	case PROP_LUMINANCE_GREEN:
		priv->luminance_green = g_value_dup_object (value);
		break;
	case PROP_LUMINANCE_BLUE:
		priv->luminance_blue = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * gcm_profile_class_init:
 **/
static void
gcm_profile_class_init (GcmProfileClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gcm_profile_finalize;
	object_class->get_property = gcm_profile_get_property;
	object_class->set_property = gcm_profile_set_property;

	/**
	 * GcmProfile:copyright:
	 */
	pspec = g_param_spec_string ("copyright", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_COPYRIGHT, pspec);

	/**
	 * GcmProfile:manufacturer:
	 */
	pspec = g_param_spec_string ("manufacturer", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_MANUFACTURER, pspec);

	/**
	 * GcmProfile:model:
	 */
	pspec = g_param_spec_string ("model", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_MODEL, pspec);

	/**
	 * GcmProfile:datetime:
	 */
	pspec = g_param_spec_string ("datetime", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DATETIME, pspec);

	/**
	 * GcmProfile:description:
	 */
	pspec = g_param_spec_string ("description", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DESCRIPTION, pspec);

	/**
	 * GcmProfile:filename:
	 */
	pspec = g_param_spec_string ("filename", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_FILENAME, pspec);

	/**
	 * GcmProfile:type:
	 */
	pspec = g_param_spec_uint ("type", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TYPE, pspec);

	/**
	 * GcmProfile:colorspace:
	 */
	pspec = g_param_spec_uint ("colorspace", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_COLORSPACE, pspec);

	/**
	 * GcmProfile:size:
	 */
	pspec = g_param_spec_uint ("size", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SIZE, pspec);

	/**
	 * GcmProfile:white-point:
	 */
	pspec = g_param_spec_object ("white-point", NULL, NULL,
				     GCM_TYPE_XYZ,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_WHITE_POINT, pspec);

	/**
	 * GcmProfile:black-point:
	 */
	pspec = g_param_spec_object ("black-point", NULL, NULL,
				     GCM_TYPE_XYZ,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_BLACK_POINT, pspec);

	/**
	 * GcmProfile:luminance-red:
	 */
	pspec = g_param_spec_object ("luminance-red", NULL, NULL,
				     GCM_TYPE_XYZ,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LUMINANCE_RED, pspec);

	/**
	 * GcmProfile:luminance-green:
	 */
	pspec = g_param_spec_object ("luminance-green", NULL, NULL,
				     GCM_TYPE_XYZ,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LUMINANCE_GREEN, pspec);

	/**
	 * GcmProfile:luminance-blue:
	 */
	pspec = g_param_spec_object ("luminance-blue", NULL, NULL,
				     GCM_TYPE_XYZ,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LUMINANCE_BLUE, pspec);

	g_type_class_add_private (klass, sizeof (GcmProfilePrivate));
}

/**
 * gcm_profile_init:
 **/
static void
gcm_profile_init (GcmProfile *profile)
{
	profile->priv = GCM_PROFILE_GET_PRIVATE (profile);
	profile->priv->profile_type = GCM_PROFILE_TYPE_UNKNOWN;
	profile->priv->colorspace = GCM_PROFILE_COLORSPACE_UNKNOWN;
	profile->priv->white_point = gcm_xyz_new ();
	profile->priv->black_point = gcm_xyz_new ();
	profile->priv->luminance_red = gcm_xyz_new ();
	profile->priv->luminance_green = gcm_xyz_new ();
	profile->priv->luminance_blue = gcm_xyz_new ();
}

/**
 * gcm_profile_finalize:
 **/
static void
gcm_profile_finalize (GObject *object)
{
	GcmProfile *profile = GCM_PROFILE (object);
	GcmProfilePrivate *priv = profile->priv;

	g_free (priv->copyright);
	g_free (priv->description);
	g_free (priv->filename);
	g_free (priv->manufacturer);
	g_free (priv->model);
	g_free (priv->datetime);
	g_object_unref (priv->white_point);
	g_object_unref (priv->black_point);
	g_object_unref (priv->luminance_red);
	g_object_unref (priv->luminance_green);
	g_object_unref (priv->luminance_blue);

	G_OBJECT_CLASS (gcm_profile_parent_class)->finalize (object);
}

/**
 * gcm_profile_new:
 *
 * Return value: a new GcmProfile object.
 **/
GcmProfile *
gcm_profile_new (void)
{
	GcmProfile *profile;
	profile = g_object_new (GCM_TYPE_PROFILE, NULL);
	return GCM_PROFILE (profile);
}

/**
 * gcm_profile_default_new:
 *
 * Return value: a new GcmProfile object.
 **/
GcmProfile *
gcm_profile_default_new (void)
{
	GcmProfile *profile = NULL;
#if 1
	profile = GCM_PROFILE (gcm_profile_lcms1_new ());
#endif
	return profile;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include <math.h>
#include "egg-test.h"

typedef struct {
	const gchar *copyright;
	const gchar *manufacturer;
	const gchar *model;
	const gchar *datetime;
	const gchar *description;
	GcmProfileType type;
	GcmProfileColorspace colorspace;
	gfloat luminance;
} GcmProfileTestData;

void
gcm_profile_test_parse_file (EggTest *test, const guint8 *datafile, GcmProfileTestData *test_data)
{
	gchar *filename;
	gchar *filename_tmp;
	gchar *copyright;
	gchar *manufacturer;
	gchar *model;
	gchar *datetime;
	gchar *description;
	gchar *ascii_string;
	gchar *pnp_id;
	gchar *data;
	guint width;
	guint type;
	guint colorspace;
	gfloat gamma;
	gboolean ret;
	GError *error = NULL;
	GcmProfile *profile_lcms1;
	GcmXyz *xyz;
	gfloat luminance;

	/************************************************************/
	egg_test_title (test, "get a profile_lcms1 object");
	profile_lcms1 = GCM_PROFILE(gcm_profile_lcms1_new ());
	egg_test_assert (test, profile_lcms1 != NULL);

	/************************************************************/
	egg_test_title (test, "get filename of data file");
	filename = egg_test_get_data_file (datafile);
	egg_test_assert (test, (filename != NULL));

	/************************************************************/
	egg_test_title (test, "load ICC file");
	ret = gcm_profile_parse (profile_lcms1, filename, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to parse: %s", error->message);

	/* get some properties */
	g_object_get (profile_lcms1,
		      "copyright", &copyright,
		      "manufacturer", &manufacturer,
		      "model", &model,
		      "datetime", &datetime,
		      "description", &description,
		      "filename", &filename_tmp,
		      "type", &type,
		      "colorspace", &colorspace,
		      NULL);

	/************************************************************/
	egg_test_title (test, "check filename for %s", datafile);
	if (g_strcmp0 (filename, filename_tmp) == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value: %s, expecting: %s", filename, filename_tmp);

	/************************************************************/
	egg_test_title (test, "check copyright for %s", datafile);
	if (g_strcmp0 (copyright, test_data->copyright) == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value: %s, expecting: %s", copyright, test_data->copyright);

	/************************************************************/
	egg_test_title (test, "check manufacturer for %s", datafile);
	if (g_strcmp0 (manufacturer, test_data->manufacturer) == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value: %s, expecting: %s", manufacturer, test_data->manufacturer);

	/************************************************************/
	egg_test_title (test, "check model for %s", datafile);
	if (g_strcmp0 (model, test_data->model) == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value: %s, expecting: %s", model, test_data->model);

	/************************************************************/
	egg_test_title (test, "check datetime for %s", datafile);
	if (g_strcmp0 (datetime, test_data->datetime) == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value: %s, expecting: %s", datetime, test_data->datetime);

	/************************************************************/
	egg_test_title (test, "check description for %s", datafile);
	if (g_strcmp0 (description, test_data->description) == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value: %s, expecting: %s", description, test_data->description);

	/************************************************************/
	egg_test_title (test, "check type for %s", datafile);
	if (type == test_data->type)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value: %i, expecting: %i", type, test_data->type);

	/************************************************************/
	egg_test_title (test, "check colorspace for %s", datafile);
	if (colorspace == test_data->colorspace)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value: %i, expecting: %i", colorspace, test_data->colorspace);

	/************************************************************/
	egg_test_title (test, "check luminance red %s", datafile);
	g_object_get (profile_lcms1,
		      "luminance-red", &xyz,
		      NULL);
	luminance = gcm_xyz_get_x (xyz);
	if (fabs (luminance - test_data->luminance) < 0.001)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid value: %f, expecting: %f", luminance, test_data->luminance);

	g_object_unref (xyz);
	g_object_unref (profile_lcms1);
	g_free (copyright);
	g_free (manufacturer);
	g_free (model);
	g_free (datetime);
	g_free (description);
	g_free (data);
	g_free (filename);
	g_free (filename_tmp);
}

void
gcm_profile_test (EggTest *test)
{
	GcmProfileTestData test_data;

	if (!egg_test_start (test, "GcmProfile"))
		return;

	/* bluish test */
	test_data.copyright = "Copyright (c) 1998 Hewlett-Packard Company";
	test_data.manufacturer = "IEC http://www.iec.ch";
	test_data.model = "IEC 61966-2.1 Default RGB colour space - sRGB";
	test_data.description = "Blueish Test";
	test_data.type = GCM_PROFILE_TYPE_DISPLAY_DEVICE;
	test_data.colorspace = GCM_PROFILE_COLORSPACE_RGB;
	test_data.luminance = 0.648454;
	test_data.datetime = "9 February 1998, 06:49:00";
	gcm_profile_test_parse_file (test, "bluish.icc", &test_data);

	/* Adobe test */
	test_data.copyright = "Copyright (c) 1998 Hewlett-Packard Company Modified using Adobe Gamma";
	test_data.manufacturer = "IEC http://www.iec.ch";
	test_data.model = "IEC 61966-2.1 Default RGB colour space - sRGB";
	test_data.description = "ADOBEGAMMA-Test";
	test_data.type = GCM_PROFILE_TYPE_DISPLAY_DEVICE;
	test_data.colorspace = GCM_PROFILE_COLORSPACE_RGB;
	test_data.luminance = 0.648446;
	test_data.datetime = "16 August 2005, 21:49:54";
	gcm_profile_test_parse_file (test, "AdobeGammaTest.icm", &test_data);

	egg_test_end (test);
}
#endif

