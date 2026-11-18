/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2000  Peter Alm, Mikael Alm, Olle Hallnas, Thomas Nilsson and 4Front Technologies
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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>

#ifdef HAVE_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

#include "libxmms/configfile.h"
#include "libxmms/dirbrowser.h"
#include "libxmms/util.h"
#include "xmms/i18n.h"

#include "httpstream.h"

#define MAX_REDIRECTS	5
#define MAX_LINE	1024

HttpStreamConfig http_stream_cfg;

/*
 * Settings
 */

static void config_free(void)
{
	g_free(http_stream_cfg.proxy_host);
	g_free(http_stream_cfg.proxy_user);
	g_free(http_stream_cfg.proxy_pass);
	g_free(http_stream_cfg.save_path);
	memset(&http_stream_cfg, 0, sizeof (http_stream_cfg));
}

void http_stream_config_load(void)
{
	HttpStreamConfig *c = &http_stream_cfg;
	ConfigFile *cfg;

	config_free();
	c->buffer_size = 128;
	c->prebuffer = 25;
	c->proxy_port = 8080;
	c->title_streaming = TRUE;

	cfg = xmms_cfg_open_default_file();
	xmms_cfg_read_int(cfg, "stream", "buffer_size", &c->buffer_size);
	xmms_cfg_read_int(cfg, "stream", "prebuffer", &c->prebuffer);
	xmms_cfg_read_boolean(cfg, "stream", "use_proxy", &c->use_proxy);
	xmms_cfg_read_string(cfg, "stream", "proxy_host", &c->proxy_host);
	xmms_cfg_read_int(cfg, "stream", "proxy_port", &c->proxy_port);
	xmms_cfg_read_boolean(cfg, "stream", "proxy_use_auth", &c->proxy_use_auth);
	xmms_cfg_read_string(cfg, "stream", "proxy_user", &c->proxy_user);
	xmms_cfg_read_string(cfg, "stream", "proxy_pass", &c->proxy_pass);
	xmms_cfg_read_boolean(cfg, "stream", "save_stream", &c->save_stream);
	xmms_cfg_read_string(cfg, "stream", "save_path", &c->save_path);
	xmms_cfg_read_boolean(cfg, "stream", "title_streaming", &c->title_streaming);
	xmms_cfg_free(cfg);

	c->buffer_size = CLAMP(c->buffer_size, 4, 4096);
	c->prebuffer = CLAMP(c->prebuffer, 0, 90);
	if (!c->proxy_host)
		c->proxy_host = g_strdup("localhost");
	if (!c->save_path || !*c->save_path)
	{
		g_free(c->save_path);
		c->save_path = g_strdup(g_get_home_dir());
	}
}

static void config_save(void)
{
	HttpStreamConfig *c = &http_stream_cfg;
	ConfigFile *cfg;

	cfg = xmms_cfg_open_default_file();
	xmms_cfg_write_int(cfg, "stream", "buffer_size", c->buffer_size);
	xmms_cfg_write_int(cfg, "stream", "prebuffer", c->prebuffer);
	xmms_cfg_write_boolean(cfg, "stream", "use_proxy", c->use_proxy);
	xmms_cfg_write_string(cfg, "stream", "proxy_host", c->proxy_host);
	xmms_cfg_write_int(cfg, "stream", "proxy_port", c->proxy_port);
	xmms_cfg_write_boolean(cfg, "stream", "proxy_use_auth", c->proxy_use_auth);
	if (c->proxy_user)
		xmms_cfg_write_string(cfg, "stream", "proxy_user", c->proxy_user);
	else
		xmms_cfg_remove_key(cfg, "stream", "proxy_user");
	if (c->proxy_pass)
		xmms_cfg_write_string(cfg, "stream", "proxy_pass", c->proxy_pass);
	else
		xmms_cfg_remove_key(cfg, "stream", "proxy_pass");
	xmms_cfg_write_boolean(cfg, "stream", "save_stream", c->save_stream);
	xmms_cfg_write_string(cfg, "stream", "save_path", c->save_path);
	xmms_cfg_write_boolean(cfg, "stream", "title_streaming", c->title_streaming);
	xmms_cfg_write_default_file(cfg);
	xmms_cfg_free(cfg);
}

/*
 * The settings page
 */

static GtkWidget *page, *size_spin, *pre_spin;
static GtkWidget *proxy_use, *proxy_hbox, *proxy_host_entry, *proxy_port_entry;
static GtkWidget *proxy_auth_use, *proxy_auth_hbox;
static GtkWidget *proxy_user_entry, *proxy_pass_entry;
static GtkWidget *save_use, *save_hbox, *save_entry, *save_browser;
static GtkWidget *title_use;

static void proxy_toggled(GtkWidget *w, gpointer data)
{
	gboolean use_proxy, use_auth;

	use_proxy = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(proxy_use));
	use_auth = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(proxy_auth_use));
	gtk_widget_set_sensitive(proxy_hbox, use_proxy);
	gtk_widget_set_sensitive(proxy_auth_use, use_proxy);
	gtk_widget_set_sensitive(proxy_auth_hbox, use_proxy && use_auth);
}

static void save_toggled(GtkWidget *w, gpointer data)
{
	gtk_widget_set_sensitive(save_hbox,
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(save_use)));
}

static void save_dir_chosen(gchar *dir)
{
	gtk_entry_set_text(GTK_ENTRY(save_entry), dir);
}

static void save_browse(GtkWidget *w, gpointer data)
{
	GtkWidget *toplevel;

	if (save_browser)
		return;
	save_browser = xmms_create_dir_browser(
		_("Select the directory where you want to store the streams:"),
		gtk_entry_get_text(GTK_ENTRY(save_entry)),
		GTK_SELECTION_SINGLE, save_dir_chosen);
	gtk_signal_connect(GTK_OBJECT(save_browser), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed), &save_browser);
	toplevel = gtk_widget_get_toplevel(page);
	if (GTK_IS_WINDOW(toplevel))
		gtk_window_set_transient_for(GTK_WINDOW(save_browser),
					     GTK_WINDOW(toplevel));
	gtk_widget_show(save_browser);
}

static GtkWidget *labelled(GtkWidget *box, const gchar *text, GtkWidget *w,
			   gboolean expand)
{
	gtk_box_pack_start(GTK_BOX(box), gtk_label_new(text), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), w, expand, expand, 0);
	return w;
}

static GtkWidget *framed(GtkWidget *vbox, const gchar *title, gboolean hbox)
{
	GtkWidget *frame, *box;

	frame = gtk_frame_new(title);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);
	box = hbox ? gtk_hbox_new(TRUE, 5) : gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(box), 5);
	gtk_container_add(GTK_CONTAINER(frame), box);
	return box;
}

GtkWidget *http_stream_config_page(void)
{
	HttpStreamConfig *c = &http_stream_cfg;
	GtkObject *adj;
	GtkWidget *box, *hbox, *browse;
	gchar *port;

	http_stream_config_load();

	page = gtk_vbox_new(FALSE, 0);
	gtk_signal_connect(GTK_OBJECT(page), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed), &page);

	/* Buffering */
	box = framed(page, _("Buffering:"), TRUE);
	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(box), hbox, TRUE, TRUE, 0);
	adj = gtk_adjustment_new(c->buffer_size, 4, 4096, 4, 4, 4);
	size_spin = labelled(hbox, _("Buffer size (kb):"),
			     gtk_spin_button_new(GTK_ADJUSTMENT(adj), 8, 0), FALSE);
	gtk_widget_set_usize(size_spin, 60, -1);
	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(box), hbox, TRUE, TRUE, 0);
	adj = gtk_adjustment_new(c->prebuffer, 0, 90, 1, 1, 1);
	pre_spin = labelled(hbox, _("Pre-buffer (percent):"),
			    gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0), FALSE);
	gtk_widget_set_usize(pre_spin, 60, -1);

	/* Proxy */
	box = framed(page, _("Proxy:"), FALSE);
	proxy_use = gtk_check_button_new_with_label(_("Use proxy"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(proxy_use), c->use_proxy);
	gtk_signal_connect(GTK_OBJECT(proxy_use), "clicked",
			   GTK_SIGNAL_FUNC(proxy_toggled), NULL);
	gtk_box_pack_start(GTK_BOX(box), proxy_use, FALSE, FALSE, 0);

	proxy_hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(box), proxy_hbox, FALSE, FALSE, 0);
	proxy_host_entry = labelled(proxy_hbox, _("Host:"), gtk_entry_new(), TRUE);
	gtk_entry_set_text(GTK_ENTRY(proxy_host_entry), c->proxy_host);
	proxy_port_entry = labelled(proxy_hbox, _("Port:"), gtk_entry_new(), FALSE);
	gtk_widget_set_usize(proxy_port_entry, 50, -1);
	port = g_strdup_printf("%d", c->proxy_port);
	gtk_entry_set_text(GTK_ENTRY(proxy_port_entry), port);
	g_free(port);

	proxy_auth_use = gtk_check_button_new_with_label(_("Use authentication"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(proxy_auth_use),
				     c->proxy_use_auth);
	gtk_signal_connect(GTK_OBJECT(proxy_auth_use), "clicked",
			   GTK_SIGNAL_FUNC(proxy_toggled), NULL);
	gtk_box_pack_start(GTK_BOX(box), proxy_auth_use, FALSE, FALSE, 0);

	proxy_auth_hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(box), proxy_auth_hbox, FALSE, FALSE, 0);
	proxy_user_entry = labelled(proxy_auth_hbox, _("Username:"),
				    gtk_entry_new(), TRUE);
	if (c->proxy_user)
		gtk_entry_set_text(GTK_ENTRY(proxy_user_entry), c->proxy_user);
	proxy_pass_entry = labelled(proxy_auth_hbox, _("Password:"),
				    gtk_entry_new(), TRUE);
	if (c->proxy_pass)
		gtk_entry_set_text(GTK_ENTRY(proxy_pass_entry), c->proxy_pass);
	gtk_entry_set_visibility(GTK_ENTRY(proxy_pass_entry), FALSE);
	proxy_toggled(NULL, NULL);

	/* Saving */
	box = framed(page, _("Save stream to disk:"), FALSE);
	save_use = gtk_check_button_new_with_label(_("Save stream to disk"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(save_use), c->save_stream);
	gtk_signal_connect(GTK_OBJECT(save_use), "clicked",
			   GTK_SIGNAL_FUNC(save_toggled), NULL);
	gtk_box_pack_start(GTK_BOX(box), save_use, FALSE, FALSE, 0);
	save_hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(box), save_hbox, FALSE, FALSE, 0);
	save_entry = labelled(save_hbox, _("Path:"), gtk_entry_new(), TRUE);
	gtk_entry_set_text(GTK_ENTRY(save_entry), c->save_path);
	browse = gtk_button_new_with_label(_("Browse"));
	gtk_signal_connect(GTK_OBJECT(browse), "clicked",
			   GTK_SIGNAL_FUNC(save_browse), NULL);
	gtk_box_pack_start(GTK_BOX(save_hbox), browse, FALSE, FALSE, 0);
	save_toggled(NULL, NULL);

	/* Titles */
	box = framed(page, _("SHOUT/Icecast:"), FALSE);
	title_use = gtk_check_button_new_with_label(
		_("Enable SHOUT/Icecast title streaming"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(title_use),
				     c->title_streaming);
	gtk_box_pack_start(GTK_BOX(box), title_use, FALSE, FALSE, 0);

	return page;
}

static gchar *entry_text_or_null(GtkWidget *entry)
{
	gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));

	return *text ? g_strdup(text) : NULL;
}

void http_stream_config_page_apply(void)
{
	HttpStreamConfig *c = &http_stream_cfg;

	if (!page)
		return;
	c->buffer_size = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(size_spin));
	c->prebuffer = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pre_spin));
	c->use_proxy = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(proxy_use));
	g_free(c->proxy_host);
	c->proxy_host = g_strdup(gtk_entry_get_text(GTK_ENTRY(proxy_host_entry)));
	c->proxy_port = atoi(gtk_entry_get_text(GTK_ENTRY(proxy_port_entry)));
	c->proxy_use_auth =
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(proxy_auth_use));
	g_free(c->proxy_user);
	c->proxy_user = entry_text_or_null(proxy_user_entry);
	g_free(c->proxy_pass);
	c->proxy_pass = entry_text_or_null(proxy_pass_entry);
	c->save_stream = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(save_use));
	g_free(c->save_path);
	c->save_path = g_strdup(gtk_entry_get_text(GTK_ENTRY(save_entry)));
	c->title_streaming =
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(title_use));
	config_save();
}

/*
 * The stream
 */

static const HttpStreamClient *client;
static gchar *stream_url;
static guint64 stream_offset;

static pthread_t thread;
static gboolean thread_running;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Written by the stream's thread, read by the plugin, under lock. */
static gboolean going, eof, prebuffering;
static gchar *buffer;
static gint buffer_length, prebuffer_length, rd_index, wr_index;

/* The stream's thread's own. */
static FILE *save_file;
static gint icy_metaint;

/* Set by the stream's thread, read by the plugin, under lock. */
static gchar *icy_name, *icy_title_now;

static GtkWidget *error_dialog;

static gboolean is_going(void)
{
	gboolean g;

	pthread_mutex_lock(&lock);
	g = going;
	pthread_mutex_unlock(&lock);
	return g;
}

static gint used_locked(void)
{
	if (wr_index >= rd_index)
		return wr_index - rd_index;
	return buffer_length - (rd_index - wr_index);
}

static void set_status(const gchar *text)
{
	if (client->ip && client->ip->set_info_text)
		client->ip->set_info_text((gchar *) text);
}

static void show_error(const gchar *text)
{
	GDK_THREADS_ENTER();
	if (!error_dialog)
	{
		error_dialog = xmms_show_message(_("Error"), (gchar *) text,
						 _("OK"), FALSE, NULL, NULL);
		gtk_signal_connect(GTK_OBJECT(error_dialog), "destroy",
				   GTK_SIGNAL_FUNC(gtk_widget_destroyed),
				   &error_dialog);
	}
	GDK_THREADS_LEAVE();
	set_status(NULL);
}

static gchar *base64_encode(const gchar *text)
{
	static const gchar tbl[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const guchar *s = (const guchar *) text;
	gsize len = strlen(text), i;
	gchar *out, *p;

	out = p = g_malloc(4 * ((len + 2) / 3) + 1);
	for (i = 0; i + 2 < len; i += 3)
	{
		*p++ = tbl[s[i] >> 2];
		*p++ = tbl[((s[i] & 3) << 4) | (s[i + 1] >> 4)];
		*p++ = tbl[((s[i + 1] & 0xf) << 2) | (s[i + 2] >> 6)];
		*p++ = tbl[s[i + 2] & 0x3f];
	}
	if (i < len)
	{
		*p++ = tbl[s[i] >> 2];
		if (i + 1 < len)
		{
			*p++ = tbl[((s[i] & 3) << 4) | (s[i + 1] >> 4)];
			*p++ = tbl[(s[i + 1] & 0xf) << 2];
		}
		else
		{
			*p++ = tbl[(s[i] & 3) << 4];
			*p++ = '=';
		}
		*p++ = '=';
	}
	*p = '\0';
	return out;
}

static gchar *basic_auth(const gchar *header, const gchar *user,
			 const gchar *pass)
{
	gchar *plain, *encoded, *line;

	plain = g_strconcat(user, ":", pass ? pass : "", NULL);
	encoded = base64_encode(plain);
	line = g_strdup_printf("%s: Basic %s\r\n", header, encoded);
	g_free(encoded);
	g_free(plain);
	return line;
}

typedef struct
{
	gchar *user, *pass, *host, *path;
	gint port;
	gboolean tls;
} Url;

static void url_free(Url *u)
{
	g_free(u->user);
	g_free(u->pass);
	g_free(u->host);
	g_free(u->path);
}

/* http[s]://[user[:pass]@]host[:port][/path], host possibly an [IPv6] one */
static gboolean url_parse(const gchar *url, Url *u)
{
	const gchar *p = url, *at, *slash, *hostend;

	memset(u, 0, sizeof (*u));
	if (!g_ascii_strncasecmp(p, "http://", 7))
		p += 7;
	else if (!g_ascii_strncasecmp(p, "https://", 8))
	{
		p += 8;
		u->tls = TRUE;
	}
	else
		return FALSE;
	slash = strchr(p, '/');
	if (!slash)
		slash = p + strlen(p);
	at = memchr(p, '@', slash - p);
	if (at)
	{
		const gchar *colon = memchr(p, ':', at - p);

		if (colon)
		{
			u->user = g_strndup(p, colon - p);
			u->pass = g_strndup(colon + 1, at - colon - 1);
		}
		else
			u->user = g_strndup(p, at - p);
		p = at + 1;
	}
	if (*p == '[')
	{
		hostend = memchr(p, ']', slash - p);
		if (!hostend)
			return FALSE;
		u->host = g_strndup(p + 1, hostend - p - 1);
		p = hostend + 1;
	}
	else
	{
		hostend = memchr(p, ':', slash - p);
		if (!hostend)
			hostend = slash;
		u->host = g_strndup(p, hostend - p);
		p = hostend;
	}
	u->port = u->tls ? 443 : 80;
	if (*p == ':')
		u->port = atoi(p + 1);
	u->path = g_strdup(*slash ? slash : "/");
	return *u->host && u->port > 0 && u->port < 65536;
}

static gboolean wait_fd(gint fd, gboolean for_write)
{
	fd_set set;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 20000;
	FD_ZERO(&set);
	FD_SET(fd, &set);
	return select(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL,
		      NULL, &tv) > 0;
}

/* A connection: a socket, with TLS over it for https. */
typedef struct
{
	gint fd;
#ifdef HAVE_TLS
	SSL *ssl;
#endif
	gboolean want_write;	/* what to wait for before trying again */
} Conn;

static Conn conn = { .fd = -1 };

static void conn_close(Conn *c)
{
#ifdef HAVE_TLS
	if (c->ssl)
	{
		SSL_shutdown(c->ssl);
		SSL_free(c->ssl);
		c->ssl = NULL;
	}
#endif
	if (c->fd >= 0)
		close(c->fd);
	c->fd = -1;
	c->want_write = FALSE;
}

/*
 * Wait a little for something to read. TLS reads whole records, so it
 * may hold data already that the socket will never signal.
 */
static gboolean conn_wait(Conn *c)
{
#ifdef HAVE_TLS
	if (c->ssl && SSL_pending(c->ssl) > 0)
		return TRUE;
#endif
	return wait_fd(c->fd, c->want_write);
}

#ifdef HAVE_TLS
/* After SSL_read() or SSL_write() made no progress: -2 to retry, or -1. */
static ssize_t tls_retry(Conn *c, gint r)
{
	switch (SSL_get_error(c->ssl, r))
	{
	case SSL_ERROR_WANT_READ:
		c->want_write = FALSE;
		return -2;
	case SSL_ERROR_WANT_WRITE:
		c->want_write = TRUE;
		return -2;
	case SSL_ERROR_ZERO_RETURN:
		return 0;
	case SSL_ERROR_SYSCALL:
		/* Servers often close without a TLS goodbye: the end all the same. */
		return r == 0 ? 0 : -1;
	default:
		return -1;
	}
}
#endif

/* Bytes read; 0 at the end, -1 on error, -2 if there are none yet. */
static ssize_t conn_read(Conn *c, gpointer buf, gsize len)
{
	ssize_t n;

#ifdef HAVE_TLS
	if (c->ssl)
	{
		gint r;

		ERR_clear_error();
		if ((r = SSL_read(c->ssl, buf, len)) > 0)
		{
			c->want_write = FALSE;
			return r;
		}
		return tls_retry(c, r);
	}
#endif
	c->want_write = FALSE;
	n = read(c->fd, buf, len);
	if (n < 0 && (errno == EAGAIN || errno == EINTR))
		return -2;
	return n;
}

/* Bytes written, or as conn_read(). */
static ssize_t conn_write(Conn *c, gconstpointer buf, gsize len)
{
	ssize_t n;

#ifdef HAVE_TLS
	if (c->ssl)
	{
		gint r;

		ERR_clear_error();
		if ((r = SSL_write(c->ssl, buf, len)) > 0)
			return r;
		return tls_retry(c, r) == -2 ? -2 : -1;
	}
#endif
	n = write(c->fd, buf, len);
	if (n < 0 && (errno == EAGAIN || errno == EINTR))
	{
		c->want_write = TRUE;
		return -2;
	}
	return n;
}

#ifdef HAVE_TLS
static gboolean is_ip_address(const gchar *host)
{
	guchar addr[16];

	return inet_pton(AF_INET, host, addr) == 1 ||
		inet_pton(AF_INET6, host, addr) == 1;
}

/* The TLS handshake, checking that the server's certificate is for host. */
static gboolean tls_start(Conn *c, const gchar *host)
{
	SSL_CTX *ctx;
	const gchar *why = NULL;
	gchar *status;
	long verify;

	status = g_strdup_printf(_("SECURING CONNECTION TO %s"), host);
	set_status(status);
	g_free(status);

	if ((ctx = SSL_CTX_new(TLS_client_method())) == NULL)
	{
		show_error(_("Couldn't set up TLS"));
		return FALSE;
	}
	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
	SSL_CTX_set_options(ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
#endif
	SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
	SSL_CTX_set_default_verify_paths(ctx);
	c->ssl = SSL_new(ctx);
	SSL_CTX_free(ctx);
	if (!c->ssl || !SSL_set_fd(c->ssl, c->fd))
	{
		show_error(_("Couldn't set up TLS"));
		return FALSE;
	}
	if (is_ip_address(host))
		X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(c->ssl), host);
	else
	{
		SSL_set_tlsext_host_name(c->ssl, host);
		SSL_set1_host(c->ssl, host);
	}

	while (is_going())
	{
		gint r, e;

		ERR_clear_error();
		if ((r = SSL_connect(c->ssl)) == 1)
			return TRUE;
		e = SSL_get_error(c->ssl, r);
		if (e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE)
			break;
		wait_fd(c->fd, e == SSL_ERROR_WANT_WRITE);
	}
	if (!is_going())
		return FALSE;

	if ((verify = SSL_get_verify_result(c->ssl)) != X509_V_OK)
		why = X509_verify_cert_error_string(verify);
	else if (ERR_peek_last_error())
		why = ERR_reason_error_string(ERR_peek_last_error());
	status = g_strdup_printf(_("Couldn't make a secure connection to %s\n%s"),
				 host, why ? why : _("The server closed the connection"));
	show_error(status);
	g_free(status);
	return FALSE;
}
#endif

static gint connect_to(const gchar *host, gint port)
{
	struct addrinfo hints, *res, *res0;
	gchar service[8], *status;
	gint fd = -1;

	status = g_strdup_printf(_("LOOKING UP %s"), host);
	set_status(status);
	g_free(status);

	g_snprintf(service, sizeof (service), "%d", port);
	memset(&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, service, &hints, &res0))
	{
		status = g_strdup_printf(_("Couldn't look up host %s"), host);
		show_error(status);
		g_free(status);
		return -1;
	}

	status = g_strdup_printf(_("CONNECTING TO %s:%d"), host, port);
	set_status(status);
	g_free(status);

	for (res = res0; res && fd < 0 && is_going(); res = res->ai_next)
	{
		gint error = 0;
		socklen_t len = sizeof (error);

		fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd < 0)
			continue;
		fcntl(fd, F_SETFL, O_NONBLOCK);
		if (connect(fd, res->ai_addr, res->ai_addrlen) < 0 &&
		    errno != EINPROGRESS)
			error = errno;
		else
		{
			while (is_going() && !wait_fd(fd, TRUE))
				;
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
				error = errno;
		}
		if (error || !is_going())
		{
			close(fd);
			fd = -1;
		}
	}
	freeaddrinfo(res0);

	if (fd < 0 && is_going())
	{
		status = g_strdup_printf(_("Couldn't connect to host %s"), host);
		show_error(status);
		g_free(status);
	}
	return fd;
}

static gboolean write_all(Conn *c, const gchar *data)
{
	gsize left = strlen(data);

	while (left > 0 && is_going())
	{
		ssize_t n = conn_write(c, data, left);

		if (n == -2)
		{
			wait_fd(c->fd, c->want_write);
			continue;
		}
		if (n <= 0)
			return FALSE;
		data += n;
		left -= n;
	}
	c->want_write = FALSE;
	return left == 0;
}

/* A header line without its CRLF; FALSE on error, end of stream or stop. */
static gboolean read_line(Conn *c, gchar *line, gsize size)
{
	gsize i = 0;

	while (is_going())
	{
		gchar ch;
		ssize_t n;

		if (!conn_wait(c))
			continue;
		if ((n = conn_read(c, &ch, 1)) == -2)
			continue;
		if (n <= 0)
			return FALSE;
		if (ch == '\n')
		{
			line[i] = '\0';
			return TRUE;
		}
		if (ch != '\r' && i < size - 1)
			line[i++] = ch;
	}
	return FALSE;
}

/* The status code from "HTTP/1.x 200 OK", or "ICY 200 OK" from SHOUTcast. */
static gint read_status(Conn *c, gchar *line, gsize size)
{
	gchar *sp;

	if (!read_line(c, line, size))
		return 0;
	sp = strchr(line, ' ');
	return sp ? atoi(sp + 1) : 0;
}

#ifdef HAVE_TLS
/* Through an HTTP proxy, https goes by a tunnel the proxy opens. */
static gboolean proxy_tunnel(Conn *c, const gchar *host, const gchar *authority,
			     const gchar *proxy_auth)
{
	gchar line[MAX_LINE], *request, *status;
	gboolean sent;
	gint code;

	request = g_strdup_printf("CONNECT %s HTTP/1.0\r\nHost: %s\r\n%s\r\n",
				  authority, authority, proxy_auth ? proxy_auth : "");
	sent = write_all(c, request);
	g_free(request);
	if (!sent)
		return FALSE;
	if ((code = read_status(c, line, sizeof (line))) != 200)
	{
		if (is_going())
		{
			status = g_strdup_printf(
				_("Couldn't connect to host %s\nProxy reported: %s"),
				host, code ? line : "");
			show_error(status);
			g_free(status);
		}
		return FALSE;
	}
	while (read_line(c, line, sizeof (line)) && *line)
		;
	return is_going();
}
#endif

/*
 * Connect, send the request and read the reply's headers, following
 * redirects, but not from https to http. Leaves conn at the body.
 */
static gboolean http_request(void)
{
	HttpStreamConfig *c = &http_stream_cfg;
	gchar *url = g_strdup(stream_url);
	gboolean was_tls = FALSE;
	gint redirects;

	for (redirects = 0; redirects <= MAX_REDIRECTS && is_going(); redirects++)
	{
		gchar line[MAX_LINE], *request, *auth = NULL, *proxy_auth = NULL;
		gchar *location = NULL, *authority, *hostport, *target, *status;
		gboolean via_proxy, sent;
		gint code;
		Url u;

		if (!url_parse(url, &u))
		{
			status = g_strdup_printf(_("Couldn't connect to host %s"), url);
			show_error(status);
			g_free(status);
			url_free(&u);
			break;
		}
		if (was_tls && !u.tls)
		{
			status = g_strdup_printf(
				_("Won't follow a secure stream's redirect to an insecure one:\n%s"),
				url);
			show_error(status);
			g_free(status);
			url_free(&u);
			break;
		}
		was_tls = u.tls;
#ifndef HAVE_TLS
		if (u.tls)
		{
			show_error(_("This XMMS was built without support for https streams"));
			url_free(&u);
			break;
		}
#endif

		conn.fd = c->use_proxy ? connect_to(c->proxy_host, c->proxy_port) :
			connect_to(u.host, u.port);
		if (conn.fd < 0)
		{
			url_free(&u);
			break;
		}

		if (strchr(u.host, ':'))
			authority = g_strdup_printf("[%s]:%d", u.host, u.port);
		else
			authority = g_strdup_printf("%s:%d", u.host, u.port);
		/* The Host header leaves out the scheme's usual port. */
		if (u.port != (u.tls ? 443 : 80))
			hostport = g_strdup(authority);
		else if (strchr(u.host, ':'))
			hostport = g_strdup_printf("[%s]", u.host);
		else
			hostport = g_strdup(u.host);

		if (u.user)
			auth = basic_auth("Authorization", u.user, u.pass);
		if (c->use_proxy && c->proxy_use_auth && c->proxy_user)
			proxy_auth = basic_auth("Proxy-Authorization",
						c->proxy_user, c->proxy_pass);
		/* A proxy is sent plain http requests; https goes through it. */
		via_proxy = c->use_proxy && !u.tls;

#ifdef HAVE_TLS
		if (u.tls && ((c->use_proxy &&
			       !proxy_tunnel(&conn, u.host, authority, proxy_auth)) ||
			      !tls_start(&conn, u.host)))
		{
			g_free(authority);
			g_free(hostport);
			g_free(auth);
			g_free(proxy_auth);
			url_free(&u);
			conn_close(&conn);
			break;
		}
#endif

		/* A proxy is given the whole URL, but not its credentials. */
		target = via_proxy ? g_strdup_printf("http://%s%s", hostport, u.path) :
			g_strdup(u.path);
		request = g_strdup_printf(
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"User-Agent: XMMS/%s\r\n"
			"%s%s%s",
			target, hostport, VERSION,
			c->title_streaming ? "Icy-MetaData: 1\r\n" : "",
			auth ? auth : "", via_proxy && proxy_auth ? proxy_auth : "");
		if (stream_offset)
		{
			gchar *with_range = g_strdup_printf(
				"%sRange: bytes=%" PRIu64 "-\r\n", request,
				(uint64_t) stream_offset);
			g_free(request);
			request = with_range;
		}
		g_free(target);
		g_free(authority);
		g_free(hostport);
		g_free(auth);
		g_free(proxy_auth);

		sent = write_all(&conn, request) && write_all(&conn, "\r\n");
		g_free(request);
		if (!sent)
		{
			url_free(&u);
			conn_close(&conn);
			break;
		}
		set_status(_("CONNECTED: WAITING FOR REPLY"));

		code = read_status(&conn, line, sizeof (line));
		if (code < 200 || code >= 400)
		{
			if (is_going())
			{
				status = g_strdup_printf(
					_("Couldn't connect to host %s\nServer reported: %s"),
					u.host, code ? line : "");
				show_error(status);
				g_free(status);
			}
			url_free(&u);
			conn_close(&conn);
			break;
		}

		while (read_line(&conn, line, sizeof (line)) && *line)
		{
			if (!g_ascii_strncasecmp(line, "icy-name:", 9))
			{
				pthread_mutex_lock(&lock);
				g_free(icy_name);
				icy_name = g_strdup(g_strstrip(line + 9));
				pthread_mutex_unlock(&lock);
			}
			else if (!g_ascii_strncasecmp(line, "icy-metaint:", 12))
				icy_metaint = atoi(line + 12);
			else if (!g_ascii_strncasecmp(line, "location:", 9))
			{
				g_free(location);
				location = g_strdup(g_strstrip(line + 9));
			}
		}
		/* A redirect may name only a path on the same server. */
		if (location && location[0] == '/')
		{
			const gchar *scheme = u.tls ? "https" : "http";
			gchar *absolute;

			if (strchr(u.host, ':'))
				absolute = g_strdup_printf("%s://[%s]:%d%s", scheme,
							   u.host, u.port, location);
			else
				absolute = g_strdup_printf("%s://%s:%d%s", scheme,
							   u.host, u.port, location);
			g_free(location);
			location = absolute;
		}
		url_free(&u);

		if (code >= 300)
		{
			conn_close(&conn);
			if (!location)
				break;
			g_free(url);
			url = location;
			icy_metaint = 0;
			continue;
		}
		g_free(location);
		g_free(url);
		return TRUE;
	}
	g_free(url);
	return FALSE;
}

static void save_open(void)
{
	HttpStreamConfig *c = &http_stream_cfg;
	gchar *name, *p, *path;
	gint i;

	name = http_stream_get_title(stream_url);
	for (p = name; *p; p++)
		if (*p == '/')
			*p = '_';
	/* "stream.ogg" is saved as "stream.ogg", not "stream.ogg.ogg". */
	p = name + strlen(name) - strlen(client->save_suffix);
	if (p > name && !g_ascii_strcasecmp(p, client->save_suffix))
		*p = '\0';
	path = g_strdup_printf("%s/%s%s", c->save_path, name, client->save_suffix);
	for (i = 1; !access(path, F_OK) && i < 100000; i++)
	{
		g_free(path);
		path = g_strdup_printf("%s/%s-%d%s", c->save_path, name, i,
				       client->save_suffix);
	}
	save_file = fopen(path, "wb");
	g_free(path);
	g_free(name);
}

/* Queue audio for the plugin, waiting for room; FALSE once stopped. */
static gboolean push(const gchar *data, gint len)
{
	if (save_file && fwrite(data, 1, len, save_file) != (size_t) len)
	{
		fclose(save_file);
		save_file = NULL;
	}

	while (len > 0)
	{
		gint room, n;

		pthread_mutex_lock(&lock);
		if (!going)
		{
			pthread_mutex_unlock(&lock);
			return FALSE;
		}
		room = buffer_length - used_locked() - 1;
		n = MIN(len, MIN(room, buffer_length - wr_index));
		if (n > 0)
		{
			memcpy(buffer + wr_index, data, n);
			wr_index = (wr_index + n) % buffer_length;
		}
		if (prebuffering && used_locked() >= prebuffer_length)
			prebuffering = FALSE;
		pthread_mutex_unlock(&lock);

		if (n <= 0)
		{
			xmms_usleep(10000);
			continue;
		}
		data += n;
		len -= n;
	}
	return TRUE;
}

static void icy_title(const gchar *meta)
{
	const gchar *start = strstr(meta, "StreamTitle='"), *end;
	gchar *title, *name, *full;

	if (!start)
		return;
	start += 13;
	end = strstr(start, "';");
	if (!end)
		end = start + strlen(start);
	title = g_strndup(start, end - start);
	pthread_mutex_lock(&lock);
	name = g_strdup(icy_name);
	pthread_mutex_unlock(&lock);
	if (name && *name)
		full = g_strdup_printf("%s (%s)", title, name);
	else
		full = g_strdup(title);
	pthread_mutex_lock(&lock);
	g_free(icy_title_now);
	icy_title_now = g_strdup(full);
	pthread_mutex_unlock(&lock);
	if (client->title_changed)
		client->title_changed(full);
	g_free(full);
	g_free(name);
	g_free(title);
}

static void *stream_loop(void *arg)
{
	gchar data[4096], meta[16 * 255 + 1];
	gint until_meta, meta_left = 0, meta_len = 0;
	gboolean in_meta = FALSE;

	if (http_request())
	{
		if (http_stream_cfg.save_stream)
			save_open();

		until_meta = icy_metaint;
		while (is_going())
		{
			gchar *status;
			ssize_t n;
			gint i = 0;

			pthread_mutex_lock(&lock);
			status = prebuffering ?
				g_strdup_printf(_("PRE-BUFFERING: %dKB/%dKB"),
						used_locked() / 1024,
						prebuffer_length / 1024) : NULL;
			pthread_mutex_unlock(&lock);
			if (status)
			{
				set_status(status);
				g_free(status);
			}

			if (!conn_wait(&conn))
				continue;
			if ((n = conn_read(&conn, data, sizeof (data))) == -2)
				continue;
			if (n <= 0)
				break;

			/* Split out the ICY metadata every icy_metaint bytes. */
			while (i < n)
			{
				if (in_meta)
				{
					gint k = MIN(meta_left, n - i);

					memcpy(meta + meta_len, data + i, k);
					meta_len += k;
					meta_left -= k;
					i += k;
					if (!meta_left)
					{
						meta[meta_len] = '\0';
						icy_title(meta);
						in_meta = FALSE;
						until_meta = icy_metaint;
					}
				}
				else if (icy_metaint > 0 && until_meta == 0)
				{
					meta_left = (guchar) data[i++] * 16;
					meta_len = 0;
					if (meta_left)
						in_meta = TRUE;
					else
						until_meta = icy_metaint;
				}
				else
				{
					gint k = n - i;

					if (icy_metaint > 0)
						k = MIN(k, until_meta);
					if (!push(data + i, k))
						break;
					i += k;
					if (icy_metaint > 0)
						until_meta -= k;
				}
			}
		}
		conn_close(&conn);
		if (save_file)
		{
			fclose(save_file);
			save_file = NULL;
		}
	}

	pthread_mutex_lock(&lock);
	eof = TRUE;
	prebuffering = FALSE;
	pthread_mutex_unlock(&lock);
	set_status(NULL);
	return NULL;
}

gboolean http_stream_is_url(const gchar *name)
{
	return !g_ascii_strncasecmp(name, "http://", 7) ||
		!g_ascii_strncasecmp(name, "https://", 8);
}

gboolean http_stream_open(const HttpStreamClient *c, const gchar *url,
			  guint64 offset)
{
	http_stream_close();
	http_stream_config_load();

	client = c;
	stream_url = g_strdup(url);
	stream_offset = offset;
	icy_metaint = 0;

	buffer_length = http_stream_cfg.buffer_size * 1024;
	prebuffer_length = buffer_length * http_stream_cfg.prebuffer / 100;
	buffer = g_malloc(buffer_length);
	rd_index = wr_index = 0;
	going = TRUE;
	eof = FALSE;
	prebuffering = prebuffer_length > 0;

	thread_running = !pthread_create(&thread, NULL, stream_loop, NULL);
	if (!thread_running)
	{
		going = FALSE;
		eof = TRUE;
	}
	return thread_running;
}

gint http_stream_read(gpointer data, gint length)
{
	gint off = 0;

	for (;;)
	{
		pthread_mutex_lock(&lock);
		if (!going || (!prebuffering && used_locked() > 0) ||
		    (eof && !used_locked()))
			break;
		pthread_mutex_unlock(&lock);
		xmms_usleep(10000);
	}
	/* The lock is held here. */
	while (going && off < length && used_locked() > 0)
	{
		gint n = MIN(length - off,
			     MIN(used_locked(), buffer_length - rd_index));

		memcpy((gchar *) data + off, buffer + rd_index, n);
		rd_index = (rd_index + n) % buffer_length;
		off += n;
	}
	pthread_mutex_unlock(&lock);
	return off;
}

void http_stream_close(void)
{
	pthread_mutex_lock(&lock);
	going = FALSE;
	pthread_mutex_unlock(&lock);

	if (thread_running)
	{
		pthread_join(thread, NULL);
		thread_running = FALSE;
	}

	pthread_mutex_lock(&lock);
	g_free(buffer);
	buffer = NULL;
	buffer_length = rd_index = wr_index = 0;
	g_free(icy_name);
	icy_name = NULL;
	g_free(icy_title_now);
	icy_title_now = NULL;
	pthread_mutex_unlock(&lock);
	g_free(stream_url);
	stream_url = NULL;
}

gchar *http_stream_get_title(const gchar *url)
{
	const gchar *name;
	gchar *title = NULL;

	pthread_mutex_lock(&lock);
	if (stream_url && !strcmp(url, stream_url))
	{
		if (icy_title_now)
			title = g_strdup(icy_title_now);
		else if (icy_name && *icy_name)
			title = g_strdup(icy_name);
	}
	pthread_mutex_unlock(&lock);
	if (title)
		return title;

	name = strrchr(url, '/');
	name = name && name[1] ? name + 1 : url;
	if (http_stream_is_url(name))
		name = strstr(name, "://") + 3;
	return g_strdup(name);
}
