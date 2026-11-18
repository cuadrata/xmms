/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2002  Peter Alm, Mikael Alm, Olle Hallnas,
 *                           Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 1999-2004  Haavard Kvaalen
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
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <libxmms/xentry.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <mpg123.h>

#include "libxmms/charset.h"
#include "libxmms/util.h"
#include "xmms/i18n.h"

#include "mpg123-plugin.h"
#include "id3v2.h"
#include "httpstream.h"

static GtkWidget *window = NULL;
static GtkWidget *filename_entry, *id3_frame;
static GtkWidget *title_entry, *artist_entry, *album_entry, *year_entry;
static GtkWidget *tracknum_entry, *comment_entry, *genre_combo;
static GtkWidget *mpeg_level, *mpeg_bitrate, *mpeg_samplerate, *mpeg_flags;
static GtkWidget *mpeg_fileinfo;

static GList *genre_list;
struct genre_item {
	const char *name;
	int id;
};
static char *current_filename;

/*
 * MPEG audio in a RIFF WAVE file cannot have an ID3v2 tag in front;
 * such a file gets an ID3v1 tag only, as it always did.
 */
static gboolean current_riff;

/* The ID3v2 text the entries were given, to see what was changed. */
static gchar *loaded[ID3V2_FIELDS];


#define MAX_STR_LEN 100

static void label_set_text(GtkWidget * label, char *str, ...)
G_GNUC_PRINTF(2, 3);

/* ID3v1 text is ISO 8859-1; the entries show it in the locale's charset. */
static void set_entry_tag(GtkEntry * entry, char * tag, int length)
{
	char *latin1 = g_strchomp(g_strndup(tag, length));
	char *text = xmms_charset_convert(latin1, strlen(latin1), "ISO-8859-1", NULL);

	gtk_entry_set_text(entry, text);
	g_free(text);
	g_free(latin1);
}

/* Fill a field, padding it with NULs as ID3v1 does. */
static void get_entry_tag(GtkEntry * entry, char * tag, int length)
{
	char *text = gtk_entry_get_text(entry);
	char *latin1 = xmms_charset_convert(text, strlen(text), NULL, "ISO-8859-1");
	size_t n = strlen(latin1);

	memset(tag, 0, length);
	memcpy(tag, latin1, MIN(n, (size_t) length));
	g_free(latin1);
}

static int genre_comp_func(gconstpointer a, gconstpointer b)
{
	const struct genre_item *ga = a, *gb = b;
	return g_ascii_strcasecmp(ga->name, gb->name);
}

/* The ID3v1 number of a genre named in the locale's language or English. */
static int genre_number(const char *name)
{
	int i;

	if (!*name)
		return -1;
	for (i = 0; i < GENRE_MAX; i++)
		if (!strcmp(name, gettext(mpg123_id3_genres[i])) ||
		    !g_ascii_strcasecmp(name, mpg123_id3_genres[i]))
			return i;
	return -1;
}

static GtkWidget *field_entry(ID3v2Field field)
{
	switch (field)
	{
	case ID3V2_TITLE:
		return title_entry;
	case ID3V2_ARTIST:
		return artist_entry;
	case ID3V2_ALBUM:
		return album_entry;
	case ID3V2_YEAR:
		return year_entry;
	case ID3V2_TRACK:
		return tracknum_entry;
	case ID3V2_GENRE:
		return GTK_COMBO(genre_combo)->entry;
	default:
		return comment_entry;
	}
}

static void show_message(char *msg)
{
	GtkWidget *mwin = xmms_show_message(_("File Info"), msg,
					    _("OK"), FALSE, NULL, NULL);

	gtk_window_set_transient_for(GTK_WINDOW(mwin), GTK_WINDOW(window));
}

/* What went wrong with the ID3v2 tag, after what was being done. */
static char *id3v2_message(const char *what, ID3v2Error error)
{
	const char *why;

	switch (error)
	{
	case ID3V2_OK:
		return NULL;
	case ID3V2_OPEN_FAILED:
		return g_strdup_printf(_("%s\nUnable to open file: %s"),
				       what, strerror(errno));
	case ID3V2_WRITE_FAILED:
		return g_strdup_printf(_("%s\nUnable to write to file: %s"),
				       what, strerror(errno));
	case ID3V2_UNSUPPORTED:
		why = _("The file's ID3v2 tag is of a kind that cannot be edited.");
		break;
	case ID3V2_TRUNCATED:
		why = _("The file's ID3v2 tag is cut short.");
		break;
	default:
		why = _("The file changed while its tag was being edited.");
		break;
	}
	return g_strdup_printf("%s\n%s", what, why);
}

static gboolean is_riff(const char *filename)
{
	FILE *fh = fopen(filename, "rb");
	char magic[4];
	gboolean riff = FALSE;

	if (fh)
	{
		riff = fread(magic, 1, 4, fh) == 4 && !memcmp(magic, "RIFF", 4);
		fclose(fh);
	}
	return riff;
}

/*
 * What the ID3v2 tag says goes over what the ID3v1 tag said. A field
 * it does not have keeps ID3v1's text, which saving then puts in it.
 */
static void show_id3v2(void)
{
	ID3v2Tag *tag;
	ID3v2Error error;
	int i;

	/* If it cannot be read, saving says why. */
	if (!(tag = id3v2_read(current_filename, &error)))
		return;
	for (i = 0; i < ID3V2_FIELDS; i++)
	{
		GtkEntry *entry = GTK_ENTRY(field_entry(i));
		char *utf8 = id3v2_get(tag, i), *text;
		int genre;

		if (!utf8)
			continue;
		if (i == ID3V2_GENRE && (genre = genre_number(utf8)) >= 0)
			text = g_strdup(gettext(mpg123_id3_genres[genre]));
		else
			text = xmms_charset_from_utf8(utf8);
		gtk_entry_set_text(entry, text);
		gtk_editable_set_position(GTK_EDITABLE(entry), 0);
		/* As the entry has it, which may be cut to its length. */
		loaded[i] = g_strdup(gtk_entry_get_text(entry));
		g_free(text);
		g_free(utf8);
	}
	id3v2_free(tag);
}

/* Put what the entries say in the ID3v2 tag, where they were changed. */
static char *save_id3v2(void)
{
	ID3v2Tag *tag;
	ID3v2Error error;
	gboolean changed = FALSE;
	int i, saved;

	if (!(tag = id3v2_read(current_filename, &error)))
		return id3v2_message(_("Couldn't write tag!"), error);
	for (i = 0; i < ID3V2_FIELDS; i++)
	{
		char *text = gtk_entry_get_text(GTK_ENTRY(field_entry(i)));
		char *utf8;
		int genre;

		if (loaded[i] ? !strcmp(text, loaded[i]) : !*text)
			continue;
		/* A known genre is stored by its English name. */
		if (i == ID3V2_GENRE && (genre = genre_number(text)) >= 0)
			utf8 = g_strdup(mpg123_id3_genres[genre]);
		else
			utf8 = xmms_charset_to_utf8(text);
		id3v2_set(tag, i, g_strstrip(utf8));
		g_free(utf8);
		changed = TRUE;
	}
	error = changed ? id3v2_write(tag, current_filename) : ID3V2_OK;
	saved = errno;
	id3v2_free(tag);
	errno = saved;
	return id3v2_message(_("Couldn't write tag!"), error);
}

/*
 * Replace the ID3v1 tag the file ends with, and append one only if
 * asked to. A file too short to hold one, or one that cannot be read,
 * has none. Text too long for ID3v1 is cut short.
 */
static char *save_id3v1(gboolean append)
{
	int fd;
	struct id3v1tag_t tag;
	char *msg = NULL;

	if ((fd = open(current_filename, O_RDWR)) != -1)
	{
		int tracknum, genre;

		memset(&tag, 0, sizeof (tag));
		if (lseek(fd, -128, SEEK_END) < 0 ||
		    read(fd, &tag, sizeof (tag)) != sizeof (tag))
			memset(&tag, 0, sizeof (tag));

		if (!strncmp(tag.tag, "TAG", 3))
			lseek(fd, -128, SEEK_END);
		else if (append)
			lseek(fd, 0, SEEK_END);
		else
		{
			close(fd);
			return NULL;
		}
		tag.tag[0] = 'T';
		tag.tag[1] = 'A';
		tag.tag[2] = 'G';
		get_entry_tag(GTK_ENTRY(title_entry), tag.title, 30);
		get_entry_tag(GTK_ENTRY(artist_entry), tag.artist, 30);
		get_entry_tag(GTK_ENTRY(album_entry), tag.album, 30);
		get_entry_tag(GTK_ENTRY(year_entry), tag.year, 4);
		tracknum = atoi(gtk_entry_get_text(GTK_ENTRY(tracknum_entry)));
		if (tracknum > 0)
		{
			get_entry_tag(GTK_ENTRY(comment_entry),
				      tag.u.v1_1.comment, 28);
			tag.u.v1_1.__zero = 0;
			tag.u.v1_1.track_number = MIN(tracknum, 255);
		}
		else
			get_entry_tag(GTK_ENTRY(comment_entry),
				      tag.u.v1_0.comment, 30);
		genre = genre_number(gtk_entry_get_text(
			GTK_ENTRY(GTK_COMBO(genre_combo)->entry)));
		tag.genre = genre >= 0 ? genre : 0xff;
		if (write(fd, &tag, sizeof (tag)) != sizeof (tag))
			msg = g_strdup_printf(_("%s\nUnable to write to file: %s"),
					      _("Couldn't write tag!"),
					      strerror(errno));
		close(fd);
	}
	else
		msg = g_strdup_printf(_("%s\nUnable to open file: %s"),
				      _("Couldn't write tag!"),
				      strerror(errno));
	return msg;
}

/*
 * The tag is ID3v2. An ID3v1 tag the file has is brought up to date
 * with it, so that the two do not disagree.
 */
static void save_cb(GtkWidget * w, gpointer data)
{
	char *msg = NULL;

	if (http_stream_is_url(current_filename))
		return;

	if (!current_riff)
		msg = save_id3v2();
	if (!msg)
		msg = save_id3v1(current_riff);
	if (msg)
	{
		show_message(msg);
		g_free(msg);
	}
	else
		gtk_widget_destroy(window);
}

static void label_set_text(GtkWidget * label, char *str, ...)
{
	va_list args;
	char tempstr[MAX_STR_LEN];

	va_start(args, str);
	g_vsnprintf(tempstr, MAX_STR_LEN, str, args);
	va_end(args);

	gtk_label_set_text(GTK_LABEL(label), tempstr);
}

/* Remove the ID3v1 tag, if the file ends with one. */
static char *remove_id3v1(gboolean *removed)
{
	int fd;
	off_t len;
	struct id3v1tag_t tag;
	char *msg = NULL;

	if ((fd = open(current_filename, O_RDWR)) == -1)
		return g_strdup_printf(_("%s\nUnable to open file: %s"),
				       _("Couldn't remove tag!"),
				       strerror(errno));
	memset(&tag, 0, sizeof (tag));
	len = lseek(fd, -128, SEEK_END);
	if (len < 0 || read(fd, &tag, sizeof (tag)) != sizeof (tag))
		memset(&tag, 0, sizeof (tag));
	if (!strncmp(tag.tag, "TAG", 3))
	{
		if (ftruncate(fd, len))
			msg = g_strdup_printf(_("%s\n"
						"Unable to truncate file: %s"),
					      _("Couldn't remove tag!"),
					      strerror(errno));
		*removed = TRUE;
	}
	close(fd);
	return msg;
}

/* Remove both kinds of tag. */
static void remove_id3_cb(GtkWidget * w, gpointer data)
{
	char *msg = NULL;
	gboolean removed = FALSE;

	if (http_stream_is_url(current_filename))
		return;

	if (!current_riff)
	{
		ID3v2Error error;
		int saved;
		ID3v2Tag *v2 = id3v2_read(current_filename, &error);

		if (v2 && v2->region > 0)
		{
			id3v2_clear(v2);
			error = id3v2_write(v2, current_filename);
			removed = TRUE;
		}
		saved = errno;
		id3v2_free(v2);
		errno = saved;
		msg = id3v2_message(_("Couldn't remove tag!"), error);
	}
	if (!msg)
		msg = remove_id3v1(&removed);

	if (msg)
	{
		show_message(msg);
		g_free(msg);
	}
	else if (!removed)
	{
		GtkWidget *mwin = xmms_show_message(_("File Info"),
						    _("No tag to remove!"),
						    _("OK"), FALSE, NULL, NULL);
		gtk_window_set_transient_for(GTK_WINDOW(mwin), GTK_WINDOW(window));
	}
	else
		gtk_widget_destroy(window);
}

static void set_mpeg_level_label(int version, int layer)
{
	static const char *versions[] = { "1", "2", "2.5" };

	label_set_text(mpeg_level, "MPEG %s, layer %d",
		       version >= 0 && version <= 2 ? versions[version] : "?",
		       layer);
}

static char* channel_mode_name(int mode)
{
	static const char *channel_mode[] =
		{N_("Stereo"), N_("Joint stereo"),
		 N_("Dual channel"), N_("Single channel")};
	if (mode < 0 || mode > 3)
		return "";
	return gettext(channel_mode[mode]);
}

static void file_info_http(char *filename)
{
	gint version, layer, bitrate, mode;
	glong rate;

	gtk_widget_set_sensitive(id3_frame, FALSE);
	if (mpg123_playing_url && !strcmp(filename, mpg123_playing_url) &&
	    mpg123_playing_info(&version, &layer, &bitrate, &rate, &mode))
	{
		set_mpeg_level_label(version, layer);
		label_set_text(mpeg_bitrate, _("Bitrate: %d kb/s"), bitrate);
		label_set_text(mpeg_samplerate, _("Samplerate: %d Hz"), (int) rate);
		label_set_text(mpeg_flags, "%s", channel_mode_name(mode));
	}
}

static void genre_set_popdown(GtkWidget *combo, GList *genres)
{
	struct genre_item *item;
	GtkWidget *w;

	while (genres)
	{
		item = genres->data;
		w = gtk_list_item_new_with_label(item->name);
		gtk_widget_show(w);
		gtk_container_add(GTK_CONTAINER(GTK_COMBO(combo)->list), w);
		genres = genres->next;
	}
}

static void file_info_box_keypress_cb(GtkWidget *w, GdkEventKey *event, gpointer data)
{
	if (event && event->keyval == GDK_Escape)
		gtk_widget_destroy(w);
}

/* What libmpg123 reports of the stream in a file. */
static void show_mpeg_info(char *filename, long filesize)
{
	static const char *emphasis[4];
	static const char *bool_label[2];
	mpg123_handle *mh;
	struct mpg123_frameinfo fi;
	off_t frames;

	emphasis[0] = _("None");
	emphasis[1] = _("50/15 ms");
	emphasis[2] = "";
	emphasis[3] = _("CCITT J.17");
	bool_label[0] = _("No");
	bool_label[1] = _("Yes");

	if (!(mh = mpg123_new(NULL, NULL)))
		return;
	mpg123_param(mh, MPG123_FLAGS, MPG123_QUIET, 0);
	if (mpg123_open(mh, filename) != MPG123_OK)
	{
		mpg123_delete(mh);
		return;
	}
	/* Counting the frames reads the whole file, but gives exact figures. */
	mpg123_scan(mh);
	if (mpg123_info(mh, &fi) == MPG123_OK)
	{
		frames = mpg123_framelength(mh);
		set_mpeg_level_label(fi.version, fi.layer);
		if (fi.vbr == MPG123_CBR)
			label_set_text(mpeg_bitrate, _("Bitrate: %d kb/s"), fi.bitrate);
		else
		{
			off_t samples = mpg123_length(mh);
			int avg = samples > 0 && fi.rate > 0 ?
				(int) (filesize * 8.0 / (samples / (double) fi.rate) / 1000.0) : 0;

			label_set_text(mpeg_bitrate,
				       _("Bitrate: Variable,\n"
					 "avg. bitrate: %d kb/s"), avg);
		}
		label_set_text(mpeg_samplerate, _("Samplerate: %d Hz"), (int) fi.rate);
		label_set_text(mpeg_flags, _("%s\nError protection: %s\n"
					     "Copyright: %s\nOriginal: %s\n"
					     "Emphasis: %s"),
			       channel_mode_name(fi.mode),
			       bool_label[(fi.flags & MPG123_CRC) != 0],
			       bool_label[(fi.flags & MPG123_COPYRIGHT) != 0],
			       bool_label[(fi.flags & MPG123_ORIGINAL) != 0],
			       emphasis[fi.emphasis & 3]);
		label_set_text(mpeg_fileinfo, _("%d frames\nFilesize: %lu B"),
			       (int) (frames > 0 ? frames : 0),
			       (unsigned long) filesize);
	}
	mpg123_close(mh);
	mpg123_delete(mh);
}

void mpg123_file_info_box(char *filename)
{
	int i;
	struct id3v1tag_t tag;
	FILE *fh;
	char *tmp, *title;

	if (!window)
	{
		GtkWidget *vbox, *hbox, *left_vbox, *table;
		GtkWidget *mpeg_frame, *mpeg_box;
		GtkWidget *label, *filename_hbox;
		GtkWidget *bbox, *save, *remove_id3, *cancel;
		
		window = gtk_window_new(GTK_WINDOW_DIALOG);
		gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, FALSE);
		gtk_signal_connect(GTK_OBJECT(window), "destroy",
				   GTK_SIGNAL_FUNC(gtk_widget_destroyed), &window);
		gtk_signal_connect(GTK_OBJECT(window), "key_press_event",
				   GTK_SIGNAL_FUNC(file_info_box_keypress_cb), NULL);
		gtk_container_set_border_width(GTK_CONTAINER(window), 10);

		vbox = gtk_vbox_new(FALSE, 10);
		gtk_container_add(GTK_CONTAINER(window), vbox);

		filename_hbox = gtk_hbox_new(FALSE, 5);
		gtk_box_pack_start(GTK_BOX(vbox), filename_hbox,
				   FALSE, TRUE, 0);

		label = gtk_label_new(_("Filename:"));
		gtk_box_pack_start(GTK_BOX(filename_hbox), label,
				   FALSE, TRUE, 0);
		filename_entry = xmms_entry_new();
		gtk_editable_set_editable(GTK_EDITABLE(filename_entry), FALSE);
		gtk_box_pack_start(GTK_BOX(filename_hbox),
				   filename_entry, TRUE, TRUE, 0);
		
		hbox = gtk_hbox_new(FALSE, 10);
		gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);
		
		left_vbox = gtk_vbox_new(FALSE, 10);
		gtk_box_pack_start(GTK_BOX(hbox), left_vbox, FALSE, FALSE, 0);

		id3_frame = gtk_frame_new(_("ID3 Tag:"));
		gtk_box_pack_start(GTK_BOX(left_vbox), id3_frame,
				   FALSE, FALSE, 0);

		table = gtk_table_new(5, 5, FALSE);
		gtk_container_set_border_width(GTK_CONTAINER(table), 5);
		gtk_container_add(GTK_CONTAINER(id3_frame), table);

		label = gtk_label_new(_("Title:"));
		gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
		gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
				 GTK_FILL, GTK_FILL, 5, 5);

		title_entry = gtk_entry_new();
		gtk_table_attach(GTK_TABLE(table), title_entry, 1, 4, 0, 1,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 5);

		label = gtk_label_new(_("Artist:"));
		gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
		gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
				 GTK_FILL, GTK_FILL, 5, 5);

		artist_entry = gtk_entry_new();
		gtk_table_attach(GTK_TABLE(table), artist_entry, 1, 4, 1, 2,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 5);

		label = gtk_label_new(_("Album:"));
		gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
		gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
				 GTK_FILL, GTK_FILL, 5, 5);

		album_entry = gtk_entry_new();
		gtk_table_attach(GTK_TABLE(table), album_entry, 1, 4, 2, 3,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 5);

		label = gtk_label_new(_("Comment:"));
		gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
		gtk_table_attach(GTK_TABLE(table), label, 0, 1, 3, 4,
				 GTK_FILL, GTK_FILL, 5, 5);

		comment_entry = gtk_entry_new();
		gtk_table_attach(GTK_TABLE(table), comment_entry, 1, 4, 3, 4,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 5);

		label = gtk_label_new(_("Year:"));
		gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
		gtk_table_attach(GTK_TABLE(table), label, 0, 1, 4, 5,
				 GTK_FILL, GTK_FILL, 5, 5);

		/* Room for a date, which ID3v2.4 allows. */
		year_entry = gtk_entry_new_with_max_length(10);
		gtk_widget_set_usize(year_entry, 80, -1);
		gtk_table_attach(GTK_TABLE(table), year_entry, 1, 2, 4, 5,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 5);

		label = gtk_label_new(_("Track number:"));
		gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
		gtk_table_attach(GTK_TABLE(table), label, 2, 3, 4, 5,
				 GTK_FILL, GTK_FILL, 5, 5);

		/* Room for "3/12": a track of how many. */
		tracknum_entry = gtk_entry_new_with_max_length(7);
		gtk_widget_set_usize(tracknum_entry, 60, -1);
		gtk_table_attach(GTK_TABLE(table), tracknum_entry, 3, 4, 4, 5,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 5);

		label = gtk_label_new(_("Genre:"));
		gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
		gtk_table_attach(GTK_TABLE(table), label, 0, 1, 5, 6,
				 GTK_FILL, GTK_FILL, 5, 5);

		/* ID3v2 genres can be anything; the list has ID3v1's. */
		genre_combo = gtk_combo_new();
		if (!genre_list)
		{
			struct genre_item *item;

			for (i = 0; i < GENRE_MAX; i++)
			{
				item = g_malloc(sizeof (*item));
				item->name = gettext(mpg123_id3_genres[i]);
				item->id = i;
				genre_list = g_list_prepend(genre_list, item);
			}
			item = g_malloc(sizeof (*item));
			item->name = "";
			item->id = 0xff;
			genre_list = g_list_prepend(genre_list, item);
			genre_list = g_list_sort(genre_list, genre_comp_func);
		}
		genre_set_popdown(genre_combo, genre_list);

		gtk_table_attach(GTK_TABLE(table), genre_combo, 1, 4, 5, 6,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK,
				 GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 5);

		bbox = gtk_hbutton_box_new();
		gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox),
					  GTK_BUTTONBOX_END);
		gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
		gtk_box_pack_start(GTK_BOX(left_vbox), bbox, FALSE, FALSE, 0);

		save = gtk_button_new_with_label(_("Save"));
		gtk_signal_connect(GTK_OBJECT(save), "clicked", GTK_SIGNAL_FUNC(save_cb), NULL);
		GTK_WIDGET_SET_FLAGS(save, GTK_CAN_DEFAULT);
		gtk_box_pack_start(GTK_BOX(bbox), save, TRUE, TRUE, 0);
		gtk_widget_grab_default(save);

		remove_id3 = gtk_button_new_with_label(_("Remove ID3"));
		gtk_signal_connect(GTK_OBJECT(remove_id3), "clicked",
				   GTK_SIGNAL_FUNC(remove_id3_cb), NULL);
		GTK_WIDGET_SET_FLAGS(remove_id3, GTK_CAN_DEFAULT);
		gtk_box_pack_start(GTK_BOX(bbox), remove_id3, TRUE, TRUE, 0);

		cancel = gtk_button_new_with_label(_("Cancel"));
		gtk_signal_connect_object(GTK_OBJECT(cancel), "clicked",
					  GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(window));
		GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
		gtk_box_pack_start(GTK_BOX(bbox), cancel, TRUE, TRUE, 0);

		mpeg_frame = gtk_frame_new(_("MPEG Info:"));
		gtk_box_pack_start(GTK_BOX(hbox), mpeg_frame, FALSE, FALSE, 0);

		mpeg_box = gtk_vbox_new(FALSE, 5);
		gtk_container_add(GTK_CONTAINER(mpeg_frame), mpeg_box);
		gtk_container_set_border_width(GTK_CONTAINER(mpeg_box), 10);
		gtk_box_set_spacing(GTK_BOX(mpeg_box), 0);

		mpeg_level = gtk_label_new("");
		gtk_widget_set_usize(mpeg_level, 120, -2);
		gtk_misc_set_alignment(GTK_MISC(mpeg_level), 0, 0);
		gtk_box_pack_start(GTK_BOX(mpeg_box), mpeg_level, FALSE, FALSE, 0);

		mpeg_bitrate = gtk_label_new("");
		gtk_misc_set_alignment(GTK_MISC(mpeg_bitrate), 0, 0);
		gtk_label_set_justify(GTK_LABEL(mpeg_bitrate),
				      GTK_JUSTIFY_LEFT);
		gtk_box_pack_start(GTK_BOX(mpeg_box),
				   mpeg_bitrate, FALSE, FALSE, 0);

		mpeg_samplerate = gtk_label_new("");
		gtk_misc_set_alignment(GTK_MISC(mpeg_samplerate), 0, 0);
		gtk_box_pack_start(GTK_BOX(mpeg_box), mpeg_samplerate,
				   FALSE, FALSE, 0);

		mpeg_flags = gtk_label_new("");
		gtk_misc_set_alignment(GTK_MISC(mpeg_flags), 0, 0);
		gtk_label_set_justify(GTK_LABEL(mpeg_flags), GTK_JUSTIFY_LEFT);
		gtk_box_pack_start(GTK_BOX(mpeg_box), mpeg_flags,
				   FALSE, FALSE, 0);

		mpeg_fileinfo = gtk_label_new("");
		gtk_misc_set_alignment(GTK_MISC(mpeg_fileinfo), 0, 0);
		gtk_label_set_justify(GTK_LABEL(mpeg_fileinfo),
				      GTK_JUSTIFY_LEFT);
		gtk_box_pack_start(GTK_BOX(mpeg_box), mpeg_fileinfo,
				   FALSE, FALSE, 0);

		gtk_widget_show_all(window);
	}

	if (current_filename)
		g_free(current_filename);
	current_filename = g_strdup(filename);

	title = g_strdup_printf(_("File Info - %s"), g_basename(filename));
	gtk_window_set_title(GTK_WINDOW(window), title);
	g_free(title);

	gtk_entry_set_text(GTK_ENTRY(filename_entry), filename);
	gtk_editable_set_position(GTK_EDITABLE(filename_entry), -1);

	title = g_strdup(g_basename(filename));
	if ((tmp = strrchr(title, '.')) != NULL)
		*tmp = '\0';
	gtk_entry_set_text(GTK_ENTRY(title_entry), title);
	g_free(title);

	gtk_entry_set_text(GTK_ENTRY(artist_entry), "");
	gtk_entry_set_text(GTK_ENTRY(album_entry), "");
	gtk_entry_set_text(GTK_ENTRY(year_entry), "");
	gtk_entry_set_text(GTK_ENTRY(tracknum_entry), "");
	gtk_entry_set_text(GTK_ENTRY(comment_entry), "");
	gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(genre_combo)->entry), "");
	for (i = 0; i < ID3V2_FIELDS; i++)
	{
		g_free(loaded[i]);
		loaded[i] = NULL;
	}
	gtk_label_set_text(GTK_LABEL(mpeg_level), "MPEG ?, layer ?");
	gtk_label_set_text(GTK_LABEL(mpeg_bitrate), "");
	gtk_label_set_text(GTK_LABEL(mpeg_samplerate), "");
	gtk_label_set_text(GTK_LABEL(mpeg_flags), "");
	gtk_label_set_text(GTK_LABEL(mpeg_fileinfo), "");

	if (http_stream_is_url(filename))
	{
		file_info_http(filename);
		return;
	}

	gtk_widget_set_sensitive(id3_frame, TRUE);
	current_riff = is_riff(current_filename);

	if ((fh = fopen(current_filename, "rb")) != NULL)
	{
		long filesize;

		fseek(fh, -sizeof (tag), SEEK_END);
		if (fread(&tag, 1, sizeof (tag), fh) == sizeof (tag))
		{
			if (!strncmp(tag.tag, "TAG", 3))
			{
				set_entry_tag(GTK_ENTRY(title_entry),
					      tag.title, 30);
				set_entry_tag(GTK_ENTRY(artist_entry),
					      tag.artist, 30);
				set_entry_tag(GTK_ENTRY(album_entry),
					      tag.album, 30);
				set_entry_tag(GTK_ENTRY(year_entry),
					      tag.year, 4);
				/* Check for v1.1 tags */
				if (tag.u.v1_1.__zero == 0 && tag.u.v1_1.track_number > 0)
				{
					char *temp = g_strdup_printf("%d", tag.u.v1_1.track_number);
					set_entry_tag(GTK_ENTRY(comment_entry),
						      tag.u.v1_1.comment, 28);
					gtk_entry_set_text(GTK_ENTRY(tracknum_entry), temp);
					g_free(temp);
				}
				else
				{
					set_entry_tag(GTK_ENTRY(comment_entry),
						      tag.u.v1_0.comment, 30);
					gtk_entry_set_text(GTK_ENTRY(tracknum_entry), "");
				}
				
				if (tag.genre < GENRE_MAX)
					gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(genre_combo)->entry),
							   gettext(mpg123_id3_genres[tag.genre]));
			}
		}
		if (!current_riff)
			show_id3v2();
		fseek(fh, 0, SEEK_END);
		filesize = ftell(fh);
		fclose(fh);
		show_mpeg_info(current_filename, filesize);
	}
}
