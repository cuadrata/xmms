/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2003  Peter Alm, Mikael Alm, Olle Hallnas,
 *                           Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 1999-2005  Haavard Kvaalen
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
 * The OSS output driver. XMMS buffers, converts and keeps the time;
 * this opens the device, plays what it is given and says how much it
 * holds. The device is opened non-blocking and stays so: a busy one
 * fails to open at once, and a write waits at most WRITE_WAIT_MS for
 * room, then takes what fits. Errors are returned, not printed.
 */

#include "OSS.h"
#include <errno.h>
#include <poll.h>

#define WRITE_WAIT_MS	100

/* Fragments of about 20 ms, this many: XMMS's own buffer holds the rest. */
#define FRAGMENTS	12

static gint fd = -1;
static gint frame_bytes, rate_hz;

static int to_oss(AFormat fmt)
{
	switch (fmt)
	{
	case FMT_U8:
		return AFMT_U8;
	case FMT_S8:
		return AFMT_S8;
	case FMT_U16_LE:
		return AFMT_U16_LE;
	case FMT_U16_BE:
		return AFMT_U16_BE;
	case FMT_U16_NE:
		return IS_BIG_ENDIAN ? AFMT_U16_BE : AFMT_U16_LE;
	case FMT_S16_LE:
		return AFMT_S16_LE;
	case FMT_S16_BE:
		return AFMT_S16_BE;
	case FMT_S16_NE:
	default:
		return IS_BIG_ENDIAN ? AFMT_S16_BE : AFMT_S16_LE;
	}
}

static gboolean from_oss(int f, AFormat *fmt)
{
	switch (f)
	{
	case AFMT_U8:
		*fmt = FMT_U8;
		return TRUE;
	case AFMT_S8:
		*fmt = FMT_S8;
		return TRUE;
	case AFMT_U16_LE:
		*fmt = FMT_U16_LE;
		return TRUE;
	case AFMT_U16_BE:
		*fmt = FMT_U16_BE;
		return TRUE;
	case AFMT_S16_LE:
		*fmt = FMT_S16_LE;
		return TRUE;
	case AFMT_S16_BE:
		*fmt = FMT_S16_BE;
		return TRUE;
	default:
		return FALSE;
	}
}

static gchar *device_path(void)
{
	if (oss_cfg.use_alt_audio_device && oss_cfg.alt_audio_device)
		return g_strdup(oss_cfg.alt_audio_device);
	if (oss_cfg.audio_device > 0)
		return g_strdup_printf("%s%d", DEV_DSP, oss_cfg.audio_device);
	return g_strdup(DEV_DSP);
}

int oss_open(AFormat *fmt, int *rate, int *nch)
{
	gchar *path = device_path();
	int f = to_oss(*fmt), channels = *nch, speed = *rate;
	int bytes_per_sec, size_log = 4, frag;

	if (fd != -1)
		oss_close();
	fd = open(path, O_WRONLY | O_NONBLOCK);
	g_free(path);
	if (fd == -1)
		return -errno;

	/* The fragments first, as OSS wants: before the format. */
	bytes_per_sec = speed * channels * ((f == AFMT_U8 || f == AFMT_S8) ? 1 : 2);
	while ((1 << size_log) < bytes_per_sec / 50 && size_log < 16)
		size_log++;
	frag = (FRAGMENTS << 16) | size_log;
	ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &frag);

	/* The format it takes, which may not be the one asked for. */
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &f) == -1 || !from_oss(f, fmt))
	{
		f = IS_BIG_ENDIAN ? AFMT_S16_BE : AFMT_S16_LE;
		if (ioctl(fd, SNDCTL_DSP_SETFMT, &f) == -1 || !from_oss(f, fmt))
		{
			oss_close();
			return -EINVAL;
		}
	}
	if (ioctl(fd, SNDCTL_DSP_CHANNELS, &channels) == -1)
	{
		/* Older drivers: mono or stereo only. */
		int stereo = channels > 1;

		ioctl(fd, SNDCTL_DSP_STEREO, &stereo);
		channels = stereo + 1;
	}
	if (ioctl(fd, SNDCTL_DSP_SPEED, &speed) == -1 || speed <= 0)
	{
		oss_close();
		return -EINVAL;
	}

	*nch = channels;
	*rate = speed;
	frame_bytes = channels * ((f == AFMT_U8 || f == AFMT_S8) ? 1 : 2);
	rate_hz = speed;
	return 0;
}

/* What fits, after waiting at most WRITE_WAIT_MS for room: the bytes
 * taken, 0 if none yet, or -errno if the device is lost. */
int oss_write(void *data, int length)
{
	struct pollfd pfd;
	ssize_t n;

	if (fd == -1 || frame_bytes <= 0)
		return -ENODEV;
	length -= length % frame_bytes;
	if (length <= 0)
		return 0;
	pfd.fd = fd;
	pfd.events = POLLOUT;
	if (poll(&pfd, 1, WRITE_WAIT_MS) <= 0)
		return 0;
	if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
		return -EIO;
	n = write(fd, data, length);
	if (n == -1)
		return (errno == EAGAIN || errno == EINTR) ? 0 : -errno;
	return (int) (n - n % frame_bytes);
}

/* What the device holds, in bytes. */
static int held_bytes(void)
{
	int odelay;
	audio_buf_info info;

	if (fd == -1)
		return 0;
	if (ioctl(fd, SNDCTL_DSP_GETODELAY, &odelay) == 0)
		return MAX(odelay, 0);
	/* Some drivers report more free space than the whole buffer. */
	if (ioctl(fd, SNDCTL_DSP_GETOSPACE, &info) == 0)
		return MAX(info.fragstotal * info.fragsize - info.bytes, 0);
	return 0;
}

int oss_delay(void)
{
	if (rate_hz <= 0 || frame_bytes <= 0)
		return 0;
	return (int) ((gint64) held_bytes() / frame_bytes * 1000 / rate_hz);
}

/* OSS can't hold what it has while paused: it drops it, to be played again. */
int oss_pause(int p)
{
	int held;

	if (fd == -1 || !p || frame_bytes <= 0)
		return 0;
	held = held_bytes() / frame_bytes;
	ioctl(fd, SNDCTL_DSP_RESET, 0);
	return held;
}

void oss_flush(void)
{
	if (fd != -1)
		ioctl(fd, SNDCTL_DSP_RESET, 0);
}

/* What it holds played now, partial fragment and all. */
void oss_start(void)
{
	if (fd != -1)
		ioctl(fd, SNDCTL_DSP_POST, 0);
}

/* The rate the open device runs at; 0 if none is open. */
int oss_device_rate(void)
{
	return fd >= 0 ? rate_hz : 0;
}

void oss_close(void)
{
	if (fd == -1)
		return;
	ioctl(fd, SNDCTL_DSP_RESET, 0);
	close(fd);
	fd = -1;
}
