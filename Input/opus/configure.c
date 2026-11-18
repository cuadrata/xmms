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

#include <glib.h>
#include <gtk/gtk.h>

#include "xmms/i18n.h"
#include "libxmms/configfile.h"
#include "libxmms/titlestring.h"

#include "opus.h"
#include "httpstream.h"

static GtkWidget *configure_win;
static GtkWidget *tag_override, *tag_box, *tag_entry, *tag_desc;
static GtkWidget *gain_header, *gain_track, *gain_album, *gain_rg_level;

static void configure_ok(GtkWidget *w, gpointer data)
{
	ConfigFile *cfg;

	(void) w;
	(void) data;

	http_stream_config_page_apply();

	opus_cfg.tag_override =
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tag_override));
	g_free(opus_cfg.tag_format);
	opus_cfg.tag_format = g_strdup(gtk_entry_get_text(GTK_ENTRY(tag_entry)));

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gain_track)))
		opus_cfg.gain_mode = OPUS_GAIN_TRACK;
	else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gain_album)))
		opus_cfg.gain_mode = OPUS_GAIN_ALBUM;
	else
		opus_cfg.gain_mode = OPUS_GAIN_HEADER;
	opus_cfg.gain_rg_level =
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gain_rg_level));

	cfg = xmms_cfg_open_default_file();
	xmms_cfg_write_boolean(cfg, "opus", "tag_override", opus_cfg.tag_override);
	xmms_cfg_write_string(cfg, "opus", "tag_format", opus_cfg.tag_format);
	xmms_cfg_write_int(cfg, "opus", "gain_mode", opus_cfg.gain_mode);
	xmms_cfg_write_boolean(cfg, "opus", "gain_rg_level", opus_cfg.gain_rg_level);
	xmms_cfg_write_default_file(cfg);
	xmms_cfg_free(cfg);

	gtk_widget_destroy(configure_win);
}

static void tag_override_cb(GtkWidget *w, gpointer data)
{
	gboolean on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));

	(void) data;
	gtk_widget_set_sensitive(tag_box, on);
	gtk_widget_set_sensitive(tag_desc, on);
}

/* The ReplayGain level only means anything with R128 gain in use. */
static void gain_mode_cb(GtkWidget *w, gpointer data)
{
	(void) w;
	(void) data;
	gtk_widget_set_sensitive(gain_rg_level,
		!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gain_header)));
}

static GtkWidget *title_page(void)
{
	GtkWidget *frame, *vbox, *label;

	frame = gtk_frame_new(_("Opus Tags:"));
	gtk_container_set_border_width(GTK_CONTAINER(frame), 5);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_container_add(GTK_CONTAINER(frame), vbox);

	tag_override = gtk_check_button_new_with_label(_("Override generic titles"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tag_override),
				     opus_cfg.tag_override);
	gtk_signal_connect(GTK_OBJECT(tag_override), "toggled",
			   GTK_SIGNAL_FUNC(tag_override_cb), NULL);
	gtk_box_pack_start(GTK_BOX(vbox), tag_override, FALSE, FALSE, 0);

	tag_box = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), tag_box, FALSE, FALSE, 0);

	label = gtk_label_new(_("Title format:"));
	gtk_box_pack_start(GTK_BOX(tag_box), label, FALSE, FALSE, 0);

	tag_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(tag_entry), opus_cfg.tag_format);
	gtk_box_pack_start(GTK_BOX(tag_box), tag_entry, TRUE, TRUE, 0);

	tag_desc = xmms_titlestring_descriptions("pafFetndgc", 2);
	gtk_box_pack_start(GTK_BOX(vbox), tag_desc, FALSE, FALSE, 0);

	gtk_widget_set_sensitive(tag_box, opus_cfg.tag_override);
	gtk_widget_set_sensitive(tag_desc, opus_cfg.tag_override);

	return frame;
}

static GtkWidget *gain_page(void)
{
	GtkWidget *frame, *vbox, *label;
	GSList *group;

	frame = gtk_frame_new(_("Gain:"));
	gtk_container_set_border_width(GTK_CONTAINER(frame), 5);

	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_container_add(GTK_CONTAINER(frame), vbox);

	label = gtk_label_new(_("The output gain in the file's header is always applied."));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	gain_header = gtk_radio_button_new_with_label(NULL, _("No normalization"));
	gtk_box_pack_start(GTK_BOX(vbox), gain_header, FALSE, FALSE, 0);
	group = gtk_radio_button_group(GTK_RADIO_BUTTON(gain_header));

	gain_track = gtk_radio_button_new_with_label(group, _("Use R128 track gain"));
	gtk_box_pack_start(GTK_BOX(vbox), gain_track, FALSE, FALSE, 0);
	group = gtk_radio_button_group(GTK_RADIO_BUTTON(gain_track));

	gain_album = gtk_radio_button_new_with_label(group, _("Use R128 album gain"));
	gtk_box_pack_start(GTK_BOX(vbox), gain_album, FALSE, FALSE, 0);

	gain_rg_level = gtk_check_button_new_with_label(
		_("Raise by 5 dB to ReplayGain level"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gain_rg_level),
				     opus_cfg.gain_rg_level);
	gtk_box_pack_start(GTK_BOX(vbox), gain_rg_level, FALSE, FALSE, 5);

	if (opus_cfg.gain_mode == OPUS_GAIN_TRACK)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gain_track), TRUE);
	else if (opus_cfg.gain_mode == OPUS_GAIN_ALBUM)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gain_album), TRUE);
	else
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gain_header), TRUE);
	gain_mode_cb(NULL, NULL);
	gtk_signal_connect(GTK_OBJECT(gain_header), "toggled",
			   GTK_SIGNAL_FUNC(gain_mode_cb), NULL);

	return frame;
}

void opus_configure(void)
{
	GtkWidget *vbox, *notebook, *bbox, *ok, *cancel;

	if (configure_win)
	{
		gdk_window_raise(configure_win->window);
		return;
	}

	configure_win = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_signal_connect(GTK_OBJECT(configure_win), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed), &configure_win);
	gtk_window_set_title(GTK_WINDOW(configure_win), _("Opus Configuration"));
	gtk_window_set_policy(GTK_WINDOW(configure_win), FALSE, FALSE, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(configure_win), 10);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(configure_win), vbox);

	notebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), http_stream_config_page(),
				 gtk_label_new(_("Streaming")));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), title_page(),
				 gtk_label_new(_("Title")));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), gain_page(),
				 gtk_label_new(_("Gain")));

	bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
	gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

	ok = gtk_button_new_with_label(_("OK"));
	gtk_signal_connect(GTK_OBJECT(ok), "clicked",
			   GTK_SIGNAL_FUNC(configure_ok), NULL);
	GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), ok, TRUE, TRUE, 0);
	gtk_widget_grab_default(ok);

	cancel = gtk_button_new_with_label(_("Cancel"));
	gtk_signal_connect_object(GTK_OBJECT(cancel), "clicked",
				  GTK_SIGNAL_FUNC(gtk_widget_destroy),
				  GTK_OBJECT(configure_win));
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), cancel, TRUE, TRUE, 0);

	gtk_widget_show_all(configure_win);
}
