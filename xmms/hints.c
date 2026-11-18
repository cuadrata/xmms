/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2003  Peter Alm, Mikael Alm, Olle Hallnas,
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
#include "xmms.h"

/*
 * What XMMS asks of the window manager for its windows: to keep them
 * above the others, to show them on every desktop, and to leave the
 * equalizer and playlist out of the taskbar. GDK asks it the way the
 * window system has, if it has one.
 */

static void hint_set_keep_above(GtkWidget *window, gboolean always)
{
	if (window && window->window)
		gdk_window_set_keep_above(window->window, always);
}

static void hint_set_window_sticky(GtkWidget *window, gboolean sticky)
{
	if (!window || !window->window)
		return;
	if (sticky)
		gdk_window_stick(window->window);
	else
		gdk_window_unstick(window->window);
}

void hint_set_skip_winlist(GtkWidget *window)
{
	if (window && window->window)
		gdk_window_set_skip_taskbar_hint(window->window, TRUE);
}

void hint_set_always(gboolean always)
{
	hint_set_keep_above(mainwin, always);
	hint_set_keep_above(equalizerwin, always);
	hint_set_keep_above(playlistwin, always);
}

gboolean hint_always_on_top_available(void)
{
	return TRUE;
}

void hint_set_sticky(gboolean sticky)
{
	hint_set_window_sticky(mainwin, sticky);
	hint_set_window_sticky(equalizerwin, sticky);
	hint_set_window_sticky(playlistwin, sticky);
}

/*
 * XMMS moves and resizes its windows itself. Where the window system
 * does it instead, dock.c hands it the drag with
 * gdk_window_begin_move_drag().
 */
gboolean hint_move_resize_available(void)
{
	return FALSE;
}

void hint_move_resize(GtkWidget *window, int x, int y, gboolean move)
{
}
