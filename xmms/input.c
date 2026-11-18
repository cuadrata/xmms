/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2003  Peter Alm, Mikael Alm, Olle Hallnas,
 *                           Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 1999-2003  Haavard Kvaalen
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
#include "xmms.h"
#include "fft.h"
#include "libxmms/titlestring.h"
#include "libxmms/util.h"
#include "libxmms/xentry.h"

static pthread_mutex_t vis_mutex = PTHREAD_MUTEX_INITIALIZER;

struct vis_node
{
	int time;
	int nch;
	int length; /* number of samples per channel */
	gint16 data[2][512];
};


struct InputPluginData *ip_data;
static GList *vis_list = NULL;

gchar *input_info_text = NULL;
extern PlayStatus *mainwin_playstatus;


/*
 * A plugin's play_file(), stop(), seek() and pause() can wait on the
 * song's file: finding the plugin reads it, and stop() joins a decoder
 * that may be stuck reading a file that no longer answers. So only the
 * playback thread calls them, one at a time and in order; the main
 * thread says what it wants (want_*) and never waits. get_time() never
 * waits, and is asked only when no call is under way.
 */
#define STALL_SECS	3	/* a call this long is said on the terminal */

static pthread_mutex_t ctl_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ctl_cond = PTHREAD_COND_INITIALIZER;
static pthread_t ctl_thread;
static gboolean ctl_up, ctl_quit;

static guint want_serial;	/* a new one for each play and stop */
static gchar *want_file;	/* NULL: stopped */
static gboolean want_paused;
static gint want_seek = -1;

static gboolean cur_open;	/* started, by a plugin or by none */
static guint cur_serial;
static gchar *cur_file;
static InputPlugin *cur_ip;
static gboolean cur_paused;
static gint last_time;		/* shown while a call is under way */

static const gchar *busy;	/* the call under way */
static GTimer *busy_timer;
static gboolean busy_told;

static InputPlugin *get_current_input_plugin(void)
{
	return cur_ip;
}

GList *get_input_list(void)
{
	return ip_data->input_list;
}

static void free_vis_data(void)
{
	GList *node;
	pthread_mutex_lock(&vis_mutex);
	node = vis_list;
	while (node)
	{
		g_free(node->data);
		node = g_list_next(node);
	}
	g_list_free(vis_list);
	vis_list = NULL;
	pthread_mutex_unlock(&vis_mutex);
}

static void convert_to_s16_ne(AFormat fmt, gpointer ptr, gint16 *left, gint16 *right,
			      int nch, int max)
{
	gint16 *ptr16;
	guint16 *ptru16;
	guint8 *ptru8;
	int i;
	
	switch (fmt)
	{
		case FMT_U8:
			ptru8 = ptr;
			if (nch == 1)
				for (i = 0; i < max; i++)
					left[i] = ((*ptru8++) ^ 128) << 8;
			else
				for (i = 0; i < max; i++)
				{
					left[i] = ((*ptru8++) ^ 128) << 8;
					right[i] = ((*ptru8++) ^ 128) << 8;
				}
			break;
		case FMT_S8:
			ptru8 = ptr;
			if (nch == 1)
				for (i = 0; i < max; i++)
					left[i] = (*ptru8++) << 8;
			else
				for (i = 0; i < max; i++)
				{
					left[i] = (*ptru8++) << 8;
					right[i] = (*ptru8++) << 8;
				}
			break;
		case FMT_U16_LE:
			ptru16 = ptr;
			if (nch == 1)
				for (i = 0; i < max; i++, ptru16++)
					left[i] = GUINT16_FROM_LE(*ptru16) ^ 32768;
			else
				for (i = 0; i < max; i++)
				{
					left[i] = GUINT16_FROM_LE(*ptru16) ^ 32768;
					ptru16++;
					right[i] = GUINT16_FROM_LE(*ptru16) ^ 32768;
					ptru16++;
				}
			break;
		case FMT_U16_BE:
			ptru16 = ptr;
			if (nch == 1)
				for (i = 0; i < max; i++, ptru16++)
					left[i] = GUINT16_FROM_BE(*ptru16) ^ 32768;
			else
				for (i = 0; i < max; i++)
				{
					left[i] = GUINT16_FROM_BE(*ptru16) ^ 32768;
					ptru16++;
					right[i] = GUINT16_FROM_BE(*ptru16) ^ 32768;
					ptru16++;
				}
			break;
		case FMT_U16_NE:
			ptru16 = ptr;
			if (nch == 1)
				for (i = 0; i < max; i++)
					left[i] = (*ptru16++) ^ 32768;
			else
				for (i = 0; i < max; i++)
				{
					left[i] = (*ptru16++) ^ 32768;
					right[i] = (*ptru16++) ^ 32768;
				}
			break;
		case FMT_S16_LE:
			ptr16 = ptr;
			if (nch == 1)
				for (i = 0; i < max; i++, ptr16++)
					left[i] = GINT16_FROM_LE(*ptr16);
			else
				for (i = 0; i < max; i++)
				{
					left[i] = GINT16_FROM_LE(*ptr16);
					ptr16++;
					right[i] = GINT16_FROM_LE(*ptr16);
					ptr16++;
				}
			break;
		case FMT_S16_BE:
			ptr16 = ptr;
			if (nch == 1)
				for (i = 0; i < max; i++, ptr16++)
					left[i] = GINT16_FROM_BE(*ptr16);
			else
				for (i = 0; i < max; i++)
				{
					left[i] = GINT16_FROM_BE(*ptr16);
					ptr16++;
					right[i] = GINT16_FROM_BE(*ptr16);
					ptr16++;
				}
			break;
		case FMT_S16_NE:
			ptr16 = ptr;
			if (nch == 1)
				for (i = 0; i < max; i++)
					left[i] = (*ptr16++);
			else
				for (i = 0; i < max; i++)
				{
					left[i] = (*ptr16++);
					right[i] = (*ptr16++);
				}
			break;
	}
}

InputVisType input_get_vis_type(void)
{
	return INPUT_VIS_OFF;
}
	
void input_add_vis_pcm(int time, AFormat fmt, int nch, int length, void *ptr)
{
	struct vis_node *vis_node;
	int max;

	max = length / nch;
	if (fmt == FMT_U16_LE || fmt == FMT_U16_BE || fmt == FMT_U16_NE ||
	    fmt == FMT_S16_LE || fmt == FMT_S16_BE || fmt == FMT_S16_NE)
		max /= 2;
	max = CLAMP(max, 0, 512);

	vis_node = g_malloc0(sizeof(*vis_node));
	vis_node->time = time;
	vis_node->nch = nch;
	vis_node->length = max;
	convert_to_s16_ne(fmt,ptr,vis_node->data[0],vis_node->data[1],nch,max);
	
	pthread_mutex_lock(&vis_mutex);
	vis_list = g_list_append(vis_list, vis_node);
	pthread_mutex_unlock(&vis_mutex);
}

/*
 * An input plugin's decoded audio: to the visualization plugins, timed
 * by where it starts, and written once there is room.
 */
int input_pass_audio(AFormat fmt, int nch, int length, void *data)
{
	OutputPlugin *op = output_pipeline();

	input_add_vis_pcm(op->written_time(), fmt, nch, length, data);
	return output_pass_audio(data, length);
}

gboolean input_check_file(gchar * filename)
{
	GList *node;
	InputPlugin *ip;

	node = get_input_list();
	while (node)
	{
		ip = (InputPlugin *) node->data;
		if (ip && !g_list_find(disabled_iplugins, ip) &&
		    ip->is_our_file(filename))
			return TRUE;
		node = node->next;
	}
	return FALSE;
}

/* A call to the plugin, with ctl_lock let go while it is under way. */
static void call_begin(const gchar *what)
{
	busy = what;
	g_timer_start(busy_timer);
	busy_told = FALSE;
	pthread_mutex_unlock(&ctl_lock);
}

static void call_end(void)
{
	pthread_mutex_lock(&ctl_lock);
	if (busy_told)
		g_message("input: %s %s: done after %.0f seconds", busy,
			  g_basename(cur_file), g_timer_elapsed(busy_timer, NULL));
	busy = NULL;
}

static void *ctl_loop(void *arg)
{
	InputPlugin *ip;
	ConfigFile *preset;
	GList *node;
	gint time;

	(void) arg;
	pthread_mutex_lock(&ctl_lock);
	for (;;)
	{
		ip = cur_ip;
		if (cur_open && (!want_file || cur_serial != want_serial))
		{
			call_begin("stopping");
			output_interrupt();
			if (ip && ip->stop)
				ip->stop();
			output_interrupt_done();
			free_vis_data();
			call_end();
			cur_open = cur_paused = FALSE;
			cur_ip = NULL;
		}
		else if (!cur_open && want_file)
		{
			cur_serial = want_serial;
			g_free(cur_file);
			cur_file = g_strdup(want_file);
			call_begin("opening");
			/* Started even if no plugin takes it: then it is skipped. */
			for (node = get_input_list(); node && !ip; node = node->next)
				if (node->data && !g_list_find(disabled_iplugins, node->data) &&
				    ((InputPlugin *) node->data)->is_our_file(cur_file))
					ip = node->data;
			if (ip)
			{
				ip->output = output_pipeline();
				ip->play_file(cur_file);
				preset = equalizerwin_open_auto_preset(cur_file);
				GDK_THREADS_ENTER();
				equalizerwin_load_auto_preset(cur_file, preset);
				input_set_eq(cfg.equalizer_active, cfg.equalizer_preamp,
					     cfg.equalizer_bands);
				GDK_THREADS_LEAVE();
				if (preset)
					xmms_cfg_free(preset);
			}
			call_end();
			cur_open = TRUE;
			cur_ip = ip;
		}
		else if (cur_open && cur_paused != want_paused)
		{
			cur_paused = want_paused;
			if (ip)
			{
				call_begin(cur_paused ? "pausing" : "resuming");
				ip->pause(cur_paused);
				call_end();
			}
		}
		else if (cur_open && want_seek >= 0)
		{
			time = want_seek;
			want_seek = -1;
			if (ip)
			{
				call_begin("seeking in");
				free_vis_data();
				output_interrupt();
				ip->seek(time);
				output_interrupt_done();
				call_end();
			}
		}
		else if (ctl_quit)
			break;
		else
			pthread_cond_wait(&ctl_cond, &ctl_lock);
	}
	pthread_mutex_unlock(&ctl_lock);
	return NULL;
}

void input_start(void)
{
	busy_timer = g_timer_new();
	ctl_up = !pthread_create(&ctl_thread, NULL, ctl_loop, NULL);
}

/* Quitting: the thread ends once what played has stopped. */
void input_shutdown(void)
{
	pthread_mutex_lock(&ctl_lock);
	ctl_quit = TRUE;
	pthread_cond_signal(&ctl_cond);
	pthread_mutex_unlock(&ctl_lock);
	if (ctl_up)
		pthread_join(ctl_thread, NULL);
	ctl_up = FALSE;
}

/* From the main loop: a call that is taking long is said, once. */
void input_poll(void)
{
	pthread_mutex_lock(&ctl_lock);
	if (busy && !busy_told && g_timer_elapsed(busy_timer, NULL) >= STALL_SECS)
	{
		busy_told = TRUE;
		g_message("input: %s %s: still waiting after %d seconds", busy,
			  g_basename(cur_file), STALL_SECS);
	}
	pthread_mutex_unlock(&ctl_lock);
}

/* A song to play from its start, or NULL to stop. */
static void want(const gchar *filename)
{
	pthread_mutex_lock(&ctl_lock);
	g_free(want_file);
	want_file = g_strdup(filename);
	want_serial++;
	want_paused = FALSE;
	want_seek = -1;
	last_time = 0;
	pthread_cond_signal(&ctl_cond);
	pthread_mutex_unlock(&ctl_lock);
}

void input_play(char *filename)
{
	if (get_current_output_plugin() == NULL)
	{
		xmms_show_message(_("No output plugin"),
				  _("No output plugin has been selected"),
				  _("OK"), FALSE, NULL, NULL);
		mainwin_stop_pushed();
		return;
	}

	/* We set the playing flag even if no inputplugin
	   recognizes the file. This way we are sure it will be skipped. */
	ip_data->playing = TRUE;
	want(filename);
}

void input_seek(int time)
{
	if (ip_data->playing)
	{
		pthread_mutex_lock(&ctl_lock);
		want_seek = time;
		last_time = time * 1000;
		pthread_cond_signal(&ctl_cond);
		pthread_mutex_unlock(&ctl_lock);
	}
}

void input_stop(void)
{
	if (ip_data->playing)
	{
		ip_data->playing = FALSE;

		if (ip_data->paused)
			input_pause();
		want(NULL);

		ip_data->paused = FALSE;
		if (input_info_text)
		{
			g_free(input_info_text);
			input_info_text = NULL;
			mainwin_set_info_text();
		}
	}
	ip_data->playing = FALSE;
}

void input_pause(void)
{
	if (get_input_playing())
	{
		ip_data->paused = !ip_data->paused;
		if (ip_data->paused)
			playstatus_set_status(mainwin_playstatus, STATUS_PAUSE);
		else
			playstatus_set_status(mainwin_playstatus, STATUS_PLAY);
		pthread_mutex_lock(&ctl_lock);
		want_paused = ip_data->paused;
		pthread_cond_signal(&ctl_cond);
		pthread_mutex_unlock(&ctl_lock);
	}
}

gint input_get_time(void)
{
	gint time;

	if (!get_input_playing())
		return -1;
	pthread_mutex_lock(&ctl_lock);
	/* The song asked for, with no call to its plugin under way. */
	if (cur_open && cur_serial == want_serial && !busy && want_seek < 0)
	{
		time = cur_ip ? cur_ip->get_time() : -1;
		if (time >= 0)
			last_time = time;
	}
	else
		time = last_time;
	pthread_mutex_unlock(&ctl_lock);
	return time;
}

/*
 * The equalizer is XMMS's, in its output pipeline, for every input
 * plugin alike; a plugin's own set_eq() is not called.
 */
void input_set_eq(int on, float preamp, float *bands)
{
	eq_filter_set(on, preamp, bands);
}

void input_get_song_info(gchar * filename, gchar ** title, gint * length)
{
	GList *node;
	InputPlugin *ip = NULL;

	node = get_input_list();
	while (node)
	{
		ip = (InputPlugin *) node->data;
		if (!g_list_find(disabled_iplugins, ip) &&
		    ip->is_our_file(filename))
			break;
		node = node->next;
	}
	if (ip && node && ip->get_song_info)
		ip->get_song_info(filename, title, length);
	else
	{
		gchar *temp, *ext;
		TitleInput *input;

		XMMS_NEW_TITLEINPUT(input);
		temp = g_strdup(filename);
		ext = strrchr(temp, '.');
		if (ext)
			*ext = '\0';
		input->file_name = g_basename(temp);
		input->file_ext = ext ? ext+1 : NULL;
		input->file_path = temp;

		(*title) = xmms_get_titlestring(xmms_get_gentitle_format(),
						input);
		if ( (*title) == NULL )
		    (*title) = g_strdup(input->file_name);
		(*length) = -1;
		g_free(temp);
		g_free(input);
	}
}

static void input_general_file_info_box(char *filename, InputPlugin *ip)
{
	GtkWidget *window, *vbox;
	GtkWidget *label, *filename_hbox, *filename_entry;
	GtkWidget *bbox, *cancel;

	char *title, *iplugin;
		
	window = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_policy(GTK_WINDOW(window), FALSE, TRUE, FALSE);
	title = g_strdup_printf(_("File Info - %s"), g_basename(filename));
	gtk_window_set_title(GTK_WINDOW(window), title);
	g_free(title);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	filename_hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), filename_hbox, FALSE, TRUE, 0);

	label = gtk_label_new(_("Filename:"));
	gtk_box_pack_start(GTK_BOX(filename_hbox), label, FALSE, TRUE, 0);
	filename_entry = xmms_entry_new();
	gtk_entry_set_text(GTK_ENTRY(filename_entry), filename);
	gtk_editable_set_editable(GTK_EDITABLE(filename_entry), FALSE);
	gtk_box_pack_start(GTK_BOX(filename_hbox), filename_entry, TRUE, TRUE, 0);

	if (ip)
		if (ip->description)
			iplugin = ip->description;
		else
			iplugin = ip->filename;
	else
		iplugin = _("No input plugin recognized this file");

	title = g_strdup_printf(_("Input plugin: %s"), iplugin);

	label = gtk_label_new(title);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
	g_free(title);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);

	bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
	gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

	cancel = gtk_button_new_with_label(_("Close"));
	gtk_signal_connect_object(GTK_OBJECT(cancel), "clicked",
				  GTK_SIGNAL_FUNC(gtk_widget_destroy),
				  GTK_OBJECT(window));
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), cancel, TRUE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(window), "key_press_event",
			   GTK_SIGNAL_FUNC(util_dialog_keypress_cb), NULL);

	gtk_widget_show_all(window);
}

void input_file_info_box(gchar * filename)
{
	GList *node;
	InputPlugin *ip;

	node = get_input_list();
	while (node)
	{
		ip = (InputPlugin *) node->data;
		if (!g_list_find(disabled_iplugins, ip) && ip->is_our_file(filename))
		{
			if (ip->file_info_box)
				ip->file_info_box(filename);
			else
				input_general_file_info_box(filename, ip);
			return;
		}
		node = node->next;
	}
	input_general_file_info_box(filename, NULL);
}

GList *input_scan_dir(gchar * dir)
{
	GList *node, *ret = NULL;
	InputPlugin *ip;

	node = get_input_list();
	while (node && !ret)
	{
		ip = (InputPlugin *) node->data;
		if (ip && ip->scan_dir && !g_list_find(disabled_iplugins, ip))
			ret = ip->scan_dir(dir);
		node = g_list_next(node);
	}
	return ret;
}

void input_get_volume(int *l, int *r)
{
	*l = -1;
	*r = -1;
	if (get_input_playing())
	{
		if (get_current_input_plugin() &&
		    get_current_input_plugin()->get_volume)
		{
			get_current_input_plugin()->get_volume(l, r);
			return;
		}
	}
	output_get_volume(l, r);

}

void input_set_volume(int l, int r)
{
	if (get_input_playing())
	{
		if (get_current_input_plugin() &&
		    get_current_input_plugin()->set_volume)
		{
			get_current_input_plugin()->set_volume(l, r);
			return;
		}
	}
	output_set_volume(l, r);
}

gboolean get_input_playing(void)
{
	return ip_data->playing;
}

gboolean get_input_paused(void)
{
	return ip_data->paused;
}

void input_update_vis(int time)
{
	GList *node;
	struct vis_node *vis = NULL;
	gboolean found = FALSE;

	pthread_mutex_lock(&vis_mutex);
	node = vis_list;
	while (g_list_next(node) && !found)
	{
		struct vis_node *visnext = node->next->data;
		vis = node->data;

		if (!(vis->time < time))
			break;

		vis_list = g_list_remove_link(vis_list, node);
		g_list_free_1(node);
		if (visnext->time >= time)
		{
			found = TRUE;
			break;
		}
		g_free(vis);
		node = vis_list;
	}
	pthread_mutex_unlock(&vis_mutex);
	if (found)
	{
		vis_send_data(vis->data, vis->nch, vis->length);
		g_free(vis);
	}
	else
		vis_send_data(NULL, 0, 0);
}
	

gchar *input_get_info_text(void)
{
	return input_info_text;
}

void input_set_info_text(gchar * text)
{
	if (input_info_text)
		g_free(input_info_text);
	if (text)
		input_info_text = g_strdup(text);
	else
		input_info_text = NULL;
	mainwin_set_info_text();
}

void input_about(gint index)
{
	InputPlugin *ip;

	ip = g_list_nth(ip_data->input_list, index)->data;
	if (ip && ip->about)
		ip->about();
}

void input_configure(gint index)
{
	InputPlugin *ip;

	ip = g_list_nth(ip_data->input_list, index)->data;
	if (ip && ip->configure)
		ip->configure();
}
