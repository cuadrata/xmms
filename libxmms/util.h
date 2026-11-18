#ifndef XMMS_UTIL_H
#define XMMS_UTIL_H
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

GtkWidget *xmms_show_message(gchar * title, gchar * text, gchar * button_text, gboolean modal, GtkSignalFunc button_action, gpointer action_data);
void xmms_usleep(gint usec);
gint xmms_collate(const gchar *a, const gchar *b);

/*
 * XMMS's directories, by the XDG Base Directory Specification, newly
 * allocated: $XDG_CONFIG_HOME/xmms (~/.config/xmms) holds the config
 * file, the playlist, the equalizer presets and the rc files;
 * $XDG_DATA_HOME/xmms (~/.local/share/xmms) holds Skins and Plugins.
 * An XDG variable that is not an absolute path is ignored, as the
 * specification says. Neither directory is made here.
 */
gchar *xmms_get_config_dir(void);
gchar *xmms_get_data_dir(void);
/* A file in the config directory: xmms_get_config_file("config"). */
gchar *xmms_get_config_file(const gchar *name);

/*
 * A value two threads share, read and set under one lock: an input
 * plugin's "playing" or "seek to", set by XMMS's thread when it stops or
 * seeks, and read by the plugin's decode thread.
 */
gint xmms_shared_get(const gint *var);
void xmms_shared_set(gint *var, gint value);

#ifdef __cplusplus
}
#endif
	
#endif
