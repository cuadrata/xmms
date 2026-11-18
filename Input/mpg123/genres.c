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

#include "xmms/i18n.h"

#include "mpg123-plugin.h"

/* The ID3v1 genres, with Winamp's extensions. */
const char *mpg123_id3_genres[GENRE_MAX] =
{
	N_("Blues"), N_("Classic Rock"), N_("Country"), N_("Dance"),
	N_("Disco"), N_("Funk"), N_("Grunge"), N_("Hip-Hop"),
	N_("Jazz"), N_("Metal"), N_("New Age"), N_("Oldies"),
	N_("Other"), N_("Pop"), N_("R&B"), N_("Rap"), N_("Reggae"),
	N_("Rock"), N_("Techno"), N_("Industrial"), N_("Alternative"),
	N_("Ska"), N_("Death Metal"), N_("Pranks"), N_("Soundtrack"),
	N_("Euro-Techno"), N_("Ambient"), N_("Trip-Hop"), N_("Vocal"),
	N_("Jazz+Funk"), N_("Fusion"), N_("Trance"), N_("Classical"),
	N_("Instrumental"), N_("Acid"), N_("House"), N_("Game"),
	N_("Sound Clip"), N_("Gospel"), N_("Noise"), N_("AlternRock"),
	N_("Bass"), N_("Soul"), N_("Punk"), N_("Space"),
	N_("Meditative"), N_("Instrumental Pop"),
	N_("Instrumental Rock"), N_("Ethnic"), N_("Gothic"),
	N_("Darkwave"), N_("Techno-Industrial"), N_("Electronic"),
	N_("Pop-Folk"), N_("Eurodance"), N_("Dream"),
	N_("Southern Rock"), N_("Comedy"), N_("Cult"),
	N_("Gangsta Rap"), N_("Top 40"), N_("Christian Rap"),
	N_("Pop/Funk"), N_("Jungle"), N_("Native American"),
	N_("Cabaret"), N_("New Wave"), N_("Psychedelic"), N_("Rave"),
	N_("Showtunes"), N_("Trailer"), N_("Lo-Fi"), N_("Tribal"),
	N_("Acid Punk"), N_("Acid Jazz"), N_("Polka"), N_("Retro"),
	N_("Musical"), N_("Rock & Roll"), N_("Hard Rock"), N_("Folk"),
	N_("Folk/Rock"), N_("National Folk"), N_("Swing"),
	N_("Fast-Fusion"), N_("Bebob"), N_("Latin"), N_("Revival"),
	N_("Celtic"), N_("Bluegrass"), N_("Avantgarde"),
	N_("Gothic Rock"), N_("Progressive Rock"),
	N_("Psychedelic Rock"), N_("Symphonic Rock"), N_("Slow Rock"),
	N_("Big Band"), N_("Chorus"), N_("Easy Listening"),
	N_("Acoustic"), N_("Humour"), N_("Speech"), N_("Chanson"),
	N_("Opera"), N_("Chamber Music"), N_("Sonata"), N_("Symphony"),
	N_("Booty Bass"), N_("Primus"), N_("Porn Groove"),
	N_("Satire"), N_("Slow Jam"), N_("Club"), N_("Tango"),
	N_("Samba"), N_("Folklore"), N_("Ballad"), N_("Power Ballad"),
	N_("Rhythmic Soul"), N_("Freestyle"), N_("Duet"),
	N_("Punk Rock"), N_("Drum Solo"), N_("A Cappella"),
	N_("Euro-House"), N_("Dance Hall"), N_("Goa"),
	N_("Drum & Bass"), N_("Club-House"), N_("Hardcore"),
	N_("Terror"), N_("Indie"), N_("BritPop"), N_("Negerpunk"),
	N_("Polsk Punk"), N_("Beat"), N_("Christian Gangsta Rap"),
	N_("Heavy Metal"), N_("Black Metal"), N_("Crossover"),
	N_("Contemporary Christian"), N_("Christian Rock"),
	N_("Merengue"), N_("Salsa"), N_("Thrash Metal"),
	N_("Anime"), N_("JPop"), N_("Synthpop")
};
