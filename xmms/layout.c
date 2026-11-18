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
#include "xmms.h"

/*
 * On X11, XMMS's windows are three windows that it moves about itself,
 * docking them to one another (dock.c). Where the window system places
 * windows and a program cannot, as on Wayland, they are one window of
 * the window system's -- the player, with the equalizer and playlist
 * grouped to it -- in a fixed layout chosen by the user: the equalizer
 * under the player, and the playlist either to the right of them or
 * under them. Nothing docks or undocks; a window that is hidden or
 * shaded closes the gap it leaves.
 *
 * The playlist is still resizable, along the one axis that keeps the
 * whole a rectangle: by its width beside the others, taking the
 * height of the column next to it, and by its height under them, as
 * wide as they are. Where the column's height is not one the playlist
 * can be drawn at -- the player or the equalizer shaded -- the playlist
 * keeps the height it has until it is. Alone, it is free in both.
 */

static gboolean applying = FALSE;

gboolean layout_fixed(void)
{
	return !gdk_can_place_toplevels();
}

/* The sizes the playlist's skin can be drawn at. */
static gboolean playlist_height_ok(gint h)
{
	return h >= 116 && (h - 58) % 29 == 0;
}

static gboolean playlist_width_ok(gint w)
{
	return w >= 275 && (w - 275) % 25 == 0;
}

/*
 * Along which axes the playlist may be resized: by the user with its
 * corner, and by the window system resizing the window of them all.
 */
void layout_playlist_axes(gboolean *horizontal, gboolean *vertical)
{
	gboolean column = cfg.player_visible || cfg.equalizer_visible;

	if (!layout_fixed() || !column)
	{
		*horizontal = TRUE;
		*vertical = TRUE;
		return;
	}
	*horizontal = cfg.layout == LAYOUT_PLAYLIST_BESIDE;
	*vertical = cfg.layout == LAYOUT_PLAYLIST_BELOW;
}

/*
 * Places the equalizer and the playlist about the player, and sizes
 * the playlist to fit. Called whenever one of the three is shown,
 * hidden, shaded, resized or the layout changes; does nothing where
 * XMMS places its windows itself.
 */
void layout_apply(void)
{
	gint x, y, mw, mh, ew, eh, cw, ch;
	gboolean horizontal, vertical;

	if (!layout_fixed() || applying || !mainwin || !mainwin->window)
		return;
	applying = TRUE;

	dock_get_widget_pos(mainwin, &x, &y);
	mw = cfg.player_visible ? PLAYER_WIDTH : 0;
	mh = cfg.player_visible ? PLAYER_HEIGHT : 0;
	ew = cfg.equalizer_visible ? EQUALIZER_WIDTH : 0;
	eh = cfg.equalizer_visible ? EQUALIZER_HEIGHT : 0;
	cw = MAX(mw, ew);
	ch = mh + eh;

	if (equalizerwin)
		dock_set_uposition(equalizerwin, x, y + mh);

	if (playlistwin)
	{
		layout_playlist_axes(&horizontal, &vertical);
		if (cfg.layout == LAYOUT_PLAYLIST_BESIDE)
		{
			if (ch > 0 && !cfg.playlist_shaded &&
			    ch != cfg.playlist_height && playlist_height_ok(ch))
				playlistwin_set_size(cfg.playlist_width, ch);
			dock_set_uposition(playlistwin, x + cw, y);
		}
		else
		{
			if (cw > 0 && cw != cfg.playlist_width && playlist_width_ok(cw))
				playlistwin_set_size(cw, cfg.playlist_height);
			dock_set_uposition(playlistwin, x, y + ch);
		}
		playlistwin_set_hints();
		if (playlistwin->window)
			gdk_window_set_group_resize(playlistwin->window, horizontal, vertical);
	}
	applying = FALSE;
}

/*
 * The layout forces one of the playlist's dimensions, so the size the
 * user gave it along that axis in the other layout is kept apart, and
 * comes back with that layout, once the new layout has forced the
 * other.
 */
void layout_set(gint layout)
{
	gboolean changing;

	if (layout != LAYOUT_PLAYLIST_BESIDE && layout != LAYOUT_PLAYLIST_BELOW)
		return;
	changing = layout != cfg.layout && layout_fixed() && playlistwin;
	if (changing)
	{
		if (cfg.layout == LAYOUT_PLAYLIST_BESIDE)
			cfg.playlist_beside_width = cfg.playlist_width;
		else
			cfg.playlist_below_height = cfg.playlist_height;
	}
	cfg.layout = layout;
	mainwin_set_layout_menu(layout);
	prefswin_set_layout(layout);
	layout_apply();
	if (changing)
	{
		if (layout == LAYOUT_PLAYLIST_BESIDE &&
		    playlist_width_ok(cfg.playlist_beside_width) &&
		    cfg.playlist_beside_width != cfg.playlist_width)
		{
			playlistwin_set_size(cfg.playlist_beside_width, cfg.playlist_height);
			layout_apply();
		}
		else if (layout == LAYOUT_PLAYLIST_BELOW &&
			 playlist_height_ok(cfg.playlist_below_height) &&
			 cfg.playlist_below_height != cfg.playlist_height)
		{
			playlistwin_set_size(cfg.playlist_width, cfg.playlist_below_height);
			layout_apply();
		}
	}
}
