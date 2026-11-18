/*  XMMS - ALSA output plugin
 *  Copyright (C) 2001-2003 Matthieu Sozeau <mattam@altern.org>
 *  Copyright (C) 2003-2005 Haavard Kvaalen
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
#include "alsa.h"
#include <errno.h>
#include <stdio.h>

static GtkWidget *configure_win = NULL;
static GtkWidget *buffer_time_spin, *period_time_spin, *thread_buffer_time_spin;
static GtkWidget *devices_combo, *rate_frame;

#define GET_SPIN_INT(spin) \
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin))
#define GET_CHARS(edit) gtk_editable_get_chars(GTK_EDITABLE(edit), 0, -1)

static void configure_win_ok_cb(GtkWidget * w, gpointer data)
{
	g_free(alsa_cfg.pcm_device);
	alsa_cfg.pcm_device = GET_CHARS(GTK_COMBO(devices_combo)->entry);
	alsa_cfg.buffer_time = GET_SPIN_INT(buffer_time_spin);
	alsa_cfg.period_time = MIN(GET_SPIN_INT(period_time_spin), alsa_cfg.buffer_time / 2);
	alsa_cfg.thread_buffer_time = GET_SPIN_INT(thread_buffer_time_spin);
	xmms_rate_frame_get(rate_frame, &alsa_cfg.output_rate, &alsa_cfg.resample_quality);

	alsa_save_config();
	gtk_widget_destroy(configure_win);
}

void alsa_save_config(void)
{
	ConfigFile *cfgfile = xmms_cfg_open_default_file();

	xmms_cfg_write_string(cfgfile, "ALSA", "pcm_device", alsa_cfg.pcm_device);
	xmms_cfg_write_int(cfgfile, "ALSA", "buffer_time", alsa_cfg.buffer_time);
	xmms_cfg_write_int(cfgfile, "ALSA", "period_time", alsa_cfg.period_time);
	xmms_cfg_write_int(cfgfile, "ALSA", "thread_buffer_time", alsa_cfg.thread_buffer_time);
	xmms_rate_write(cfgfile, "ALSA", alsa_cfg.output_rate, alsa_cfg.resample_quality);
	xmms_cfg_write_default_file(cfgfile);
	xmms_cfg_free(cfgfile);
}

static void get_devices_for_card(GtkCombo *combo, int card)
{
	GtkWidget *item;
	int pcm_device = -1, err;
	snd_pcm_info_t *pcm_info;
	snd_ctl_t *ctl;
	char dev[64], *card_name;

	g_snprintf(dev, sizeof (dev), "hw:%i", card);

	if ((err = snd_ctl_open(&ctl, dev, 0)) < 0)
	{
		printf("snd_ctl_open() failed: %s", snd_strerror(-err));
		return;
	}

	if ((err = snd_card_get_name(card, &card_name)) != 0)
	{
		g_warning("snd_card_get_name() failed: %s", snd_strerror(-err));
		card_name = _("Unknown soundcard");
	}

	snd_pcm_info_alloca(&pcm_info);

	for (;;)
	{
		char *device, *descr;
		if ((err = snd_ctl_pcm_next_device(ctl, &pcm_device)) < 0)
		{
			g_warning("snd_ctl_pcm_next_device() failed: %s",
				  snd_strerror(-err));
			pcm_device = -1;
		}
		if (pcm_device < 0)
			break;

		snd_pcm_info_set_device(pcm_info, pcm_device);
		snd_pcm_info_set_subdevice(pcm_info, 0);
		snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_PLAYBACK);

		if ((err = snd_ctl_pcm_info(ctl, pcm_info)) < 0)
		{
			if (err != -ENOENT)
				g_warning("get_devices_for_card(): "
					  "snd_ctl_pcm_info() "
					  "failed (%d:%d): %s.", card,
					  pcm_device, snd_strerror(-err));
			continue;
		}

		device = g_strdup_printf("hw:%d,%d", card, pcm_device);
		descr = g_strconcat(card_name, ": ",
				    snd_pcm_info_get_name(pcm_info),
				    " (", device, ")", NULL);
		item = gtk_list_item_new_with_label(descr);
		gtk_widget_show(item);
		g_free(descr);
		gtk_combo_set_item_string(combo, GTK_ITEM(item), device);
		g_free(device);
		gtk_container_add(GTK_CONTAINER(combo->list), item);
	}

	snd_ctl_close(ctl);
}



static void get_devices(GtkCombo *combo)
{
	GtkWidget *item;
	int card = -1;
	int err = 0;
	char *descr;

	descr = g_strdup_printf(_("Default PCM device (%s)"), "default");
	item = gtk_list_item_new_with_label(descr);
	gtk_widget_show(item);
	g_free(descr);
	gtk_combo_set_item_string(combo, GTK_ITEM(item), "default");
	gtk_container_add(GTK_CONTAINER(combo->list), item);

	if ((err = snd_card_next(&card)) != 0)
	{
		g_warning("snd_next_card() failed: %s", snd_strerror(-err));
		return;
	}

	while (card > -1)
	{
		get_devices_for_card(combo, card);
		if ((err = snd_card_next(&card)) != 0)
		{
			g_warning("snd_next_card() failed: %s",
				  snd_strerror(-err));
			break;
		}
	}
}

/* A spin button, labelled, in a row of a table. */
static GtkWidget *add_spin(GtkWidget *table, gint row, const gchar *label,
			   gint value, gint min, gint max)
{
	GtkWidget *l, *spin;
	GtkObject *adj;

	l = gtk_label_new(label);
	gtk_misc_set_alignment(GTK_MISC(l), 0, 0.5);
	gtk_table_attach(GTK_TABLE(table), l, 0, 1, row, row + 1, GTK_FILL, 0, 0, 0);
	adj = gtk_adjustment_new(value, min, max, 10, 100, 0);
	spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 8, 0);
	gtk_widget_set_usize(spin, 60, -1);
	gtk_table_attach(GTK_TABLE(table), spin, 1, 2, row, row + 1, 0, 0, 0, 0);
	return spin;
}

void alsa_configure(void)
{
	GtkWidget *vbox, *notebook, *page, *frame, *box, *table, *note;
	GtkWidget *bbox, *ok, *cancel;

	if (configure_win)
	{
		gdk_window_raise(configure_win->window);
		return;
	}

	configure_win = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_signal_connect(GTK_OBJECT(configure_win), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed), &configure_win);
	gtk_window_set_title(GTK_WINDOW(configure_win), _("ALSA Driver configuration"));
	gtk_window_set_policy(GTK_WINDOW(configure_win), FALSE, TRUE, FALSE);
	gtk_container_border_width(GTK_CONTAINER(configure_win), 10);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(configure_win), vbox);
	notebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

	/* The device */
	page = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(page), 5);
	frame = gtk_frame_new(_("Audio device:"));
	gtk_box_pack_start(GTK_BOX(page), frame, FALSE, FALSE, 0);
	box = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(box), 5);
	gtk_container_add(GTK_CONTAINER(frame), box);
	devices_combo = gtk_combo_new();
	gtk_box_pack_start(GTK_BOX(box), devices_combo, FALSE, FALSE, 0);
	get_devices(GTK_COMBO(devices_combo));
	gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(devices_combo)->entry), alsa_cfg.pcm_device);
	rate_frame = xmms_rate_frame_new(alsa_cfg.output_rate, alsa_cfg.resample_quality,
					  alsa_device_rate);
	gtk_box_pack_start(GTK_BOX(page), rate_frame, FALSE, FALSE, 0);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, gtk_label_new(_("Device settings")));

	/* Its buffering: XMMS's own is in its preferences. */
	page = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(page), 5);
	frame = gtk_frame_new(_("Soundcard:"));
	gtk_box_pack_start(GTK_BOX(page), frame, FALSE, FALSE, 0);
	table = gtk_table_new(2, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), 5);
	gtk_table_set_col_spacings(GTK_TABLE(table), 5);
	gtk_container_add(GTK_CONTAINER(frame), table);
	buffer_time_spin = add_spin(table, 0, _("Buffer time (ms):"), alsa_cfg.buffer_time, 20, 10000);
	period_time_spin = add_spin(table, 1, _("Period time (ms):"), alsa_cfg.period_time, 1, 500);
	frame = gtk_frame_new(_("XMMS:"));
	gtk_box_pack_start(GTK_BOX(page), frame, FALSE, FALSE, 0);
	table = gtk_table_new(1, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 5);
	gtk_table_set_row_spacings(GTK_TABLE(table), 5);
	gtk_table_set_col_spacings(GTK_TABLE(table), 5);
	gtk_container_add(GTK_CONTAINER(frame), table);
	thread_buffer_time_spin = add_spin(table, 0, _("Buffer time (ms):"),
					   alsa_cfg.thread_buffer_time, 1000, 10000);
	note = gtk_label_new(_("Changes take effect from the next song."));
	gtk_misc_set_alignment(GTK_MISC(note), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(page), note, FALSE, FALSE, 0);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, gtk_label_new(_("Advanced settings")));

	bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
	gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

	ok = gtk_button_new_with_label(_("OK"));
	gtk_signal_connect(GTK_OBJECT(ok), "clicked", GTK_SIGNAL_FUNC(configure_win_ok_cb), NULL);
	GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), ok, TRUE, TRUE, 0);
	gtk_widget_grab_default(ok);

	cancel = gtk_button_new_with_label(_("Cancel"));
	gtk_signal_connect_object(GTK_OBJECT(cancel), "clicked",
				  GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(configure_win));
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), cancel, TRUE, TRUE, 0);

	gtk_widget_show_all(configure_win);
}
