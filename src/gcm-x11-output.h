/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __GCM_X11_OUTPUT_H
#define __GCM_X11_OUTPUT_H

#include <glib-object.h>

#include "gcm-clut.h"
#include "gcm-edid.h"

G_BEGIN_DECLS

#define GCM_TYPE_X11_OUTPUT		(gcm_x11_output_get_type ())
#define GCM_X11_OUTPUT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GCM_TYPE_X11_OUTPUT, GcmX11Output))
#define GCM_IS_X11_OUTPUT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GCM_TYPE_X11_OUTPUT))

#define GCM_X11_OUTPUT_ERROR		1
#define GCM_X11_OUTPUT_ERROR_INTERNAL	0

typedef struct _GcmX11OutputPrivate	GcmX11OutputPrivate;
typedef struct _GcmX11Output		GcmX11Output;
typedef struct _GcmX11OutputClass	GcmX11OutputClass;

struct _GcmX11Output
{
	 GObject		 parent;
	 GcmX11OutputPrivate	*priv;
};

struct _GcmX11OutputClass
{
	GObjectClass		 parent_class;
};

GType		 gcm_x11_output_get_type		(void);
GcmX11Output	*gcm_x11_output_new			(void);

void		 gcm_x11_output_set_display		(GcmX11Output		*output,
							 gpointer		 display);
void		 gcm_x11_output_set_name		(GcmX11Output		*output,
							 const gchar		*name);
const gchar	*gcm_x11_output_get_name		(GcmX11Output		*output);
void		 gcm_x11_output_set_id			(GcmX11Output		*output,
							 guint			 id);
guint		 gcm_x11_output_get_id			(GcmX11Output		*output);
void		 gcm_x11_output_set_crtc_id		(GcmX11Output		*output,
							 guint			 crtc_id);
void		 gcm_x11_output_set_gamma_size		(GcmX11Output		*output,
							 guint			 gamma_size);
guint		 gcm_x11_output_get_gamma_size		(GcmX11Output		*output);
void		 gcm_x11_output_set_position		(GcmX11Output		*output,
							 guint			 x,
							 guint			 y);
void		 gcm_x11_output_get_position		(GcmX11Output		*output,
							 guint			*x,
							 guint			*y);
void		 gcm_x11_output_set_size		(GcmX11Output		*output,
							 guint			 width,
							 guint			 height);
void		 gcm_x11_output_get_size		(GcmX11Output		*output,
							 guint			*width,
							 guint			*height);
void		 gcm_x11_output_set_primary		(GcmX11Output		*output,
							 gboolean		 primary);
gboolean	 gcm_x11_output_get_primary		(GcmX11Output		*output);
void		 gcm_x11_output_set_connected		(GcmX11Output		*output,
							 gboolean		 connected);
gboolean	 gcm_x11_output_get_connected		(GcmX11Output		*output);
gboolean	 gcm_x11_output_get_gamma		(GcmX11Output		*output,
							 guint			*length,
							 guint16		**red,
							 guint16		**green,
							 guint16		**blue,
							 GError			**error);
gboolean	 gcm_x11_output_set_gamma		(GcmX11Output		*output,
							 guint			 length,
							 guint16		*red,
							 guint16		*green,
							 guint16		*blue,
							 GError			**error);
gboolean	 gcm_x11_output_set_gamma_from_clut	(GcmX11Output		*output,
							 GcmClut		*clut,
							 GError			**error);
GcmEdid		*gcm_x11_output_get_edid		(GcmX11Output		*output,
							 GError			**error);

G_END_DECLS

#endif /* __GCM_X11_OUTPUT_H */

