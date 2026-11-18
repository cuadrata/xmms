/*  XMMS - Cross-platform multimedia player
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
#ifndef LAYOUT_H
#define LAYOUT_H

/*
 * Where the window system places windows itself and a program cannot
 * (Wayland), XMMS's three windows are one window of the window
 * system's, laid out in one of these ways; see layout.c.
 */
enum
{
	LAYOUT_PLAYLIST_BESIDE,		/* equalizer under the player, playlist to their right */
	LAYOUT_PLAYLIST_BELOW		/* player, equalizer, playlist, one above the other */
};

gboolean layout_fixed(void);
void layout_apply(void);
void layout_set(gint layout);
void layout_playlist_axes(gboolean *horizontal, gboolean *vertical);

#endif
