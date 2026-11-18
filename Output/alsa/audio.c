/*  XMMS - ALSA output plugin
 *  Copyright (C) 2001-2003 Matthieu Sozeau <mattam@altern.org>
 *  Copyright (C) 1998-2003  Peter Alm, Mikael Alm, Olle Hallnas,
 *                           Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 1999-2006  Haavard Kvaalen
 *  Copyright (C) 2005       Takashi Iwai
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
 * The ALSA output driver. XMMS buffers, converts and keeps the time;
 * this opens the device, plays what it is given and says how much it
 * holds. The PCM is non-blocking: a busy device fails to open at once,
 * and a write waits at most WRITE_WAIT_MS for room, then takes what
 * fits. Errors are returned, not printed: XMMS reports them.
 */

#include "alsa.h"
#include <errno.h>

#define WRITE_WAIT_MS	100

static snd_pcm_t *alsa_pcm;
static unsigned int alsa_rate;
static int alsa_frame;
static gboolean alsa_can_pause, alsa_paused;

static snd_pcm_format_t to_alsa(AFormat fmt)
{
	switch (fmt)
	{
	case FMT_U8:
		return SND_PCM_FORMAT_U8;
	case FMT_S8:
		return SND_PCM_FORMAT_S8;
	case FMT_U16_LE:
		return SND_PCM_FORMAT_U16_LE;
	case FMT_U16_BE:
		return SND_PCM_FORMAT_U16_BE;
	case FMT_U16_NE:
		return SND_PCM_FORMAT_U16;
	case FMT_S16_LE:
		return SND_PCM_FORMAT_S16_LE;
	case FMT_S16_BE:
		return SND_PCM_FORMAT_S16_BE;
	case FMT_S16_NE:
	default:
		return SND_PCM_FORMAT_S16;
	}
}

static gboolean from_alsa(snd_pcm_format_t f, AFormat *fmt)
{
	switch (f)
	{
	case SND_PCM_FORMAT_U8:
		*fmt = FMT_U8;
		return TRUE;
	case SND_PCM_FORMAT_S8:
		*fmt = FMT_S8;
		return TRUE;
	case SND_PCM_FORMAT_U16_LE:
		*fmt = FMT_U16_LE;
		return TRUE;
	case SND_PCM_FORMAT_U16_BE:
		*fmt = FMT_U16_BE;
		return TRUE;
	case SND_PCM_FORMAT_S16_LE:
		*fmt = FMT_S16_LE;
		return TRUE;
	case SND_PCM_FORMAT_S16_BE:
		*fmt = FMT_S16_BE;
		return TRUE;
	default:
		return FALSE;
	}
}

/*
 * An underrun or a suspend: the device ready to play again, if it can be.
 * 0 if it is, or will be soon (a resume still under way), else the error.
 */
static int alsa_recover(int err)
{
	if (err == -ESTRPIPE)
	{
		err = snd_pcm_resume(alsa_pcm);
		if (err == -EAGAIN || err == 0)
			return 0;
		err = -ESTRPIPE;
	}
	if (err == -EPIPE || err == -ESTRPIPE)
		return snd_pcm_prepare(alsa_pcm);
	return err;
}

/* Gave up opening: the half-opened device let go, and why returned. */
static int alsa_fail(int err)
{
	snd_pcm_close(alsa_pcm);
	alsa_pcm = NULL;
	return err < 0 ? err : -EINVAL;
}

int alsa_open(AFormat *fmt, int *rate, int *nch)
{
	snd_pcm_hw_params_t *hw;
	snd_pcm_sw_params_t *sw;
	snd_pcm_format_t f = to_alsa(*fmt);
	unsigned int channels = *nch, r = *rate, buffer_time, period_time;
	snd_pcm_uframes_t period;
	int err;

	if (alsa_pcm)
		alsa_close();
	/* Non-blocking: a busy device fails at once, and writes never wait long. */
	if ((err = snd_pcm_open(&alsa_pcm, alsa_cfg.pcm_device,
				SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0)
	{
		alsa_pcm = NULL;
		return err;
	}

	snd_pcm_hw_params_alloca(&hw);
	if ((err = snd_pcm_hw_params_any(alsa_pcm, hw)) < 0)
	{
		return alsa_fail(err);
	}
	if ((err = snd_pcm_hw_params_set_access(alsa_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
	{
		return alsa_fail(err);
	}
	/* One of these works on almost every card. */
	if (snd_pcm_hw_params_set_format(alsa_pcm, hw, f) < 0)
	{
		static const snd_pcm_format_t fallback[] = {
			SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_S16_BE, SND_PCM_FORMAT_U8
		};
		guint i;

		for (i = 0; i < sizeof fallback / sizeof fallback[0]; i++)
			if (snd_pcm_hw_params_set_format(alsa_pcm, hw, fallback[i]) == 0)
				break;
		if (i == sizeof fallback / sizeof fallback[0])
		{
			return alsa_fail(-EINVAL);
		}
		f = fallback[i];
	}
	if ((err = snd_pcm_hw_params_set_channels_near(alsa_pcm, hw, &channels)) < 0)
	{
		return alsa_fail(err);
	}
	/*
	 * The device's own rates only: XMMS converts others itself, and
	 * better. alsa-lib's rate converter would also stand between XMMS
	 * and dmix, and it can't take back what it was given, so what a
	 * skip drops would still be heard.
	 */
	snd_pcm_hw_params_set_rate_resample(alsa_pcm, hw, 0);
	if ((err = snd_pcm_hw_params_set_rate_near(alsa_pcm, hw, &r, 0)) < 0 || r == 0)
	{
		return alsa_fail(err < 0 ? err : -EINVAL);
	}
	buffer_time = alsa_cfg.buffer_time * 1000;
	if ((err = snd_pcm_hw_params_set_buffer_time_near(alsa_pcm, hw, &buffer_time, 0)) < 0)
	{
		return alsa_fail(err);
	}
	period_time = alsa_cfg.period_time * 1000;
	if ((err = snd_pcm_hw_params_set_period_time_near(alsa_pcm, hw, &period_time, 0)) < 0)
	{
		return alsa_fail(err);
	}
	if ((err = snd_pcm_hw_params(alsa_pcm, hw)) < 0)
	{
		return alsa_fail(err);
	}
	snd_pcm_hw_params_get_period_size(hw, &period, 0);
	alsa_can_pause = snd_pcm_hw_params_can_pause(hw);

	snd_pcm_sw_params_alloca(&sw);
	snd_pcm_sw_params_current(alsa_pcm, sw);
	/* It starts once a period is queued. */
	snd_pcm_sw_params_set_start_threshold(alsa_pcm, sw, period);
	snd_pcm_sw_params_set_avail_min(alsa_pcm, sw, period);
	if ((err = snd_pcm_sw_params(alsa_pcm, sw)) < 0)
	{
		return alsa_fail(err);
	}

	if (!from_alsa(f, fmt))
		*fmt = FMT_S16_NE;
	*nch = channels;
	*rate = r;
	alsa_rate = r;
	alsa_frame = snd_pcm_format_physical_width(f) / 8 * channels;
	alsa_paused = FALSE;
	return 0;
}

/* What fits, after waiting at most WRITE_WAIT_MS for room: the bytes
 * taken, 0 if none yet, or the error if the device is lost. */
int alsa_write(void *data, int length)
{
	snd_pcm_sframes_t frames, n;
	int err;

	if (!alsa_pcm || alsa_frame <= 0)
		return -ENODEV;
	frames = length / alsa_frame;
	if (frames <= 0)
		return 0;
	n = snd_pcm_writei(alsa_pcm, data, frames);
	if (n == -EAGAIN)
	{
		err = snd_pcm_wait(alsa_pcm, WRITE_WAIT_MS);
		if (err == 0)
			return 0;
		if (err < 0)
			return alsa_recover(err) < 0 ? err : 0;
		n = snd_pcm_writei(alsa_pcm, data, frames);
		if (n == -EAGAIN)
			return 0;
	}
	if (n < 0)
		return alsa_recover(n) < 0 ? (int) n : 0;
	return (int) n * alsa_frame;
}

int alsa_delay(void)
{
	snd_pcm_sframes_t d;

	if (!alsa_pcm || alsa_rate == 0 || snd_pcm_delay(alsa_pcm, &d) < 0 || d <= 0)
		return 0;
	return (int) ((gint64) d * 1000 / alsa_rate);
}

/* Paused, or going on: what it drops, if it can't pause, is played again. */
/*
 * Take back what the device holds and hasn't played, as far as it lets
 * us, before a drop. Through dmix this takes it out of the mix again: a
 * drop alone leaves what was mixed for the card to be heard, the end of
 * one song over the start of the next. The frames taken back.
 */
static snd_pcm_sframes_t take_back(void)
{
	snd_pcm_sframes_t n;

	/* Where the device has got to now, not at the last write. */
	snd_pcm_avail(alsa_pcm);
	if ((n = snd_pcm_rewindable(alsa_pcm)) <= 0)
		return 0;
	n = snd_pcm_rewind(alsa_pcm, n);
	return n > 0 ? n : 0;
}

int alsa_pause(int p)
{
	snd_pcm_state_t state;
	snd_pcm_sframes_t held = 0;

	if (!alsa_pcm || !p == !alsa_paused)
		return 0;
	state = snd_pcm_state(alsa_pcm);
	if (p)
	{
		/* Not started yet, it has nothing to stop. */
		if (state == SND_PCM_STATE_RUNNING &&
		    (!alsa_can_pause || snd_pcm_pause(alsa_pcm, 1) < 0))
		{
			/* It can't pause: what it holds is dropped, to be played
			 * again. What it gave back is what won't be heard; a device
			 * that gives nothing back drops all it holds. */
			if ((held = take_back()) == 0 &&
			    (snd_pcm_delay(alsa_pcm, &held) < 0 || held < 0))
				held = 0;
			snd_pcm_drop(alsa_pcm);
		}
	}
	else if (state == SND_PCM_STATE_PAUSED)
		snd_pcm_pause(alsa_pcm, 0);
	else if (state == SND_PCM_STATE_SETUP)
		snd_pcm_prepare(alsa_pcm);
	alsa_paused = p ? TRUE : FALSE;
	return (int) held;
}

void alsa_flush(void)
{
	if (!alsa_pcm)
		return;
	take_back();
	snd_pcm_drop(alsa_pcm);
	snd_pcm_prepare(alsa_pcm);
	alsa_paused = FALSE;
}

/* What it holds played now, though less than a period: it starts only
 * once a period is queued, and nothing more is coming for now. */
void alsa_start(void)
{
	snd_pcm_sframes_t held;

	if (alsa_pcm && snd_pcm_state(alsa_pcm) == SND_PCM_STATE_PREPARED &&
	    snd_pcm_delay(alsa_pcm, &held) == 0 && held > 0)
		snd_pcm_start(alsa_pcm);
}

/* The rate the open device runs at; 0 if none is open. */
int alsa_device_rate(void)
{
	return alsa_pcm ? (int) alsa_rate : 0;
}

void alsa_close(void)
{
	if (!alsa_pcm)
		return;
	snd_pcm_drop(alsa_pcm);
	snd_pcm_close(alsa_pcm);
	alsa_pcm = NULL;
	alsa_paused = FALSE;
}
