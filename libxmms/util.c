#if defined(HAVE_CONFIG_H)
#include "config.h"
#include "util.h"
#endif

#include <gtk/gtk.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include <sys/time.h>
#include <time.h>

/* $VAR/xmms when VAR is an absolute path, else ~/FALLBACK/xmms. */
static gchar *xdg_dir(const gchar *var, const gchar *fallback)
{
	const gchar *v = g_getenv(var);

	if (v && v[0] == '/')
		return g_strconcat(v, "/xmms", NULL);
	return g_strconcat(g_get_home_dir(), "/", fallback, "/xmms", NULL);
}

gchar *xmms_get_config_dir(void)
{
	return xdg_dir("XDG_CONFIG_HOME", ".config");
}

gchar *xmms_get_data_dir(void)
{
	return xdg_dir("XDG_DATA_HOME", ".local/share");
}

gchar *xmms_get_config_file(const gchar *name)
{
	gchar *dir = xmms_get_config_dir();
	gchar *file = g_strconcat(dir, "/", name, NULL);

	g_free(dir);
	return file;
}



GtkWidget *xmms_show_message(gchar * title, gchar * text, gchar * button_text, gboolean modal, GtkSignalFunc button_action, gpointer action_data)
{
	GtkWidget *dialog, *vbox, *label, *bbox, *button;

	dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_window_set_modal(GTK_WINDOW(dialog), modal);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 15);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), vbox, TRUE, TRUE, 0);

	label = gtk_label_new(text);
	gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 0);
	gtk_widget_show(label);
	gtk_widget_show(vbox);

	bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_SPREAD);
	gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area), bbox, FALSE, FALSE, 0);

	button = gtk_button_new_with_label(button_text);
	if (button_action)
		gtk_signal_connect(GTK_OBJECT(button), "clicked", button_action, action_data);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked", GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(dialog));
	gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_widget_grab_default(button);
	gtk_widget_show(button);

	gtk_widget_show(bbox);
	gtk_widget_show(dialog);

	return dialog;
}

void xmms_usleep(gint usec)
{
	struct timespec req;

	req.tv_sec = usec / 1000000;
	usec -= req.tv_sec * 1000000;
	req.tv_nsec = usec * 1000;

	nanosleep(&req, NULL);
}

static wchar_t *collate_key(const gchar *str)
{
	size_t len, i;
	wchar_t *key;

	len = mbstowcs(NULL, str, 0);
	if (len == (size_t) -1)
		return NULL;
	key = g_new(wchar_t, len + 1);
	mbstowcs(key, str, len + 1);
	for (i = 0; i < len; i++)
		key[i] = towlower(key[i]);
	return key;
}

/*
 * Order two strings for people to read: without regard to case, and
 * by the collation of the locale's LC_COLLATE. A string that is not
 * valid in the locale's character set falls back to ASCII rules.
 * Strings that collate the same are ordered by their bytes, so the
 * order is total and a sort comes out the same every time.
 */
gint xmms_collate(const gchar *a, const gchar *b)
{
	wchar_t *ka, *kb;
	gint ret;

	g_return_val_if_fail(a != NULL, 0);
	g_return_val_if_fail(b != NULL, 0);

	ka = collate_key(a);
	kb = collate_key(b);
	if (ka && kb)
		ret = wcscoll(ka, kb);
	else
		ret = g_ascii_strcasecmp(a, b);
	g_free(ka);
	g_free(kb);

	if (ret == 0)
		ret = strcmp(a, b);
	return ret;
}

static pthread_mutex_t shared_lock = PTHREAD_MUTEX_INITIALIZER;

gint xmms_shared_get(const gint *var)
{
	gint value;

	pthread_mutex_lock(&shared_lock);
	value = *var;
	pthread_mutex_unlock(&shared_lock);
	return value;
}

void xmms_shared_set(gint *var, gint value)
{
	pthread_mutex_lock(&shared_lock);
	*var = value;
	pthread_mutex_unlock(&shared_lock);
}
