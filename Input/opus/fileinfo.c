/*  XMMS - Opus input plugin
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
#include <string.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "xmms/i18n.h"
#include "libxmms/charset.h"
#include "libxmms/xentry.h"

#include "opus.h"
#include "httpstream.h"

/* The tags shown, in order, with the labels they are shown under. */
static const struct
{
	const char *tag;
	const char *label;
} fields[] =
{
	{ "TITLE", N_("Title:") },
	{ "ARTIST", N_("Artist:") },
	{ "ALBUM", N_("Album:") },
	{ "COMMENT", N_("Comment:") },
	{ "DATE", N_("Date:") },
	{ "TRACKNUMBER", N_("Track number:") },
	{ "GENRE", N_("Genre:") },
};

static GtkWidget *window, *filename_entry, *tag_frame, *info_label;
static GtkWidget *entries[ARRAY_LEN(fields)];

static void keypress_cb(GtkWidget *w, GdkEventKey *event, gpointer data)
{
	(void) data;
	if (event && event->keyval == GDK_Escape)
		gtk_widget_destroy(w);
}

/* Every value of a tag, joined, in the user's character set. */
static gchar *tag_values(const OpusTags *tags, const char *name)
{
	GString *s = g_string_new("");
	gchar *utf8, *local;
	int i, n = opus_tags_query_count(tags, name);

	for (i = 0; i < n; i++)
	{
		if (i > 0)
			g_string_append(s, ", ");
		g_string_append(s, opus_tags_query(tags, name, i));
	}
	utf8 = s->str;
	g_string_free(s, FALSE);
	local = xmms_charset_from_utf8(utf8);
	g_free(utf8);
	return local;
}

static void append_gain(GString *s, const char *label, int q8)
{
	g_string_sprintfa(s, "%s %+.2f dB\n", label, q8 / 256.0);
}

static void show_file(const char *filename)
{
	OggOpusFile *of;
	const OpusHead *head;
	const OpusTags *tags;
	GString *info;
	ogg_int64_t total;
	opus_int64 size;
	unsigned int i;
	int gain;

	for (i = 0; i < ARRAY_LEN(fields); i++)
		gtk_entry_set_text(GTK_ENTRY(entries[i]), "");

	if (http_stream_is_url(filename))
	{
		gtk_widget_set_sensitive(tag_frame, FALSE);
		gtk_label_set_text(GTK_LABEL(info_label), _("Stream"));
		return;
	}
	gtk_widget_set_sensitive(tag_frame, TRUE);

	if ((of = op_open_file(filename, NULL)) == NULL)
	{
		gtk_label_set_text(GTK_LABEL(info_label), _("Not an Opus file"));
		return;
	}
	head = op_head(of, 0);
	tags = op_tags(of, 0);

	for (i = 0; i < ARRAY_LEN(fields); i++)
	{
		gchar *v = tag_values(tags, fields[i].tag);

		if (!v || !*v)
		{
			g_free(v);
			if (!strcmp(fields[i].tag, "COMMENT"))
				v = tag_values(tags, "DESCRIPTION");
			else
				continue;
		}
		gtk_entry_set_text(GTK_ENTRY(entries[i]), v ? v : "");
		g_free(v);
	}

	info = g_string_new("");
	g_string_sprintfa(info, _("Channels: %d\n"), head->channel_count);
	if (head->input_sample_rate)
		g_string_sprintfa(info, _("Original rate: %u Hz\n"),
				  (unsigned) head->input_sample_rate);
	g_string_sprintfa(info, _("Average bitrate: %d kb/s\n"),
			  (int) ((op_bitrate(of, -1) + 500) / 1000));
	if ((total = op_pcm_total(of, -1)) >= 0)
		g_string_sprintfa(info, _("Length: %d:%02d\n"),
				  (int) (total / 48000 / 60),
				  (int) (total / 48000 % 60));
	if ((size = op_raw_total(of, -1)) >= 0)
		g_string_sprintfa(info, _("File size: %ld B\n"), (long) size);
	if (op_link_count(of) > 1)
		g_string_sprintfa(info, _("Chained links: %d\n"),
				  op_link_count(of));
	append_gain(info, _("Output gain:"), head->output_gain);
	if (opus_tags_get_track_gain(tags, &gain) == 0)
		append_gain(info, _("Track gain:"), gain);
	if (opus_tags_get_album_gain(tags, &gain) == 0)
		append_gain(info, _("Album gain:"), gain);
	if (tags->vendor)
		g_string_sprintfa(info, _("Encoder: %s"), tags->vendor);

	gtk_label_set_text(GTK_LABEL(info_label), info->str);
	g_string_free(info, TRUE);
	op_free(of);
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

	tag_frame = gtk_frame_new(_("Opus Tags:"));
	gtk_box_pack_start(GTK_BOX(left), tag_frame, FALSE, FALSE, 0);

	table = gtk_table_new(ARRAY_LEN(fields), 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_container_add(GTK_CONTAINER(tag_frame), table);

	for (i = 0; i < ARRAY_LEN(fields); i++)
	{
		label = gtk_label_new(_(fields[i].label));
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

	info_frame = gtk_frame_new(_("Opus Info:"));
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

void opus_file_info_box(char *filename)
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
