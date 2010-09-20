/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __GCM_SENSOR_COLORMUNKI_H
#define __GCM_SENSOR_COLORMUNKI_H

#include <glib-object.h>

#include "gcm-sensor.h"

G_BEGIN_DECLS

#define GCM_TYPE_SENSOR_COLORMUNKI		(gcm_sensor_colormunki_get_type ())
#define GCM_SENSOR_COLORMUNKI(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GCM_TYPE_SENSOR_COLORMUNKI, GcmSensorColormunki))
#define GCM_SENSOR_COLORMUNKI_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GCM_TYPE_SENSOR_COLORMUNKI, GcmSensorColormunkiClass))
#define GCM_IS_SENSOR_COLORMUNKI(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GCM_TYPE_SENSOR_COLORMUNKI))
#define GCM_IS_SENSOR_COLORMUNKI_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GCM_TYPE_SENSOR_COLORMUNKI))
#define GCM_SENSOR_COLORMUNKI_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GCM_TYPE_SENSOR_COLORMUNKI, GcmSensorColormunkiClass))

typedef struct _GcmSensorColormunkiPrivate	GcmSensorColormunkiPrivate;
typedef struct _GcmSensorColormunki		GcmSensorColormunki;
typedef struct _GcmSensorColormunkiClass	GcmSensorColormunkiClass;

struct _GcmSensorColormunki
{
	 GcmSensor			 parent;
	 GcmSensorColormunkiPrivate	*priv;
};

struct _GcmSensorColormunkiClass
{
	GcmSensorClass	parent_class;
};

GType		 gcm_sensor_colormunki_get_type		 (void);
GcmSensor	*gcm_sensor_colormunki_new		 (void);

G_END_DECLS

#endif /* __GCM_SENSOR_COLORMUNKI_H */
