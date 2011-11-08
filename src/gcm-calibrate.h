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

#ifndef __GCM_CALIBRATE_H
#define __GCM_CALIBRATE_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <colord.h>

G_BEGIN_DECLS

#define GCM_TYPE_CALIBRATE		(gcm_calibrate_get_type ())
#define GCM_CALIBRATE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GCM_TYPE_CALIBRATE, GcmCalibrate))
#define GCM_CALIBRATE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GCM_TYPE_CALIBRATE, GcmCalibrateClass))
#define GCM_IS_CALIBRATE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GCM_TYPE_CALIBRATE))
#define GCM_IS_CALIBRATE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GCM_TYPE_CALIBRATE))
#define GCM_CALIBRATE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GCM_TYPE_CALIBRATE, GcmCalibrateClass))

typedef struct _GcmCalibratePrivate	GcmCalibratePrivate;
typedef struct _GcmCalibrate		GcmCalibrate;
typedef struct _GcmCalibrateClass	GcmCalibrateClass;

struct _GcmCalibrate
{
	 GObject			 parent;
	 GcmCalibratePrivate		*priv;
};

struct _GcmCalibrateClass
{
	GObjectClass	parent_class;
	/* vtable */
	gboolean	 (*calibrate_display)		(GcmCalibrate	*calibrate,
							 CdDevice	*device,
							 CdSensor	*sensor,
							 GtkWindow	*window,
							 GError		**error);
	gboolean	 (*calibrate_device)		(GcmCalibrate	*calibrate,
							 CdDevice	*device,
							 CdSensor	*sensor,
							 GtkWindow	*window,
							 GError		**error);
	gboolean	 (*calibrate_printer)		(GcmCalibrate	*calibrate,
							 CdDevice	*device,
							 CdSensor	*sensor,
							 GtkWindow	*window,
							 GError		**error);
	void		 (*interaction)			(GcmCalibrate	*calibrate,
							 GtkResponseType response);

	/* signal */
	void		(* title_changed)		(GcmCalibrate	*calibrate,
							 const gchar	*title);
	void		(* message_changed)		(GcmCalibrate	*calibrate,
							 const gchar	*message);
	void		(* image_changed)		(GcmCalibrate	*calibrate,
							 const gchar	*filename);
	void		(* progress_changed)		(GcmCalibrate	*calibrate,
							 guint		 percentage);
	void		(* interaction_required)	(GcmCalibrate	*calibrate,
							 const gchar	*button_text);
};

typedef enum {
	GCM_CALIBRATE_ERROR_USER_ABORT,
	GCM_CALIBRATE_ERROR_NO_SUPPORT,
	GCM_CALIBRATE_ERROR_NO_DATA,
	GCM_CALIBRATE_ERROR_INTERNAL
} GcmCalibrateError;

/* dummy */
#define GCM_CALIBRATE_ERROR	1

typedef enum {
	GCM_CALIBRATE_REFERENCE_KIND_CMP_DIGITAL_TARGET_3,
	GCM_CALIBRATE_REFERENCE_KIND_CMP_DT_003,
	GCM_CALIBRATE_REFERENCE_KIND_COLOR_CHECKER,
	GCM_CALIBRATE_REFERENCE_KIND_COLOR_CHECKER_DC,
	GCM_CALIBRATE_REFERENCE_KIND_COLOR_CHECKER_SG,
	GCM_CALIBRATE_REFERENCE_KIND_HUTCHCOLOR,
	GCM_CALIBRATE_REFERENCE_KIND_I1_RGB_SCAN_1_4,
	GCM_CALIBRATE_REFERENCE_KIND_IT8,
	GCM_CALIBRATE_REFERENCE_KIND_LASER_SOFT_DC_PRO,
	GCM_CALIBRATE_REFERENCE_KIND_QPCARD_201,
	GCM_CALIBRATE_REFERENCE_KIND_UNKNOWN
} GcmCalibrateReferenceKind;

typedef enum {
	GCM_CALIBRATE_DEVICE_KIND_CRT,
	GCM_CALIBRATE_DEVICE_KIND_LCD,
	GCM_CALIBRATE_DEVICE_KIND_PROJECTOR,
	GCM_CALIBRATE_DEVICE_KIND_UNKNOWN
} GcmCalibrateDisplayKind;

typedef enum {
	GCM_CALIBRATE_PRINT_KIND_LOCAL,
	GCM_CALIBRATE_PRINT_KIND_GENERATE,
	GCM_CALIBRATE_PRINT_KIND_ANALYZE,
	GCM_CALIBRATE_PRINT_KIND_UNKNOWN
} GcmCalibratePrintKind;

typedef enum {
	GCM_CALIBRATE_PRECISION_SHORT,
	GCM_CALIBRATE_PRECISION_NORMAL,
	GCM_CALIBRATE_PRECISION_LONG,
	GCM_CALIBRATE_PRECISION_UNKNOWN
} GcmCalibratePrecision;

GType		 gcm_calibrate_get_type			(void);
GcmCalibrate	*gcm_calibrate_new			(void);

/* designed for gcm-calibrate to call */
gboolean	 gcm_calibrate_device			(GcmCalibrate	*calibrate,
							 CdDevice	*device,
							 GtkWindow	*window,
							 GError		**error);
void		 gcm_calibrate_set_content_widget	(GcmCalibrate	*calibrate,
							 GtkWidget	*widget);
void		 gcm_calibrate_interaction		(GcmCalibrate	*calibrate,
							 GtkResponseType response);

/* designed for super-classes to call */
void		 gcm_calibrate_set_sensor		(GcmCalibrate	*calibrate,
							 CdSensor	*sensor);
void		 gcm_calibrate_set_title		(GcmCalibrate	*calibrate,
							 const gchar	*title);
void		 gcm_calibrate_set_message		(GcmCalibrate	*calibrate,
							 const gchar	*message);
void		 gcm_calibrate_set_image		(GcmCalibrate	*calibrate,
							 const gchar	*filename);
void		 gcm_calibrate_set_progress		(GcmCalibrate	*calibrate,
							 guint		 percentage);
void		 gcm_calibrate_pop			(GcmCalibrate	*calibrate);
void		 gcm_calibrate_interaction_required	(GcmCalibrate	*calibrate,
							 const gchar	*button_text);
GtkWidget	*gcm_calibrate_get_content_widget	(GcmCalibrate	*calibrate);

/* JUNK */
gboolean	 gcm_calibrate_set_from_exif		(GcmCalibrate	*calibrate,
							 const gchar	*filename,
							 GError		**error);
const gchar	*gcm_calibrate_get_filename_result	(GcmCalibrate	*calibrate);
const gchar	*gcm_calibrate_get_working_path		(GcmCalibrate	*calibrate);
const gchar	*gcm_calibrate_get_basename		(GcmCalibrate	*calibrate);

G_END_DECLS

#endif /* __GCM_CALIBRATE_H */

