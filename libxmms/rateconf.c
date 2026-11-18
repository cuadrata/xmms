/*  XMMS - Cross-platform multimedia player
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

#include "config.h"

#include "xmms/i18n.h"
#include "xmms/plugin.h"
#include "rateconf.h"

static const gint rates[] = { 0, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };

void xmms_rate_read(ConfigFile *cfg, const gchar *section, gint *rate, gint *quality)
{
	*rate = XMMS_RATE_DEFAULT;
	*quality = OUTPUT_QUALITY_BEST;
	if (cfg)
	{
		xmms_cfg_read_int(cfg, (gchar *) section, "output_rate", rate);
		xmms_cfg_read_int(cfg, (gchar *) section, "resample_quality", quality);
	}
	/* Anything nonsensical is the default. */
	if (*rate != 0 && (*rate < 8000 || *rate > 768000))
		*rate = XMMS_RATE_DEFAULT;
	if (*quality != OUTPUT_QUALITY_MEDIUM)
		*quality = OUTPUT_QUALITY_BEST;
}

void xmms_rate_write(ConfigFile *cfg, const gchar *section, gint rate, gint quality)
{
	xmms_cfg_write_int(cfg, (gchar *) section, "output_rate", rate);
	xmms_cfg_write_int(cfg, (gchar *) section, "resample_quality", quality);
}

static GtkWidget *option_menu(GtkWidget **items, gint n, gint active)
{
	GtkWidget *menu = gtk_menu_new(), *option = gtk_option_menu_new();
	gint i;

	for (i = 0; i < n; i++)
	{
		gtk_widget_show(items[i]);
		gtk_menu_append(GTK_MENU(menu), items[i]);
	}
	gtk_option_menu_set_menu(GTK_OPTION_MENU(option), menu);
	gtk_option_menu_set_history(GTK_OPTION_MENU(option), active);
	return option;
}

static GtkWidget *item(const gchar *label, gint value)
{
	GtkWidget *w = gtk_menu_item_new_with_label(label);

	gtk_object_set_data(GTK_OBJECT(w), "value", GINT_TO_POINTER(value));
	return w;
}

static gint chosen(GtkWidget *option)
{
	GtkWidget *menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(option));
	GtkWidget *active = gtk_menu_get_active(GTK_MENU(menu));

	return active ? GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(active), "value")) : 0;
}

/* The rate the device runs at, kept up to date while the frame is shown. */
static gint show_device_rate(gpointer data)
{
	GtkWidget *l = data;
	gint (*device_rate) (void) = gtk_object_get_data(GTK_OBJECT(l), "device_rate");
	gint rate = device_rate ? device_rate() : 0;
	gchar *text = rate > 0 ? g_strdup_printf(_("%d Hz"), rate) : g_strdup(_("Not open"));

	gtk_label_set_text(GTK_LABEL(l), text);
	g_free(text);
	return TRUE;
}

static void stop_showing(GtkWidget *w, gpointer data)
{
	gtk_timeout_remove(GPOINTER_TO_INT(data));
}

static GtkWidget *label(const gchar *text)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_misc_set_alignment(GTK_MISC(l), 0, 0.5);
	return l;
}

GtkWidget *xmms_rate_frame_new(gint rate, gint quality, gint (*device_rate) (void))
{
	GtkWidget *frame, *box, *table, *rate_menu, *quality_menu, *note, *now;
	gint timer;
	GtkWidget *items[sizeof (rates) / sizeof (rates[0]) + 1];
	gint i, n = 0, active = 0;
	gboolean listed = FALSE;

	for (i = 0; i < (gint) (sizeof (rates) / sizeof (rates[0])); i++)
	{
		gchar *text = rates[i] ? g_strdup_printf(_("%d Hz"), rates[i])
			: g_strdup(_("Each song's rate"));

		if (rates[i] == rate)
		{
			active = n;
			listed = TRUE;
		}
		items[n++] = item(text, rates[i]);
		g_free(text);
	}
	if (!listed)
	{
		/* A rate set in the config by hand stays as it is. */
		gchar *text = g_strdup_printf(_("%d Hz"), rate);

		active = n;
		items[n++] = item(text, rate);
		g_free(text);
	}
	rate_menu = option_menu(items, n, active);

	items[0] = item(_("Best"), OUTPUT_QUALITY_BEST);
	items[1] = item(_("Medium"), OUTPUT_QUALITY_MEDIUM);
	quality_menu = option_menu(items, 2, quality == OUTPUT_QUALITY_MEDIUM ? 1 : 0);

	frame = gtk_frame_new(_("Sample rate:"));
	box = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(box), 5);
	gtk_container_add(GTK_CONTAINER(frame), box);
	table = gtk_table_new(3, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 5);
	gtk_table_set_col_spacings(GTK_TABLE(table), 5);
	gtk_box_pack_start(GTK_BOX(box), table, FALSE, FALSE, 0);
	gtk_table_attach(GTK_TABLE(table), label(_("Ask for:")), 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
	gtk_table_attach(GTK_TABLE(table), rate_menu, 1, 2, 0, 1, GTK_FILL, 0, 0, 0);
	gtk_table_attach(GTK_TABLE(table), label(_("Conversion:")), 0, 1, 1, 2, GTK_FILL, 0, 0, 0);
	gtk_table_attach(GTK_TABLE(table), quality_menu, 1, 2, 1, 2, GTK_FILL, 0, 0, 0);
	gtk_table_attach(GTK_TABLE(table), label(_("Device runs at:")), 0, 1, 2, 3, GTK_FILL, 0, 0, 0);
	now = label("");
	gtk_object_set_data(GTK_OBJECT(now), "device_rate", (gpointer) device_rate);
	gtk_table_attach(GTK_TABLE(table), now, 1, 2, 2, 3, GTK_FILL, 0, 0, 0);
	show_device_rate(now);
	timer = gtk_timeout_add(500, show_device_rate, now);
	gtk_signal_connect(GTK_OBJECT(now), "destroy", GTK_SIGNAL_FUNC(stop_showing),
			   GINT_TO_POINTER(timer));
	note = gtk_label_new(_("If the device can't run at the rate asked for, it runs "
			       "at the nearest one it can; a device shared with other "
			       "programs often has a fixed rate. Songs at any other rate "
			       "are converted to the rate the device runs at."));
	gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
	gtk_label_set_justify(GTK_LABEL(note), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC(note), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(box), note, FALSE, FALSE, 0);

	gtk_object_set_data(GTK_OBJECT(frame), "rate", rate_menu);
	gtk_object_set_data(GTK_OBJECT(frame), "quality", quality_menu);
	return frame;
}

void xmms_rate_frame_get(GtkWidget *frame, gint *rate, gint *quality)
{
	*rate = chosen(gtk_object_get_data(GTK_OBJECT(frame), "rate"));
	*quality = chosen(gtk_object_get_data(GTK_OBJECT(frame), "quality"));
}
