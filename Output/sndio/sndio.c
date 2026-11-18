/*  XMMS - sndio output plugin
 *  Copyright (C) 2026 The CGTK authors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * The plugin's table, its settings, and its about and configure boxes.
 * Playback is in audio.c.
 */

#include "sndioout.h"

#include <sndio.h>
#include <libxmms/configfile.h>
#include <libxmms/util.h>
#include <libxmms/rateconf.h>

#define BUFFER_TIME_MIN 200
#define BUFFER_TIME_MAX 10000

struct sndio_config sndio_cfg;

static OutputDriver sndio_driver =
{
	NULL,
	NULL,
	NULL,
	OUTPUT_DRIVER_VERSION,
	0,
	sndio_init,
	sndio_cleanup,
	sndio_about,
	sndio_configure,
	sndio_open,
	sndio_write,
	sndio_delay,
	sndio_start,
	sndio_pause,
	sndio_flush,
	sndio_close,
	sndio_get_buffering,
	sndio_get_rate,
};

OutputDriver *get_output_driver_info(void)
{
	sndio_driver.description = g_strdup_printf(_("sndio %s output plugin"), VERSION);
	return &sndio_driver;
}

void sndio_init(void)
{
	ConfigFile *cfgfile;

	sndio_cfg.device = NULL;
	sndio_cfg.buffer_time = 1000;

	cfgfile = xmms_cfg_open_default_file();
	xmms_cfg_read_string(cfgfile, "sndio", "device", &sndio_cfg.device);
	xmms_cfg_read_int(cfgfile, "sndio", "buffer_time", &sndio_cfg.buffer_time);
	xmms_rate_read(cfgfile, "sndio", &sndio_cfg.output_rate, &sndio_cfg.resample_quality);
	xmms_cfg_free(cfgfile);

	if (sndio_cfg.device == NULL || *sndio_cfg.device == '\0')
	{
		g_free(sndio_cfg.device);
		sndio_cfg.device = g_strdup(SIO_DEVANY);
	}
}

/* XMMS's buffer for sndio: played from as soon as there is any. */
void sndio_get_buffering(int *buffer_ms, int *prebuffer)
{
	*buffer_ms = CLAMP(sndio_cfg.buffer_time, BUFFER_TIME_MIN, BUFFER_TIME_MAX);
	*prebuffer = 0;
}

void sndio_get_rate(int *rate, int *quality)
{
	*rate = sndio_cfg.output_rate;
	*quality = sndio_cfg.resample_quality;
}

void sndio_cleanup(void)
{
	sndio_close();
	g_free(sndio_cfg.device);
	sndio_cfg.device = NULL;
}

static void save_config(void)
{
	ConfigFile *cfgfile = xmms_cfg_open_default_file();

	xmms_cfg_write_string(cfgfile, "sndio", "device", sndio_cfg.device);
	xmms_cfg_write_int(cfgfile, "sndio", "buffer_time", sndio_cfg.buffer_time);
	xmms_rate_write(cfgfile, "sndio", sndio_cfg.output_rate, sndio_cfg.resample_quality);
	xmms_cfg_write_default_file(cfgfile);
	xmms_cfg_free(cfgfile);
}

void sndio_about(void)
{
	static GtkWidget *dialog;

	if (dialog != NULL)
		return;

	dialog = xmms_show_message(
		_("About sndio output"),
		_("XMMS sndio output plugin\n\n"
		  "Copyright (C) 2026 The CGTK authors\n\n"
		  "This program is free software; you can redistribute it and/or modify\n"
		  "it under the terms of the GNU General Public License as published by\n"
		  "the Free Software Foundation; either version 2 of the License, or\n"
		  "(at your option) any later version.\n"
		  "\n"
		  "This program is distributed in the hope that it will be useful,\n"
		  "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		  "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
		  "GNU General Public License for more details."),
		_("OK"), FALSE, NULL, NULL);
	gtk_signal_connect(GTK_OBJECT(dialog), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed), &dialog);
}

static GtkWidget *configure_win, *device_entry, *buffer_spin, *rate_frame;

static void configure_ok(GtkWidget *widget, gpointer data)
{
	g_free(sndio_cfg.device);
	sndio_cfg.device = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(device_entry))));
	if (*sndio_cfg.device == '\0')
	{
		g_free(sndio_cfg.device);
		sndio_cfg.device = g_strdup(SIO_DEVANY);
	}
	sndio_cfg.buffer_time =
		gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(buffer_spin));
	xmms_rate_frame_get(rate_frame, &sndio_cfg.output_rate, &sndio_cfg.resample_quality);

	save_config();
	gtk_widget_destroy(configure_win);
}

static GtkWidget *table_label(GtkWidget *table, gint row, const gchar *text)
{
	GtkWidget *label = gtk_label_new(text);

	gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row + 1,
			 GTK_FILL, 0, 0, 0);
	return label;
}

/* The device, the buffer size and the sample rate; all take effect from the
 * next song. */
void sndio_configure(void)
{
	GtkWidget *vbox, *table, *hint, *bbox, *ok, *cancel;
	GtkObject *adj;

	if (configure_win)
	{
		gdk_window_raise(configure_win->window);
		return;
	}

	configure_win = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_signal_connect(GTK_OBJECT(configure_win), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed), &configure_win);
	gtk_window_set_title(GTK_WINDOW(configure_win), _("sndio output configuration"));
	gtk_window_set_policy(GTK_WINDOW(configure_win), FALSE, FALSE, FALSE);
	gtk_window_set_position(GTK_WINDOW(configure_win), GTK_WIN_POS_MOUSE);
	gtk_container_set_border_width(GTK_CONTAINER(configure_win), 10);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(configure_win), vbox);

	table = gtk_table_new(2, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 5);
	gtk_table_set_col_spacings(GTK_TABLE(table), 5);
	gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);

	table_label(table, 0, _("Device:"));
	device_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(device_entry), sndio_cfg.device);
	gtk_table_attach_defaults(GTK_TABLE(table), device_entry, 1, 2, 0, 1);

	table_label(table, 1, _("Buffer size (ms):"));
	adj = gtk_adjustment_new(sndio_cfg.buffer_time, BUFFER_TIME_MIN,
				 BUFFER_TIME_MAX, 100, 1000, 0);
	buffer_spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0, 0);
	gtk_table_attach_defaults(GTK_TABLE(table), buffer_spin, 1, 2, 1, 2);

	hint = gtk_label_new(_("\"default\" plays through sndiod when it is running,\n"
			       "and otherwise straight to the first audio device.\n"
			       "AUDIODEVICE, if set, is used instead."));
	gtk_label_set_justify(GTK_LABEL(hint), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(hint), 0.0, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), hint, FALSE, FALSE, 0);

	rate_frame = xmms_rate_frame_new(sndio_cfg.output_rate, sndio_cfg.resample_quality,
					  sndio_device_rate);
	gtk_box_pack_start(GTK_BOX(vbox), rate_frame, FALSE, FALSE, 0);

	bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
	gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

	ok = gtk_button_new_with_label(_("OK"));
	gtk_signal_connect(GTK_OBJECT(ok), "clicked",
			   GTK_SIGNAL_FUNC(configure_ok), NULL);
	GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), ok, TRUE, TRUE, 0);

	cancel = gtk_button_new_with_label(_("Cancel"));
	gtk_signal_connect_object(GTK_OBJECT(cancel), "clicked",
				  GTK_SIGNAL_FUNC(gtk_widget_destroy),
				  GTK_OBJECT(configure_win));
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), cancel, TRUE, TRUE, 0);

	gtk_widget_grab_default(ok);
	gtk_widget_show_all(configure_win);
}
