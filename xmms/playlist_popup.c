/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2001  Peter Alm, Mikael Alm, Olle Hallnas,
 *                           Thomas Nilsson and 4Front Technologies
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

/*
 * The menus behind the playlist's bottom buttons: held down, dragged
 * to an item and released, as in Winamp 2, with a quick click running
 * the button's own item.
 *
 * The menu is a child window of the playlist, not a window of its own:
 * it lies within the playlist, and a new window appearing under the
 * pressed pointer is a change of focus to a compositor, and some send
 * a release of their own at that, which chose the item at once. In
 * the playlist's own window nothing changes hands, and the release is
 * the user's.
 */

typedef struct
{
	GdkWindow *window;   /* in the playlist window */
	gint x, y;           /* where in it */
	GdkGC *gc;
	gint num_items;
	gint *nx, *ny;
	gint *sx, *sy;
	gint barx, bary;
	gint active, base;
	void (*handler) (gint item);
}
PlaylistPopup;

static PlaylistPopup *popup = NULL;

static void playlist_popup_draw(PlaylistPopup * pp)
{
	gint i;

	skin_draw_pixmap(pp->window, pp->gc, SKIN_PLEDIT,
			 pp->barx, pp->bary, 0, 0, 3, pp->num_items * 18);
	for (i = 0; i < pp->num_items; i++)
	{
		if (i == pp->active)
			skin_draw_pixmap(pp->window, pp->gc,
					 SKIN_PLEDIT, pp->sx[i], pp->sy[i],
					 3, i * 18, 22, 18);
		else
			skin_draw_pixmap(pp->window, pp->gc,
					 SKIN_PLEDIT, pp->nx[i], pp->ny[i],
					 3, i * 18, 22, 18);
	}
}

void playlist_popup_destroy(void)
{
	PlaylistPopup *p = popup;

	if (!p)
		return;
	popup = NULL;

	gdk_pointer_ungrab(GDK_CURRENT_TIME);
	gdk_window_set_user_data(p->window, NULL);
	gdk_window_destroy(p->window);
	gdk_gc_unref(p->gc);
	g_free(p->nx);
	g_free(p->ny);
	g_free(p->sx);
	g_free(p->sy);
	if (p->handler && p->active != -1)
		p->handler(p->active + p->base);
	g_free(p);
}

static void playlist_popup_set_active(gint x, gint y)
{
	gint active;

	if (x >= 0 && x < 25 && y >= 0 && y < popup->num_items * 18)
		active = y / 18;
	else
		active = -1;
	if (popup->active != active)
	{
		popup->active = active;
		playlist_popup_draw(popup);
	}
}

/*
 * The playlist window's event handlers offer the menu their events
 * first. With the pointer grabbed to the menu they come in its own
 * coordinates; before the grab takes, in the playlist's.
 */
gboolean playlist_popup_event(GdkEvent * event)
{
	gint x, y;

	if (!popup)
		return FALSE;

	switch (event->type)
	{
		case GDK_EXPOSE:
			if (event->any.window != popup->window)
				return FALSE;
			playlist_popup_draw(popup);
			return TRUE;
		case GDK_MOTION_NOTIFY:
			x = event->motion.x;
			y = event->motion.y;
			break;
		case GDK_BUTTON_PRESS:
		case GDK_2BUTTON_PRESS:
		case GDK_3BUTTON_PRESS:
		case GDK_BUTTON_RELEASE:
			x = event->button.x;
			y = event->button.y;
			break;
		default:
			return FALSE;
	}
	if (event->any.window == playlistwin->window)
	{
		x -= popup->x;
		y -= popup->y;
	}
	else if (event->any.window != popup->window)
		return FALSE;

	if (event->type == GDK_MOTION_NOTIFY)
		playlist_popup_set_active(x, y);
	else if (event->type == GDK_BUTTON_RELEASE && event->button.button == 1)
	{
		playlist_popup_set_active(x, y);
		playlist_popup_destroy();
	}
	return TRUE;
}

/* x, y: in the playlist window, where the top left item goes. */
void playlist_popup(gint x, gint y, gint num_items, gint * nx, gint * ny, gint * sx, gint * sy,
		    gint barx, gint bary, gint base, void (*handler) (gint item))
{
	GdkWindowAttr attr;

	if (popup)
		playlist_popup_destroy();
	popup = g_malloc0(sizeof (PlaylistPopup));
	popup->num_items = num_items;
	popup->nx = g_malloc0(sizeof (gint) * num_items);
	memcpy(popup->nx, nx, sizeof (gint) * num_items);
	popup->ny = g_malloc0(sizeof (gint) * num_items);
	memcpy(popup->ny, ny, sizeof (gint) * num_items);
	popup->sx = g_malloc0(sizeof (gint) * num_items);
	memcpy(popup->sx, sx, sizeof (gint) * num_items);
	popup->sy = g_malloc0(sizeof (gint) * num_items);
	memcpy(popup->sy, sy, sizeof (gint) * num_items);
	popup->barx = barx;
	popup->bary = bary;
	popup->handler = handler;
	popup->active = num_items - 1;
	popup->base = base;
	popup->x = x - 1;
	popup->y = y - 1;

	attr.window_type = GDK_WINDOW_CHILD;
	attr.wclass = GDK_INPUT_OUTPUT;
	attr.x = popup->x;
	attr.y = popup->y;
	attr.width = 25;
	attr.height = num_items * 18;
	attr.event_mask = GDK_EXPOSURE_MASK | GDK_POINTER_MOTION_MASK |
		GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK;
	popup->window = gdk_window_new(playlistwin->window, &attr, GDK_WA_X | GDK_WA_Y);
	gdk_window_set_user_data(popup->window, playlistwin);
	popup->gc = gdk_gc_new(popup->window);
	gdk_window_show(popup->window);
	playlist_popup_draw(popup);
	gdk_pointer_grab(popup->window, FALSE,
			 GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
			 NULL, NULL, GDK_CURRENT_TIME);
}
