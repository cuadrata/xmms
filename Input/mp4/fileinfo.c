/*  XMMS - MP4 input plugin
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "xmms/i18n.h"
#include "libxmms/charset.h"
#include "libxmms/xentry.h"

#include <neaacdec.h>

#include "mp4.h"
#include "alac.h"
#include "httpstream.h"

/* The tags shown, in order. */
static const char *labels[] =
{
	N_("Title:"), N_("Artist:"), N_("Album artist:"), N_("Album:"),
	N_("Comment:"), N_("Date:"), N_("Track number:"), N_("Genre:")
};

static GtkWidget *window, *filename_entry, *tag_frame, *info_label;
static GtkWidget *entries[ARRAY_LEN(labels)];

static void keypress_cb(GtkWidget *w, GdkEventKey *event, gpointer data)
{
	(void) data;
	if (event && event->keyval == GDK_Escape)
		gtk_widget_destroy(w);
}

static void set_entry(int i, const char *utf8)
{
	gchar *local = utf8 ? xmms_charset_from_utf8(utf8) : NULL;

	gtk_entry_set_text(GTK_ENTRY(entries[i]), local ? local : "");
	g_free(local);
}

static const char *aac_profile(const mp4AudioSpecificConfig *asc)
{
	if (asc->objectTypeIndex == 29)
		return "HE-AAC v2";
	if (asc->objectTypeIndex == 5 || asc->sbr_present_flag == 1)
		return "HE-AAC";
	switch (asc->objectTypeIndex)
	{
	case 1: return "AAC Main";
	case 2: return "AAC LC";
	case 3: return "AAC SSR";
	case 4: return "AAC LTP";
	case 23: return "AAC LD";
	default: return "AAC";
	}
}

static void append_common(GString *s, guint64 length_ms, long size)
{
	if (length_ms)
		g_string_sprintfa(s, _("Average bitrate: %d kb/s\n"),
				  (int) (size * 8 / (long) length_ms));
	g_string_sprintfa(s, _("Length: %d:%02d\n"),
			  (int) (length_ms / 60000), (int) (length_ms / 1000 % 60));
	g_string_sprintfa(s, _("File size: %ld B\n"), size);
}

static void show_mp4(Mp4File *m, GString *s, long size)
{
	const Mp4Tags *t = &m->tags;
	gchar *track = t->track ? g_strdup_printf("%d", t->track) : NULL;

	set_entry(0, t->title);
	set_entry(1, t->artist);
	set_entry(2, t->album_artist);
	set_entry(3, t->album);
	set_entry(4, t->comment);
	set_entry(5, t->date);
	set_entry(6, track);
	set_entry(7, t->genre);
	g_free(track);

	if (m->codec == MP4_CODEC_ALAC)
	{
		Alac *a = alac_new(m->config, m->config_len);

		if (a)
		{
			g_string_sprintfa(s, _("Apple Lossless, %u-bit\n"), alac_bits(a));
			g_string_sprintfa(s, _("Channels: %u\n"), alac_channels(a));
			g_string_sprintfa(s, _("Sample rate: %u Hz\n"), alac_rate(a));
			alac_free(a);
		}
	}
	else
	{
		mp4AudioSpecificConfig asc;

		if (NeAACDecAudioSpecificConfig(m->config, m->config_len, &asc) >= 0)
		{
			g_string_sprintfa(s, "%s\n", aac_profile(&asc));
			g_string_sprintfa(s, _("Channels: %u\n"), asc.channelsConfiguration);
			g_string_sprintfa(s, _("Sample rate: %lu Hz\n"),
					  asc.samplingFrequency);
		}
	}
	append_common(s, mp4_length_ms(m), size);
	if (m->priming || m->valid)
		g_string_sprintfa(s, _("Gapless: yes\n"));
	if (t->encoder)
		g_string_sprintfa(s, _("Encoder: %s"), t->encoder);
}

static void show_adts(FILE *fp, GString *s, long size)
{
	guint8 h[7], t[10];
	guint rate, ch, len, frames = 0;
	long pos = 0;


	/* Past an ID3v2 tag, if any. */
	if (fread(t, 1, 10, fp) == 10 && !memcmp(t, "ID3", 3))
		pos = 10 + ((long) (t[6] & 0x7f) << 21 | (long) (t[7] & 0x7f) << 14 |
			    (long) (t[8] & 0x7f) << 7 | (t[9] & 0x7f));
	while (fseek(fp, pos, SEEK_SET) == 0 && fread(h, 1, 7, fp) == 7 &&
	       mp4_adts_header(h, &rate, &ch, &len))
	{
		frames++;
		pos += len;
	}
	if (!frames)
	{
		g_string_append(s, _("Not an AAC file"));
		return;
	}
	fseek(fp, 0, SEEK_SET);
	g_string_sprintfa(s, _("AAC (ADTS)\n"));
	g_string_sprintfa(s, _("Channels: %u\n"), ch);
	g_string_sprintfa(s, _("Sample rate: %u Hz\n"), rate);
	append_common(s, (guint64) (frames - 1) * 1024 * 1000 / rate, size);
}

static void show_file(const char *filename)
{
	Mp4File *m;
	FILE *fp;
	GString *info;
	long size = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_LEN(labels); i++)
		gtk_entry_set_text(GTK_ENTRY(entries[i]), "");

	if (http_stream_is_url(filename))
	{
		gtk_widget_set_sensitive(tag_frame, FALSE);
		gtk_label_set_text(GTK_LABEL(info_label), _("Stream"));
		return;
	}
	gtk_widget_set_sensitive(tag_frame, TRUE);

	if ((fp = fopen(filename, "rb")) != NULL)
	{
		fseek(fp, 0, SEEK_END);
		size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
	}

	info = g_string_new("");
	if ((m = mp4_open(filename)) != NULL)
	{
		show_mp4(m, info, size);
		mp4_close(m);
	}
	else if (fp)
	{
		gtk_widget_set_sensitive(tag_frame, FALSE);
		show_adts(fp, info, size);
	}
	if (fp)
		fclose(fp);
	gtk_label_set_text(GTK_LABEL(info_label), info->str);
	g_string_free(info, TRUE);
}

static void create_window(void)
{
	GtkWidget *vbox, *hbox, *left, *table, *label, *info_frame, *info_box;
	GtkWidget *bbox, *close;
	unsigned int i;

	window = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, FALSE);
	gtk_signal_connect(GTK_OBJECT(window), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed), &window);
	gtk_signal_connect(GTK_OBJECT(window), "key_press_event",
			   GTK_SIGNAL_FUNC(keypress_cb), NULL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);
	label = gtk_label_new(_("Filename:"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);
	filename_entry = xmms_entry_new();
	gtk_editable_set_editable(GTK_EDITABLE(filename_entry), FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), filename_entry, TRUE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 10);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	left = gtk_vbox_new(FALSE, 10);
	gtk_box_pack_start(GTK_BOX(hbox), left, TRUE, TRUE, 0);

	tag_frame = gtk_frame_new(_("MP4 Tags:"));
	gtk_box_pack_start(GTK_BOX(left), tag_frame, FALSE, FALSE, 0);

	table = gtk_table_new(ARRAY_LEN(labels), 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_container_add(GTK_CONTAINER(tag_frame), table);

	for (i = 0; i < ARRAY_LEN(labels); i++)
	{
		label = gtk_label_new(_(labels[i]));
		gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
		gtk_table_attach(GTK_TABLE(table), label, 0, 1, i, i + 1,
				 GTK_FILL, GTK_FILL, 5, 5);

		entries[i] = xmms_entry_new();
		gtk_editable_set_editable(GTK_EDITABLE(entries[i]), FALSE);
		gtk_widget_set_usize(entries[i], 250, -1);
		gtk_table_attach(GTK_TABLE(table), entries[i], 1, 2, i, i + 1,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 5);
	}

	bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
	gtk_box_pack_start(GTK_BOX(left), bbox, FALSE, FALSE, 0);

	close = gtk_button_new_with_label(_("Close"));
	gtk_signal_connect_object(GTK_OBJECT(close), "clicked",
				  GTK_SIGNAL_FUNC(gtk_widget_destroy),
				  GTK_OBJECT(window));
	GTK_WIDGET_SET_FLAGS(close, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), close, TRUE, TRUE, 0);
	gtk_widget_grab_default(close);

	info_frame = gtk_frame_new(_("Audio Info:"));
	gtk_box_pack_start(GTK_BOX(hbox), info_frame, FALSE, FALSE, 0);

	info_box = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(info_box), 10);
	gtk_container_add(GTK_CONTAINER(info_frame), info_box);

	info_label = gtk_label_new("");
	gtk_misc_set_alignment(GTK_MISC(info_label), 0, 0);
	gtk_label_set_justify(GTK_LABEL(info_label), GTK_JUSTIFY_LEFT);
	gtk_box_pack_start(GTK_BOX(info_box), info_label, FALSE, FALSE, 0);

	gtk_widget_show_all(window);
}

void mp4_file_info_box(char *filename)
{
	gchar *title;

	if (!window)
		create_window();
	else
		gdk_window_raise(window->window);

	title = g_strdup_printf(_("File Info - %s"), g_basename(filename));
	gtk_window_set_title(GTK_WINDOW(window), title);
	g_free(title);

	gtk_entry_set_text(GTK_ENTRY(filename_entry), filename);
	gtk_editable_set_position(GTK_EDITABLE(filename_entry), -1);

	show_file(filename);
}
