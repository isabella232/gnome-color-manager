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

/**
 * SECTION:gcm-sensor-colormunki
 * @short_description: functionality to talk to the ColorMunki colorimeter.
 *
 * This object contains all the low level logic for the ColorMunki hardware.
 */

#include "config.h"

#include <glib-object.h>
#include <libusb-1.0/libusb.h>

#include "egg-debug.h"
#include "gcm-common.h"
#include "gcm-buffer.h"
#include "gcm-usb.h"
#include "gcm-sensor-colormunki.h"

static void     gcm_sensor_colormunki_finalize	(GObject     *object);

#define GCM_SENSOR_COLORMUNKI_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GCM_TYPE_SENSOR_COLORMUNKI, GcmSensorColormunkiPrivate))

typedef enum {
	GCM_COLORMUNKI_DIAL_POSITION_UNKNOWN,
	GCM_COLORMUNKI_DIAL_POSITION_PROJECTOR,
	GCM_COLORMUNKI_DIAL_POSITION_SURFACE,
	GCM_COLORMUNKI_DIAL_POSITION_CALIBRATION,
	GCM_COLORMUNKI_DIAL_POSITION_AMBIENT,
	GCM_COLORMUNKI_DIAL_POSITION_LAST
} GcmSensorColormunkiDialPosition;

/**
 * GcmSensorColormunkiPrivate:
 *
 * Private #GcmSensorColormunki data
 **/
struct _GcmSensorColormunkiPrivate
{
	struct libusb_transfer		*transfer_interrupt;
	struct libusb_transfer		*transfer_state;
	GcmUsb				*usb;
	GcmSensorColormunkiDialPosition	 dial_position;
	gchar				*version_string;
	gchar				*chip_id;
	gchar				*firmware_revision;
	guint32				 tick_duration;
	guint32				 min_int;
	guint32				 eeprom_blocks;
	guint32				 eeprom_blocksize;
};

G_DEFINE_TYPE (GcmSensorColormunki, gcm_sensor_colormunki, GCM_TYPE_SENSOR)

#define COLORMUNKI_VENDOR_ID			0x0971
#define COLORMUNKI_PRODUCT_ID			0x2007

#define COLORMUNKI_COMMAND_DIAL_ROTATE		0x00
#define COLORMUNKI_COMMAND_BUTTON_PRESSED	0x01
#define COLORMUNKI_COMMAND_BUTTON_RELEASED	0x02

#define	COLORMUNKI_BUTTON_STATE_RELEASED	0x00
#define	COLORMUNKI_BUTTON_STATE_PRESSED		0x01

#define	COLORMUNKI_DIAL_POSITION_PROJECTOR	0x00
#define	COLORMUNKI_DIAL_POSITION_SURFACE	0x01
#define	COLORMUNKI_DIAL_POSITION_CALIBRATION	0x02
#define	COLORMUNKI_DIAL_POSITION_AMBIENT	0x03

/**
 * gcm_sensor_colormunki_print_data:
 **/
static void
gcm_sensor_colormunki_print_data (const gchar *title, const guchar *data, gsize length)
{
	guint i;

	if (!egg_debug_is_verbose ())
		return;

	if (g_strcmp0 (title, "request") == 0)
		g_print ("%c[%dm", 0x1B, 31);
	if (g_strcmp0 (title, "reply") == 0)
		g_print ("%c[%dm", 0x1B, 34);
	g_print ("%s\t", title);

	for (i=0; i< length; i++)
		g_print ("%02x [%c]\t", data[i], g_ascii_isprint (data[i]) ? data[i] : '?');

	g_print ("%c[%dm\n", 0x1B, 0);
}

static void
gcm_sensor_colormunki_submit_transfer (GcmSensorColormunki *sensor_colormunki);


/**
 * gcm_sensor_colormunki_refresh_state_transfer_cb:
 **/
static void
gcm_sensor_colormunki_refresh_state_transfer_cb (struct libusb_transfer *transfer)
{
	GcmSensorColormunki *sensor_colormunki = GCM_SENSOR_COLORMUNKI (transfer->user_data);
	GcmSensorColormunkiPrivate *priv = sensor_colormunki->priv;
	guint8 *reply = transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		egg_warning ("did not succeed");
		goto out;
	}

	/*
	 * Returns:  00 00
	 *           |/ ||
	 * dial pos -/  \--- button value
	 * - 00 = projector
	 * - 01 = surface
	 * - 02 = calibration
	 * - 03 = ambient
	 */
	if (reply[0] == COLORMUNKI_DIAL_POSITION_PROJECTOR) {
		egg_debug ("now projector");
		priv->dial_position = GCM_COLORMUNKI_DIAL_POSITION_PROJECTOR;
	} else if (reply[0] == COLORMUNKI_DIAL_POSITION_SURFACE) {
		egg_debug ("now surface");
		priv->dial_position = GCM_COLORMUNKI_DIAL_POSITION_SURFACE;
	} else if (reply[0] == COLORMUNKI_DIAL_POSITION_CALIBRATION) {
		egg_debug ("now calibration");
		priv->dial_position = GCM_COLORMUNKI_DIAL_POSITION_CALIBRATION;
	} else if (reply[0] == COLORMUNKI_DIAL_POSITION_AMBIENT) {
		egg_debug ("now ambient");
		priv->dial_position = GCM_COLORMUNKI_DIAL_POSITION_AMBIENT;
	} else {
		egg_warning ("dial position unknown: 0x%02x", reply[0]);
		priv->dial_position = GCM_COLORMUNKI_DIAL_POSITION_UNKNOWN;
	}

	/* button state */
	if (reply[1] == COLORMUNKI_BUTTON_STATE_RELEASED) {
		egg_debug ("button released");
	} else if (reply[1] == COLORMUNKI_BUTTON_STATE_PRESSED) {
		egg_debug ("button pressed");
	} else {
		egg_warning ("switch state unknown: 0x%02x", reply[1]);
	}

	gcm_sensor_colormunki_print_data ("reply", transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE, transfer->actual_length);
out:
	g_free (transfer->buffer);
}

/**
 * gcm_sensor_colormunki_refresh_state:
 **/
static gboolean
gcm_sensor_colormunki_refresh_state (GcmSensorColormunki *sensor_colormunki, GError **error)
{
	gint retval;
	gboolean ret = FALSE;
	static guchar *request;
	libusb_device_handle *handle;
	GcmSensorColormunkiPrivate *priv = sensor_colormunki->priv;

	/* do sync request */
	handle = gcm_usb_get_device_handle (priv->usb);

	/* request new button state */
	request = g_new0 (guchar, LIBUSB_CONTROL_SETUP_SIZE + 2);
	libusb_fill_control_setup (request, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE, 0x87, 0x00, 0, 2);
	libusb_fill_control_transfer (priv->transfer_state, handle, request, &gcm_sensor_colormunki_refresh_state_transfer_cb, sensor_colormunki, 2000);

	/* submit transfer */
	retval = libusb_submit_transfer (priv->transfer_state);
	if (retval < 0) {
		egg_warning ("failed to submit transfer: %s", libusb_strerror (retval));
		goto out;
	}

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * gcm_sensor_colormunki_transfer_cb:
 **/
static void
gcm_sensor_colormunki_transfer_cb (struct libusb_transfer *transfer)
{
	guint32 timestamp;
	GcmSensorColormunki *sensor_colormunki = GCM_SENSOR_COLORMUNKI (transfer->user_data);
	guint8 *reply = transfer->buffer;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		egg_warning ("did not succeed");
		return;
	}

	/*
	 *   subcmd ----\       /------------ 32 bit event time
	 *  cmd ----|\ ||       || || || ||
	 * Returns: 02 00 00 00 ac 62 07 00
	 * always zero ---||-||
	 *
	 * cmd is:
	 * 00	dial rotate
	 * 01	button pressed
	 * 02	button released
	 *
	 * subcmd is:
	 * 00	button event
	 * 01	dial rotate
	 */
	gcm_sensor_colormunki_print_data ("reply", transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE, transfer->actual_length);
	timestamp = (reply[7] << 24) + (reply[6] << 16) + (reply[5] << 8) + (reply[4] << 0);
	/* we only care when the button is pressed */
	if (reply[0] == COLORMUNKI_COMMAND_BUTTON_RELEASED) {
		egg_debug ("ignoring button released");
		goto out;
	}

	if (reply[0] == COLORMUNKI_COMMAND_DIAL_ROTATE) {
		egg_warning ("dial rotate at %ims", timestamp);
	} else if (reply[0] == COLORMUNKI_COMMAND_BUTTON_PRESSED) {
		egg_debug ("button pressed at %ims", timestamp);
		gcm_sensor_button_pressed (GCM_SENSOR (sensor_colormunki));
	}

	/* get the device state */
	gcm_sensor_colormunki_refresh_state (sensor_colormunki, NULL);

out:
	/* get the next bit of data */
	g_free (transfer->buffer);
	gcm_sensor_colormunki_submit_transfer (sensor_colormunki);
}

/**
 * gcm_sensor_colormunki_submit_transfer:
 **/
static void
gcm_sensor_colormunki_submit_transfer (GcmSensorColormunki *sensor_colormunki)
{
	gint retval;
	guchar *reply;
	libusb_device_handle *handle;
	GcmSensorColormunkiPrivate *priv = sensor_colormunki->priv;

	reply = g_new0 (guchar, 8);
	handle = gcm_usb_get_device_handle (priv->usb);
	libusb_fill_interrupt_transfer (priv->transfer_interrupt,
					handle, 0x83, reply, 8,
					gcm_sensor_colormunki_transfer_cb,
					sensor_colormunki, -1);

	egg_debug ("submitting transfer");
	retval = libusb_submit_transfer (priv->transfer_interrupt);
	if (retval < 0)
		egg_warning ("failed to submit transfer: %s", libusb_strerror (retval));
}

/**
 * gcm_sensor_colormunki_playdo:
 **/
static gboolean
gcm_sensor_colormunki_playdo (GcmSensor *sensor, GError **error)
{
	gboolean ret = TRUE;
	GcmSensorColormunki *sensor_colormunki = GCM_SENSOR_COLORMUNKI (sensor);
//	GcmSensorColormunkiPrivate *priv = sensor_colormunki->priv;

	egg_debug ("submit transfer");
	gcm_sensor_colormunki_submit_transfer (sensor_colormunki);
	ret = gcm_sensor_colormunki_refresh_state (sensor_colormunki, error);
//out:
	return ret;
}

/**
 * gcm_sensor_colormunki_startup:
 **/
static gboolean
gcm_sensor_colormunki_startup (GcmSensor *sensor, GError **error)
{
	gint retval;
	libusb_device_handle *handle;
	guchar buffer[36];
	gboolean ret = FALSE;
	GcmSensorColormunki *sensor_colormunki = GCM_SENSOR_COLORMUNKI (sensor);
	GcmSensorColormunkiPrivate *priv = sensor_colormunki->priv;

	/* connect */
	ret = gcm_usb_connect (priv->usb,
			       COLORMUNKI_VENDOR_ID, COLORMUNKI_PRODUCT_ID,
			       0x01, 0x00, error);
	if (!ret)
		goto out;

	/* attach to the default mainloop */
	gcm_usb_attach_to_context (priv->usb, NULL);

	/* get firmware parameters */
	handle = gcm_usb_get_device_handle (priv->usb);
	retval = libusb_control_transfer (handle,
					  LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
					  0x86, 0, 0, buffer, 24, 2000);
	if (retval < 0) {
		g_set_error (error, GCM_SENSOR_ERROR,
			     GCM_SENSOR_ERROR_NO_SUPPORT,
			     "failed to get firmware parameters: %s",
			     libusb_strerror (retval));
		goto out;
	}
	priv->firmware_revision = g_strdup_printf ("%i.%i", gcm_buffer_read_uint16_le (buffer), gcm_buffer_read_uint16_le (buffer+4));
	priv->tick_duration = gcm_buffer_read_uint16_le (buffer+8);
	priv->min_int = gcm_buffer_read_uint16_le (buffer+0x0c);
	priv->eeprom_blocks = gcm_buffer_read_uint16_le (buffer+0x10);
	priv->eeprom_blocksize = gcm_buffer_read_uint16_le (buffer+0x14);

	/* get chip ID */
	retval = libusb_control_transfer (handle,
					  LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
					  0x8A, 0, 0, buffer, 8, 2000);
	if (retval < 0) {
		g_set_error (error, GCM_SENSOR_ERROR,
			     GCM_SENSOR_ERROR_NO_SUPPORT,
			     "failed to get chip id parameters: %s",
			     libusb_strerror (retval));
		goto out;
	}
	priv->chip_id = g_strdup_printf ("%02x-%02x%02x%02x%02x%02x%02x%02x",
					 buffer[0], buffer[1], buffer[2], buffer[3],
					 buffer[4], buffer[5], buffer[6], buffer[7]);

	/* get version string */
	priv->version_string = g_new0 (gchar, 36);
	retval = libusb_control_transfer (handle,
					  LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
					  0x85, 0, 0, (guchar*) priv->version_string, 36, 2000);
	if (retval < 0) {
		g_set_error (error, GCM_SENSOR_ERROR,
			     GCM_SENSOR_ERROR_NO_SUPPORT,
			     "failed to get version string: %s",
			     libusb_strerror (retval));
		goto out;
	}

	/* print details */
	egg_debug ("Chip ID\t%s", priv->chip_id);
	egg_debug ("Version\t%s", priv->version_string);
	egg_debug ("Firmware\tfirmware_revision=%s, tick_duration=%i, min_int=%i, eeprom_blocks=%i, eeprom_blocksize=%i",
		   priv->firmware_revision, priv->tick_duration, priv->min_int, priv->eeprom_blocks, priv->eeprom_blocksize);

	/* do unknown cool stuff */
	ret = gcm_sensor_colormunki_playdo (sensor, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * gcm_sensor_colormunki_get_ambient:
 **/
static gboolean
gcm_sensor_colormunki_get_ambient (GcmSensor *sensor, gdouble *value, GError **error)
{
	gboolean ret = FALSE;
	GcmSensorColormunki *sensor_colormunki = GCM_SENSOR_COLORMUNKI (sensor);

	/* no hardware support */
	if (sensor_colormunki->priv->dial_position != GCM_COLORMUNKI_DIAL_POSITION_AMBIENT) {
		g_set_error_literal (error, GCM_SENSOR_ERROR,
				     GCM_SENSOR_ERROR_NO_SUPPORT,
				     "Cannot measure ambient light in this mode (turn dial!)");
		goto out;
	}

/*
 * ioctl(3, USBDEVFS_SUBMITURB or USBDEVFS_SUBMITURB32, {type=3, endpoint=129, status=0, flags=0, buffer_length=1096, actual_length=0, start_frame=0, number_of_packets=0, error_count=0, signr=0, usercontext=(nil), buffer=00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ) = 0
 * ioctl(3, USBDEVFS_CONTROL or USBDEVFS_CONTROL32, {requesttype=64, request=128, value=0, index=0, length=12, timeout=2000, data=00 00 01 00 b7 3e 00 00 02 00 00 00 ) = 12
 * 
 * ioctl(3, USBDEVFS_SUBMITURB or USBDEVFS_SUBMITURB32, {type=3, endpoint=129, status=0, flags=0, buffer_length=548, actual_length=0, start_frame=0, number_of_packets=0, error_count=0, signr=0, usercontext=(nil), buffer=d0 a3 9d 00 d0 a3 9d 00 00 00 00 00 00 00 00 00 00 d0 86 40 bf c6 fa 21 a4 4b 61 40 0b 24 0c d6 7a 29 04 40 91 3a 0e c7 f9 28 04 40 c0 b1 55 bc 9b 28 04 40 b9 d3 41 53 86 6a 07 40 df 23 db 4d 0c e3 06 40 20 5c bf 4d b2 53 05 40 5f 28 38 74 26 44 07 40 e9 45 b7 e4 2f a5 08 40 bb a2 87 d7 8c db 07 40 34 90 30 b1 f3 a1 06 40 b0 8f fa 63 84 98 05 40 35 1f 09 07 97 47 04 40 53 ac 8a be ) = 0
 * 
 * ioctl(3, USBDEVFS_REAPURBNDELAY or USBDEVFS_REAPURBNDELAY32, {type=3, endpoint=129, status=0, flags=0, buffer_length=548, actual_length=548, start_frame=0, number_of_packets=0, error_count=0, signr=0, usercontext=(nil), buffer=de 07 da 07 d6 07 d8 07 d6 07 16 08 29 0b 79 0d 22 12 f2 17 b4 1c 31 20 4b 22 e2 22 7b 22 a8 21 93 20 eb 1e 2d 1d fe 1b 1c 1b e5 19 69 19 c8 19 b5 19 8a 18 16 17 a4 15 86 14 ac 13 e8 12 22 12 20 12 bf 12 8e 13 d2 13 de 13 ea 13 fb 13 39 14 89 14 bd 14 ec 14 8b 15 78 16 69 17 99 18 ca 19 97 1a 14 1b 6f 1b b5 1b 7f 1c 98 1d 59 1e a9 1e af 1e 71 1e d2 1d db 1c c1 1b d4 1a 50 1a 46 1a }) = 0
 * write(1, " Result is XYZ: 126.685284 136.9"..., 91 Result is XYZ: 126.685284 136.946975 206.789116, D50 Lab: 112.817679 -7.615524 -49.589593
) = 91
* write(1, " Ambient = 430.2 Lux, CCT = 1115"..., 54 Ambient = 430.2 Lux, CCT = 11152K (Delta E 9.399372)
 */

	/* success */
	ret = TRUE;
out:
	return ret;
}


/**
 * gcm_sensor_huey_dump:
 **/
static gboolean
gcm_sensor_colormunki_dump (GcmSensor *sensor, GString *data, GError **error)
{
	gboolean ret = TRUE;
	GcmSensorColormunki *sensor_colormunki = GCM_SENSOR_COLORMUNKI (sensor);
	GcmSensorColormunkiPrivate *priv = sensor_colormunki->priv;

	/* dump the unlock string */
	g_string_append_printf (data, "colormunki-dump-version: %i\n", 1);
	g_string_append_printf (data, "chip-id:%s", priv->chip_id);
	g_string_append_printf (data, "version:%s", priv->version_string);
	g_string_append_printf (data, "firmware-revision:%s", priv->firmware_revision);
	g_string_append_printf (data, "tick-duration:%i", priv->tick_duration);
	g_string_append_printf (data, "min-int:%i", priv->min_int);
	g_string_append_printf (data, "eeprom-blocks:%i", priv->eeprom_blocks);
	g_string_append_printf (data, "eeprom-blocksize:%i", priv->eeprom_blocksize);
//out:
	return ret;
}

/**
 * gcm_sensor_colormunki_class_init:
 **/
static void
gcm_sensor_colormunki_class_init (GcmSensorColormunkiClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GcmSensorClass *parent_class = GCM_SENSOR_CLASS (klass);
	object_class->finalize = gcm_sensor_colormunki_finalize;

	/* setup klass links */
	parent_class->get_ambient = gcm_sensor_colormunki_get_ambient;
//	parent_class->set_leds = gcm_sensor_colormunki_set_leds;
//	parent_class->sample = gcm_sensor_colormunki_sample;
	parent_class->startup = gcm_sensor_colormunki_startup;
	parent_class->dump = gcm_sensor_colormunki_dump;

	g_type_class_add_private (klass, sizeof (GcmSensorColormunkiPrivate));
}

/**
 * gcm_sensor_colormunki_init:
 **/
static void
gcm_sensor_colormunki_init (GcmSensorColormunki *sensor)
{
	GcmSensorColormunkiPrivate *priv;
	priv = sensor->priv = GCM_SENSOR_COLORMUNKI_GET_PRIVATE (sensor);
	priv->transfer_interrupt = libusb_alloc_transfer (0);
	priv->transfer_state = libusb_alloc_transfer (0);
	priv->usb = gcm_usb_new ();
}

/**
 * gcm_sensor_colormunki_finalize:
 **/
static void
gcm_sensor_colormunki_finalize (GObject *object)
{
	GcmSensorColormunki *sensor = GCM_SENSOR_COLORMUNKI (object);
	GcmSensorColormunkiPrivate *priv = sensor->priv;

	/* FIXME: cancel transfers if in progress */

	/* detach from the main loop */
	g_object_unref (priv->usb);

	/* free transfers */
	libusb_free_transfer (priv->transfer_interrupt);
	libusb_free_transfer (priv->transfer_state);
	g_free (priv->version_string);
	g_free (priv->chip_id);
	g_free (priv->firmware_revision);

	G_OBJECT_CLASS (gcm_sensor_colormunki_parent_class)->finalize (object);
}

/**
 * gcm_sensor_colormunki_new:
 *
 * Return value: a new #GcmSensor object.
 **/
GcmSensor *
gcm_sensor_colormunki_new (void)
{
	GcmSensorColormunki *sensor;
	sensor = g_object_new (GCM_TYPE_SENSOR_COLORMUNKI,
			       "native", TRUE,
			       NULL);
	return GCM_SENSOR (sensor);
}
