/*  XMMS - sndio output plugin
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

/*
 * The sndio output driver. XMMS buffers, converts and keeps the time;
 * this opens sndio, plays what it is given and says how much it holds.
 *
 * The handle is non-blocking: opening fails at once, and a write waits
 * on sndio's descriptors at most WRITE_WAIT_MS for room, then takes what
 * fits. sndio's clock is its own: it reports through sio_onmove() how
 * many frames have been played, and what was written and not yet played
 * is what it holds. A hang-up, or sio_eof(), is a lost device: as when
 * sndiod took the stream and then couldn't open its card.
 *
 * sndio only starts playing once its buffer is full, so start() fills it
 * with silence when it hasn't begun: a song's last part, shorter than the
 * buffer, would otherwise never be heard. A seek or a pause drops what
 * it holds with sio_flush(), and XMMS plays a pause's again; without
 * sio_flush(), sio_stop() plays it out first.
 */

#include "sndioout.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sndio.h>

/*
 * What sndio is asked to hold. sndiod adds its own buffer to it (sndiod
 * -b), and audio written is heard only after both, so that is how long a
 * pause or a volume change takes to be heard.
 */
#define DEVICE_BUFFER_MS	50
#define WRITE_WAIT_MS		100

static struct sio_hdl *hdl;
static gboolean started;
static gint frame_bytes, rate_hz, buffer_frames;
static gint64 sent, played;	/* frames, since it was last started */

static void on_move(void *arg, int delta)
{
	played += delta;
}

/*
 * Set sndio up for the format. A device opened directly may agree to
 * something else: XMMS converts to 8 or 16 bits, one or two channels,
 * and any rate, and nothing more.
 */
gint sndio_open(AFormat *fmt, gint *rate, gint *nch)
{
	static gboolean told_bits, told_channels;
	struct sio_par par;
	AFormat f = *fmt;

	if (hdl)
		sndio_close();
	hdl = sio_open(sndio_cfg.device, SIO_PLAY, 1);
	if (hdl == NULL)
		return -EIO;

	if (f == FMT_S16_NE)
		f = G_BYTE_ORDER == G_LITTLE_ENDIAN ? FMT_S16_LE : FMT_S16_BE;
	if (f == FMT_U16_NE)
		f = G_BYTE_ORDER == G_LITTLE_ENDIAN ? FMT_U16_LE : FMT_U16_BE;

	sio_initpar(&par);
	par.bits = f == FMT_U8 || f == FMT_S8 ? 8 : 16;
	par.sig = f == FMT_S8 || f == FMT_S16_LE || f == FMT_S16_BE;
	par.le = f == FMT_S16_LE || f == FMT_U16_LE;
	par.pchan = *nch;
	par.rate = *rate;
	par.appbufsz = *rate * DEVICE_BUFFER_MS / 1000;

	if (!sio_setpar(hdl, &par) || !sio_getpar(hdl, &par))
	{
		sndio_close();
		return -EIO;
	}
	if (par.bits == 8 && par.bps == 1)
		f = par.sig ? FMT_S8 : FMT_U8;
	else if (par.bits == 16 && par.bps == 2)
		f = par.sig ? (par.le ? FMT_S16_LE : FMT_S16_BE)
			    : (par.le ? FMT_U16_LE : FMT_U16_BE);
	else
	{
		/* Said once: XMMS tries again, and nothing will change. */
		if (!told_bits)
			g_warning("sndio: the device plays %u-bit audio, which XMMS cannot "
				  "convert to; sndiod converts anything", par.bits);
		told_bits = TRUE;
		sndio_close();
		return -EINVAL;
	}
	if (par.pchan < 1 || par.pchan > 2)
	{
		if (!told_channels)
			g_warning("sndio: the device plays %u channels, which XMMS cannot "
				  "convert to; sndiod converts anything", par.pchan);
		told_channels = TRUE;
		sndio_close();
		return -EINVAL;
	}

	sio_onmove(hdl, on_move, NULL);
	*fmt = f;
	*rate = par.rate;
	*nch = par.pchan;
	rate_hz = par.rate;
	frame_bytes = (par.bits / 8) * par.pchan;
	buffer_frames = par.appbufsz;
	started = FALSE;
	sent = played = 0;
	return 0;
}

/* What fits, after waiting at most WRITE_WAIT_MS for room: the bytes
 * taken, 0 if none yet, or negative if the device is lost. */
gint sndio_write(gpointer data, gint length)
{
	struct pollfd pfd[8];
	gint nfds, revents;
	size_t n;

	if (!hdl || frame_bytes <= 0)
		return -ENODEV;
	length -= length % frame_bytes;
	if (length <= 0)
		return 0;
	if (!started)
	{
		if (!sio_start(hdl))
			return -EIO;
		started = TRUE;
		sent = played = 0;
	}
	nfds = sio_pollfd(hdl, pfd, POLLOUT);
	if (nfds > 0 && poll(pfd, nfds, WRITE_WAIT_MS) < 0 && errno != EINTR)
		return -errno;
	revents = sio_revents(hdl, pfd);
	if (revents & POLLHUP || sio_eof(hdl))
		return -EIO;
	if (!(revents & POLLOUT))
		return 0;
	n = sio_write(hdl, data, length);
	if (n == 0 && sio_eof(hdl))
		return -EIO;
	n -= n % frame_bytes;
	sent += n / frame_bytes;
	return (gint) n;
}

gint sndio_delay(void)
{
	gint64 held;

	if (!hdl || !started || rate_hz <= 0)
		return 0;
	/* Brings the count of frames played up to date. */
	if (sio_nfds(hdl) > 0)
	{
		struct pollfd pfd[8];
		gint nfds = sio_pollfd(hdl, pfd, POLLOUT);

		if (nfds > 0)
		{
			poll(pfd, nfds, 0);
			sio_revents(hdl, pfd);
		}
	}
	held = sent - played;
	return held > 0 ? (gint) (held * 1000 / rate_hz) : 0;
}

/* What it holds played, though the buffer isn't full: if it hasn't begun,
 * silence fills the buffer so that it does. */
void sndio_start(void)
{
	gint64 room;
	gchar *silence;
	size_t n;

	if (!hdl || !started || played > 0 || sent <= 0)
		return;
	room = buffer_frames - (sent - played);
	if (room <= 0)
		return;
	silence = g_malloc0(room * frame_bytes);
	n = sio_write(hdl, silence, room * frame_bytes);
	sent += n / frame_bytes;
	g_free(silence);
}

/* Stopped, playing out what it holds or throwing it away. */
static void stop(gboolean drain)
{
	if (!hdl || !started)
		return;
	started = FALSE;
#ifdef HAVE_SIO_FLUSH
	if (!drain)
	{
		sio_flush(hdl);
		return;
	}
#endif
	sio_stop(hdl);
}

gint sndio_pause(gint p)
{
	gint64 held = sent - played;

	if (!p || !hdl || !started)
		return 0;
#ifdef HAVE_SIO_FLUSH
	stop(FALSE);
	return (gint) MAX(held, 0);
#else
	stop(TRUE);
	return 0;
#endif
}

void sndio_flush(void)
{
	stop(FALSE);
}

/* The rate the open device runs at; 0 if none is open. */
int sndio_device_rate(void)
{
	return hdl ? rate_hz : 0;
}

void sndio_close(void)
{
	if (!hdl)
		return;
	stop(FALSE);
	sio_close(hdl);
	hdl = NULL;
}
