/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2000  Peter Alm, Mikael Alm, Olle Hallnas, Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 2026  The CGTK authors
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

/*
 *  The analyzer draws in a GtkGLArea in a window of GTK's, from a
 *  timeout in the thread GTK runs in: whatever window system GTK is
 *  on is the one it is on. It used to open the X display a second
 *  time and draw with GLX from a thread of its own.
 *
 *  A frame is drawn for every refresh of the screen: each swap waits
 *  for the next, and the picture turns by what it turns in the time
 *  the screen shows a frame for. Drawn at a rate of its own, however
 *  near the screen's, every so often a frame would be shown twice or
 *  not at all, and the picture would be seen to jerk.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gtkgl/gtkglarea.h>
#include <math.h>
#include <time.h>

#include <GL/gl.h>

#include "xmms/plugin.h"
#include "libxmms/xmmsctrl.h"
#include "libxmms/util.h"
#include "xmms/i18n.h"

#define NUM_BANDS 16

static GtkWidget *window = NULL, *area = NULL;
static guint draw_timeout = 0;
/* The speeds are in degrees for every sixtieth of a second. */
static GLfloat y_angle = 45.0, y_speed = 0.5;
static GLfloat x_angle = 20.0, x_speed = 0.0;
static GLfloat z_angle = 0.0, z_speed = 0.0;
/* When the last frame was begun, in seconds, and how far the picture
 * is behind the clock; how many frames in a row have come sooner than
 * the screen shows them, whether the clock has been given the pacing
 * of them, and when it lets the next go: see draw_timeout_func. */
static gdouble frame_begun = 0.0, frame_behind = 0.0;
static gint frames_early = 0;
static gboolean frames_clocked = FALSE;
static gdouble frame_due = 0.0;
static GLfloat heights[16][16], scale;

static void oglspectrum_init(void);
static void oglspectrum_cleanup(void);
static void oglspectrum_render_freq(gint16 data[2][256]);

VisPlugin oglspectrum_vp =
{
	NULL,
	NULL,
	0,
	NULL, /* Description */
	0,
	1,
	oglspectrum_init, /* init */
	oglspectrum_cleanup, /* cleanup */
	NULL, /* about */
	NULL, /* configure */
	NULL, /* disable_plugin */
	NULL, /* playback_start */
	NULL, /* playback_stop */
	NULL, /* render_pcm */
	oglspectrum_render_freq  /* render_freq */
};

VisPlugin *get_vplugin_info(void)
{
	oglspectrum_vp.description =
		g_strdup_printf(_("OpenGL Spectrum analyzer %s"), VERSION);
	return &oglspectrum_vp;
}

static void draw_rectangle(GLfloat x1, GLfloat y1, GLfloat z1, GLfloat x2, GLfloat y2, GLfloat z2)
{
	if(y1 == y2)
	{

		glVertex3f(x1, y1, z1);
		glVertex3f(x2, y1, z1);
		glVertex3f(x2, y2, z2);

		glVertex3f(x2, y2, z2);
		glVertex3f(x1, y2, z2);
		glVertex3f(x1, y1, z1);
	}
	else
	{
		glVertex3f(x1, y1, z1);
		glVertex3f(x2, y1, z2);
		glVertex3f(x2, y2, z2);

		glVertex3f(x2, y2, z2);
		glVertex3f(x1, y2, z1);
		glVertex3f(x1, y1, z1);
	}
}

static void draw_bar(GLfloat x_offset, GLfloat z_offset, GLfloat height, GLfloat red, GLfloat green, GLfloat blue )
{
	GLfloat width = 0.1;

	glColor3f(red,green,blue);
	draw_rectangle(x_offset, height, z_offset, x_offset + width, height, z_offset + 0.1);
	draw_rectangle(x_offset, 0, z_offset, x_offset + width, 0, z_offset + 0.1);

	glColor3f(0.5 * red, 0.5 * green, 0.5 * blue);
	draw_rectangle(x_offset, 0.0, z_offset + 0.1, x_offset + width, height, z_offset + 0.1);
	draw_rectangle(x_offset, 0.0, z_offset, x_offset + width, height, z_offset );

	glColor3f(0.25 * red, 0.25 * green, 0.25 * blue);
	draw_rectangle(x_offset, 0.0, z_offset , x_offset, height, z_offset + 0.1);
	draw_rectangle(x_offset + width, 0.0, z_offset , x_offset + width, height, z_offset + 0.1);


}

static void draw_bars(void)
{
	gint x,y;
	GLfloat x_offset, z_offset, r_base, b_base;

	if (!gtk_gl_area_make_current(GTK_GL_AREA(area)))
		return;

	glClearColor(0,0,0,0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glPushMatrix();
	glTranslatef(0.0,-0.5,-5.0);
	glRotatef(x_angle,1.0,0.0,0.0);
	glRotatef(y_angle,0.0,1.0,0.0);
	glRotatef(z_angle,0.0,0.0,1.0);

	glBegin(GL_TRIANGLES);
	for(y = 0; y < 16; y++)
	{
		z_offset = -1.6 + ((15 - y) * 0.2);

		b_base = y * (1.0 / 15);
		r_base = 1.0 - b_base;

		for(x = 0; x < 16; x++)
		{
			x_offset = -1.6 + (x * 0.2);

			draw_bar(x_offset, z_offset, heights[y][x], r_base - (x * (r_base / 15.0)), x * (1.0 / 15), b_base);
		}
	}
	glEnd();

	glPopMatrix();

	gtk_gl_area_swap_buffers(GTK_GL_AREA(area));
}

static void reset_view(void)
{
	x_speed = 0.0;
	y_speed = 0.5;
	z_speed = 0.0;
	x_angle = 20.0;
	y_angle = 45.0;
	z_angle = 0.0;
}

static void realize_cb(GtkWidget *widget, gpointer data)
{
	if (!gtk_gl_area_make_current(GTK_GL_AREA(widget)))
		return;

	/*
	 * Each frame waits for the screen's next refresh. The wait is
	 * spent in the thread the rest of XMMS runs in, and is never
	 * longer than the frames are apart.
	 */
	gdk_gl_set_swap_interval(widget->window, 1);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-1, 1, -1, 1, 1.5, 10);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
}

static gint configure_cb(GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
	gint width, height;

	if (gtk_gl_area_make_current(GTK_GL_AREA(widget)))
	{
		/* In the pixels GL draws, which a scaled display has more of. */
		gtk_gl_area_get_drawable_size(GTK_GL_AREA(widget), &width, &height);
		glViewport(0, 0, width, height);
	}
	return TRUE;
}

/*
 * Nothing is drawn for an expose: the next frame is never more than a
 * refresh away, and a frame drawn in between would wait for a refresh
 * of its own, one for every expose of a window being resized.
 */
static gint expose_cb(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	return TRUE;
}

static gdouble frame_time(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0.0;
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* How long the screen shows a frame for, in seconds: a sixtieth of
 * one if nothing says. */
static gdouble frame_period(void)
{
	gint rate = 0;

	if (area && GTK_WIDGET_REALIZED(area))
		rate = gdk_window_get_refresh_rate(area->window);
	return rate > 0 ? 1000.0 / rate : 1.0 / 60.0;
}

static GLfloat turned(GLfloat angle, GLfloat by)
{
	angle = fmod(angle + by, 360.0);
	return angle < 0.0 ? angle + 360.0 : angle;
}

/*
 * A frame is drawn, and the next is asked for only when this one is
 * done: however long a frame takes, the rest of XMMS has the time in
 * between, and whoever runs the main loop until nothing is left to do
 * finds an end to it.
 *
 * The frame waits for the screen in its swap, so the next can be asked
 * for almost at once: a few milliseconds on, which is the end that
 * whoever runs the main loop finds. Where nothing makes a swap wait --
 * GL drawn by software on X, or a window that is not shown -- frames
 * would follow each other as fast as they can be drawn; when four in a
 * row have come much sooner than the screen can have shown them, the
 * clock lets them go, one for each of the screen's, until a frame
 * takes that long again by itself.
 */
static gint draw_timeout_func(gpointer data)
{
	gdouble begun, period, since, now, wait;

	GDK_THREADS_ENTER();
	begun = frame_time();
	period = frame_period();
	since = frame_begun > 0.0 ? begun - frame_begun : period;
	frame_begun = begun;

	if (!frames_clocked)
	{
		frames_early = since < period * 0.75 ? frames_early + 1 : 0;
		if (frames_early >= 4)
		{
			frames_clocked = TRUE;
			frame_due = begun;
		}
	}

	if (area && GTK_WIDGET_DRAWABLE(area))
	{
		gdouble step;

		/*
		 * The picture turns by what it turns in the time the screen
		 * shows a frame for; a little more when it is behind the
		 * clock and a little less when it is ahead, so that it keeps
		 * the clock's time whatever the frames really come at. The
		 * moment a frame is begun at says little of when it is seen:
		 * a picture turned by the time since the last was begun
		 * would be shown at even intervals and turn unevenly. After
		 * a pause it goes on from where it was.
		 */
		if (since > 0.25)
		{
			since = period;
			frame_behind = 0.0;
		}
		step = MAX(period + frame_behind / 32.0, 0.0);
		frame_behind += since - step;

		x_angle = turned(x_angle, x_speed * step * 60.0);
		y_angle = turned(y_angle, y_speed * step * 60.0);
		z_angle = turned(z_angle, z_speed * step * 60.0);

		draw_bars();
	}
	else
		frame_behind = 0.0;

	now = frame_time();
	if (frames_clocked && now - begun >= period * 0.75)
	{
		frames_clocked = FALSE;
		frames_early = 0;
	}
	if (frames_clocked)
	{
		frame_due += period;
		if (frame_due < now - period)
			frame_due = now;
		wait = (frame_due - now) * 1000.0;
	}
	else
		wait = period * 250.0;
	draw_timeout = gtk_timeout_add((guint32) CLAMP(wait, 1.0, 100.0),
				       draw_timeout_func, NULL);
	GDK_THREADS_LEAVE();

	return FALSE;
}

/*
 * The keys the main window has, sent the way anything outside XMMS
 * would send them. The thread that answers on the control socket
 * takes the GDK lock for some of what it is asked, and answers one
 * asker at a time: the lock is let go of while it is asked, or it
 * could be waiting for the lock while this waits for its answer.
 */
static void remote_command(guint keyval)
{
	gint session = oglspectrum_vp.xmms_session;

	GDK_THREADS_LEAVE();
	switch (keyval)
	{
		case GDK_z:
			xmms_remote_playlist_prev(session);
			break;
		case GDK_x:
			xmms_remote_play(session);
			break;
		case GDK_c:
			xmms_remote_pause(session);
			break;
		case GDK_v:
			xmms_remote_stop(session);
			break;
		case GDK_b:
			xmms_remote_playlist_next(session);
			break;
	}
	GDK_THREADS_ENTER();
}

static gint key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	switch (event->keyval)
	{
		case GDK_Escape:
			gtk_widget_destroy(window);
			break;
		case GDK_z:
		case GDK_x:
		case GDK_c:
		case GDK_v:
		case GDK_b:
			remote_command(event->keyval);
			break;
		case GDK_Up:
			x_speed -= 0.1;
			if(x_speed < -3.0)
				x_speed = -3.0;
			break;
		case GDK_Down:
			x_speed += 0.1;
			if(x_speed > 3.0)
				x_speed = 3.0;
			break;
		case GDK_Left:
			y_speed -= 0.1;
			if(y_speed < -3.0)
				y_speed = -3.0;
			break;
		case GDK_Right:
			y_speed += 0.1;
			if(y_speed > 3.0)
				y_speed = 3.0;
			break;
		case GDK_w:
			z_speed -= 0.1;
			if(z_speed < -3.0)
				z_speed = -3.0;
			break;
		case GDK_q:
			z_speed += 0.1;
			if(z_speed > 3.0)
				z_speed = 3.0;
			break;
		case GDK_Return:
			reset_view();
			break;
		default:
			return FALSE;
	}
	return TRUE;
}

static void destroy_cb(GtkWidget *widget, gpointer data)
{
	oglspectrum_vp.disable_plugin(&oglspectrum_vp);
}

static void oglspectrum_init(void)
{
	int attr_list[] = {
		GDK_GL_RGBA,
		GDK_GL_DEPTH_SIZE, 16,
		GDK_GL_DOUBLEBUFFER,
		GDK_GL_NONE
	};
	int x, y;

	if (window)
		return;

	for(x = 0; x < 16; x++)
	{
		for(y = 0; y < 16; y++)
		{
			heights[y][x] = 0.0;
		}
	}
	scale = 1.0 / log(256.0);
	reset_view();

	if (!gdk_gl_query() || (area = gtk_gl_area_new(attr_list)) == NULL)
	{
		g_log(NULL, G_LOG_LEVEL_CRITICAL, __FILE__ ": unable to create window");
		area = NULL;
		return;
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), _("OpenGL Spectrum analyzer"));
	gtk_window_set_policy(GTK_WINDOW(window), TRUE, TRUE, FALSE);
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);
	gtk_signal_connect(GTK_OBJECT(window), "destroy", GTK_SIGNAL_FUNC(destroy_cb), NULL);
	gtk_signal_connect(GTK_OBJECT(window), "destroy", GTK_SIGNAL_FUNC(gtk_widget_destroyed), &window);
	gtk_signal_connect(GTK_OBJECT(window), "key_press_event", GTK_SIGNAL_FUNC(key_press_cb), NULL);

	gtk_signal_connect(GTK_OBJECT(area), "destroy", GTK_SIGNAL_FUNC(gtk_widget_destroyed), &area);
	gtk_signal_connect(GTK_OBJECT(area), "realize", GTK_SIGNAL_FUNC(realize_cb), NULL);
	gtk_signal_connect(GTK_OBJECT(area), "configure_event", GTK_SIGNAL_FUNC(configure_cb), NULL);
	gtk_signal_connect(GTK_OBJECT(area), "expose_event", GTK_SIGNAL_FUNC(expose_cb), NULL);
	gtk_container_add(GTK_CONTAINER(window), area);

	gtk_widget_show(area);
	gtk_widget_show(window);

	frame_begun = frame_behind = 0.0;
	frames_early = 0;
	frames_clocked = FALSE;
	draw_timeout = gtk_timeout_add(1, draw_timeout_func, NULL);
}

static void oglspectrum_cleanup(void)
{
	if (draw_timeout)
	{
		gtk_timeout_remove(draw_timeout);
		draw_timeout = 0;
	}
	if (window)
		gtk_widget_destroy(window);
}

static void oglspectrum_render_freq(gint16 data[2][256])
{
	gint i,c;
	gint y;
	GLfloat val;

	gint xscale[] = {0, 1, 2, 3, 5, 7, 10, 14, 20, 28, 40, 54, 74, 101, 137, 187, 255};

	for(y = 15; y > 0; y--)
	{
		for(i = 0; i < 16; i++)
		{
			heights[y][i] = heights[y - 1][i];
		}
	}

	for(i = 0; i < NUM_BANDS; i++)
	{
		for(c = xscale[i], y = 0; c < xscale[i + 1]; c++)
		{
			if(data[0][c] > y)
				y = data[0][c];
		}
		y >>= 7;
		if(y > 0)
			val = (log(y) * scale);
		else
			val = 0;


		heights[0][i] = val;


	}
}

