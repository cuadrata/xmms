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

/*
 * The output pipeline.
 *
 * Input plugins write their audio through the OutputPlugin interface they
 * always have, into a ring buffer. A thread of the pipeline's own takes
 * it from there a block at a time, makes it 16-bit, runs the equalizer,
 * the effect plugins and the volume over it, converts it to the rate,
 * format and channels the device settled on, and writes it to the output
 * driver. A driver may keep its device at one rate, in stereo, whatever
 * the song's (its get_rate()); a song at another rate is converted, with
 * libsamplerate, and nothing is lost or added where songs join.
 *
 * That thread is the only one that ever calls the driver. Everything
 * else asks: a song opening says which device and format it wants, a
 * seek or a stop asks for the device's audio to be dropped, a stop for
 * the device to be let go, the interface for a pause; each records its
 * request and returns, and the thread acts on the latest requests
 * between its writes, none of which waits more than about 100 ms. So
 * nothing outside the thread ever waits on a device, however slow,
 * busy, stalled or gone it is.
 *
 * The device is always in one state, which only the thread changes:
 *
 *   Closed       no device.
 *   Opening      wanted, but it wouldn't open: tried again after 0.25 s,
 *                doubling to every 2 s, for as long as the song wants it.
 *   Playing      taking the song's audio.
 *   Paused       held where it was; what it dropped is played again.
 *   Playing out  the song has ended and the device holds its last audio,
 *                or the song was stopped and the device dropped it, or
 *                nothing has played yet. The next song in the same
 *                format, on the same driver, follows it there, with no
 *                gap after one that ended; another device or format
 *                waits until it has been heard. With nothing to follow,
 *                the device stays open.
 *
 * Some devices lose the start of what they play after being opened (a USB
 * DAC that sets itself up again), though not after a drop. So the device
 * is kept open while XMMS runs: a stop or a skip only drops what it holds,
 * and with nothing playing, the device is opened anyway, once for each
 * driver chosen, in the format most music has. It closes for a song in
 * another format or on another driver. A driver with a device per song,
 * or with no clock, is opened for each song and closed after it.
 *
 * A device that fails a write, or takes nothing for STALL_MS while
 * playing, is lost: what it held is dropped, and it is opened again as
 * if it had never opened.
 *
 * The time shown is what the device has played of the song: the audio
 * it took, less what it still holds. It stands still while there is no
 * device, and is frozen when paused.
 */

#include "xmms.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#include "libxmms/xconvert.h"
#include "resample.h"

/* Audio taken from the ring at a time. */
#define BLOCK_MS		20
/* Stop waiting for the prebuffer when nothing has been written for this
 * long: a short file, or a stream that stalls. */
#define PREBUFFER_IDLE_MS	200
/* How often an open device with nothing to play is looked at; anything
 * asked of the thread wakes it sooner. */
#define IDLE_POLL_MS		1000
/* The format a device is opened in before any song: 16-bit, as every
 * stream asks for, and stereo, at the driver's own rate, or for a driver
 * that follows each song's rate, the one most music has. */
#define IDLE_RATE		44100
#define IDLE_NCH		2
/* The driver's buffering, when it doesn't say. */
#define DEFAULT_BUFFER_MS	3000
#define DEFAULT_PREBUFFER	25	/* percent */
/* A device that won't open is tried again this soon, then less often. */
#define RETRY_FIRST_MS		250
#define RETRY_MAX_MS		2000
/* A device that takes nothing for this long, while playing, is lost. */
#define STALL_MS		3000
/*
 * After a driver that lingers (ALSA, through a sound server that holds the
 * card a few seconds after the last stream closes: PipeWire's 5 s), another
 * driver waits this long before opening the card itself. Otherwise OSS
 * finds it busy, which is waited out anyway, and sndio, whose server takes
 * the stream though it couldn't open the card, plays silence.
 */
#define HANDOVER_MS		6000
/* What was last written to the device, kept to play again what it drops
 * when it pauses. */
#define HISTORY_MS		2000
/* With nothing more to come, the converter gives out what it holds once
 * the device holds less than this: soon enough to be heard in turn, late
 * enough for the next song to follow on at the same rate. */
#define DRAIN_MS		200

typedef enum
{
	DEV_CLOSED,
	DEV_OPENING,
	DEV_PLAYING,
	DEV_PAUSED,
	DEV_PLAYING_OUT
}
DevState;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static gboolean thread_up;

/* Under lock: the stream the input plugin writes. */
static gboolean quit;
static gboolean stream_open;
static gboolean song_ended;		/* the next close is a song's natural end */
static AFormat in_fmt;
static gint in_rate, in_nch, in_frame, in_bps;
static guchar *ring;
static gint ring_size, rd, used;
static gint in_flight;			/* taken from the ring, not all given to the device yet */
static guint64 written, delivered;	/* the plugin's bytes, since opening or flushing */
static gint base_time;			/* ms, at opening or flushing */
static guint generation;		/* counts openings, flushes and stops */
static gboolean paused;			/* the pause asked for */
static gint pause_time;
static gboolean prebuffering;
static gint prebuffer_bytes, free_calls;
static gboolean interrupted;		/* a seek or stop is coming: pass_audio() won't wait */
static struct timeval last_write;

/* Under lock: what is asked of the device. */
static OutputDriver *drv;		/* the driver chosen */
static gboolean want_valid;		/* a song wants a device */
static OutputDriver *want_drv;		/* which, chosen when the song opened */
static AFormat want_fmt;
static gint want_rate, want_nch;
static gboolean req_drop;		/* drop what the device holds: a seek or a stop */
static gboolean req_close;		/* and close it: a stop */
static guint want_serial;		/* counts songs opened */

/* Under lock: what the thread last found the device holding. */
static gint dev_delay;			/* ms */
static struct timeval dev_delay_at;
static gboolean dev_running;		/* the delay falls as time passes */

/* The thread's own. */
static DevState state = DEV_CLOSED;
static OutputDriver *dev_drv;		/* the driver the device was opened with */
static AFormat dev_fmt, dev_want_fmt;	/* what it took; what it was opened for */
static gint dev_rate, dev_nch, dev_want_rate, dev_want_nch;
static gint dev_quality;		/* how songs at other rates are converted */
static guint dev_serial;		/* the song it was opened for */
static gint retry_ms;
static struct timeval retry_at;
static gboolean open_failing;		/* told the terminal; told again only once it opens */
static gboolean start_sent;		/* start() since the last write */
static gboolean stalling;
static gboolean write_failed;		/* a per-song device's write failed, this song */
static OutputDriver *idle_tried;	/* the driver last opened with no song */
static OutputDriver *closed_drv;	/* the driver last closed, and when */
static struct timeval closed_at;
static struct timeval stall_since;
static guint eq_generation;		/* the stream the equalizer's memory is of */
static struct xmms_convert_buffers *cbuf;
static Resampler *rs;
static guchar *work;
static gint work_size;
static guchar *history, *replay;
static gint history_size, history_end, history_fill, replay_bytes;

/* A processed block, being written: a device may take it a part at a
 * time. A replay goes before it, and it is held meanwhile. */
typedef struct
{
	gboolean active;
	guchar *data;
	gint length, offset;
	gint in_bytes;		/* the ring's bytes it was made from: 0 for a replay */
	guint gen;
}
Pending;

static Pending out, held;

static gint ms_since(const struct timeval *tv)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return (gint) ((now.tv_sec - tv->tv_sec) * 1000 + (now.tv_usec - tv->tv_usec) / 1000);
}

static gint sample_size(AFormat fmt)
{
	return (fmt == FMT_U8 || fmt == FMT_S8) ? 1 : 2;
}

static gint bytes_to_ms(guint64 bytes)
{
	return in_bps ? (gint) (bytes * 1000 / in_bps) : 0;
}

/* What the device holds now, as best known without asking it. */
static gint delay_now_locked(void)
{
	gint d = dev_delay;

	if (dev_running && !paused)
		d -= ms_since(&dev_delay_at);
	return MAX(d, 0);
}

static gint output_time_locked(void)
{
	gint t;

	if (paused)
		return pause_time;
	t = base_time + bytes_to_ms(delivered) - delay_now_locked();
	/* A song's audio heard before it began: the last one's still playing. */
	return MAX(t, base_time);
}

static void set_delay_locked(gint delay, gboolean running)
{
	dev_delay = MAX(delay, 0);
	dev_running = running && dev_delay > 0;
	gettimeofday(&dev_delay_at, NULL);
}

/* Whether a driver may be left open when nothing plays, and so share it
 * between songs. */
static gboolean driver_idles(OutputDriver *d)
{
	return d && !(d->flags & (OUTPUT_DRIVER_PER_SONG | OUTPUT_DRIVER_NO_CLOCK));
}

/* The rate a driver wants its device opened at whatever the song's, 0
 * for the song's own, and how songs at other rates are converted. */
static gint driver_rate(OutputDriver *d, gint *quality)
{
	gint rate = 0, q = OUTPUT_QUALITY_BEST;

	if (d && d->get_rate)
		d->get_rate(&rate, &q);
	if (quality)
		*quality = q;
	return MAX(rate, 0);
}

/* ---- the blocks: the thread's own ---- */

static gboolean native16(AFormat fmt)
{
	return fmt == FMT_S16_NE || fmt == (G_BYTE_ORDER == G_LITTLE_ENDIAN ? FMT_S16_LE : FMT_S16_BE);
}

/* Into the device's sample format and channels. */
static gint device_format(void **data, gint length, AFormat fmt, gint nch)
{
	convert_func_t fmt_func;
	convert_channel_func_t ch_func;

	if (fmt != dev_fmt && (fmt_func = xmms_convert_get_func(dev_fmt, fmt)) != NULL)
		length = fmt_func(cbuf, data, length);
	if (nch != dev_nch && (ch_func = xmms_convert_get_channel_func(dev_fmt, dev_nch, nch)) != NULL)
		length = ch_func(cbuf, data, length);
	return length;
}

/* The block, in the format it was written in, through the equalizer,
 * effects and volume, and into the device's format. Returns its
 * length, with *data where it now is. The thread's own. */
static gint process_block(void **data, gint length, AFormat fmt, gint rate, gint nch)
{
	AFormat ofmt;
	gint orate, onch;
	EffectPlugin *ep;
	convert_func_t fmt_func;

	/* Everything after works on 16-bit native samples. */
	if (fmt != FMT_S16_NE && (fmt_func = xmms_convert_get_func(FMT_S16_NE, fmt)) != NULL)
	{
		length = fmt_func(cbuf, data, length);
		fmt = FMT_S16_NE;
	}
	if (native16(fmt))
		eq_filter_apply(*data, length / (2 * nch), nch, rate);

	ep = effects_enabled() ? get_current_effect_plugin() : NULL;
	ofmt = fmt;
	orate = rate;
	onch = nch;
	if (ep && ep->mod_samples)
	{
		if (ep->query_format)
			ep->query_format(&ofmt, &orate, &onch);
		length = ep->mod_samples(data, length, fmt, rate, nch);
	}

	xmms_apply_volume(*data, length, ofmt, onch);

	/* Another rate than the device's, or the end of one: converted at 16
	 * bits, before the device's format and channels. */
	if (orate != dev_rate || resampler_holding(rs))
	{
		if (!native16(ofmt) && (fmt_func = xmms_convert_get_func(FMT_S16_NE, ofmt)) != NULL)
		{
			length = fmt_func(cbuf, data, length);
			ofmt = FMT_S16_NE;
		}
		if (native16(ofmt))
			length = resampler_run(rs, data, length, orate, dev_rate, onch, dev_quality);
	}
	return device_format(data, length, ofmt, onch);
}

/* One block from the ring to the device. Called with lock held, and
 * returns with it held. */

/* The format the device should have for a stream: what the effects make
 * of it. */
static void wanted_format(AFormat *fmt, gint *rate, gint *nch)
{
	EffectPlugin *ep = effects_enabled() ? get_current_effect_plugin() : NULL;

	*fmt = FMT_S16_NE;
	if (ep && ep->query_format)
		ep->query_format(fmt, rate, nch);
}


/* ---- the device: only the thread comes here ---- */

static void history_reset(void)
{
	history_end = history_fill = 0;
	replay_bytes = 0;
}

static void history_add(const guchar *data, gint length)
{
	gint first;

	if (history_size <= 0)
		return;
	if (length > history_size)
	{
		data += length - history_size;
		length = history_size;
	}
	first = MIN(length, history_size - history_end);
	memcpy(history + history_end, data, first);
	memcpy(history, data + first, length - first);
	history_end = (history_end + length) % history_size;
	history_fill = MIN(history_fill + length, history_size);
}

/* The last bytes written, played again before anything else: what a
 * device dropped when it paused. The block being written waits. */
static void replay_last(gint bytes, guint gen)
{
	gint start, first;

	bytes = MIN(bytes, history_fill);
	bytes -= bytes % (sample_size(dev_fmt) * dev_nch);
	if (bytes <= 0)
		return;
	replay = g_realloc(replay, bytes);
	start = (history_end - bytes + history_size) % history_size;
	first = MIN(bytes, history_size - start);
	memcpy(replay, history + start, first);
	memcpy(replay + first, history, bytes - first);
	held = out;
	out.active = TRUE;
	out.data = replay;
	out.length = bytes;
	out.offset = 0;
	out.in_bytes = 0;
	out.gen = gen;
}

static const gchar *driver_name(OutputDriver *d)
{
	return d && d->description ? d->description : "the output plugin";
}

static gboolean device_open(OutputDriver *d, AFormat fmt, gint rate, gint nch)
{
	AFormat f = fmt;
	gint r = rate, n = nch, err;

	err = d->open ? d->open(&f, &r, &n) : -ENODEV;
	if (err != 0 || r <= 0 || n <= 0)
	{
		/* Once for a run of failures; again only after it has opened. */
		if (!open_failing)
			g_warning("output: %s couldn't open the device (%s); trying until it can",
				  driver_name(d), err < 0 ? g_strerror(-err) : "unknown error");
		open_failing = TRUE;
		return FALSE;
	}
	if (open_failing)
		g_message("output: %s opened the device", driver_name(d));
	open_failing = FALSE;

	dev_drv = d;
	dev_fmt = f;
	dev_rate = r;
	dev_nch = n;
	dev_want_fmt = fmt;
	dev_want_rate = rate;
	dev_want_nch = nch;
	driver_rate(d, &dev_quality);
	resampler_reset(rs);
	start_sent = FALSE;
	stalling = FALSE;
	write_failed = FALSE;
	history_size = MAX((gint) ((gint64) r * HISTORY_MS / 1000), 1) * sample_size(f) * n;
	history = g_realloc(history, history_size);
	history_reset();
	pthread_mutex_lock(&lock);
	set_delay_locked(0, FALSE);
	pthread_mutex_unlock(&lock);
	return TRUE;
}

/* Closed, what it holds dropped if asked: it has played out otherwise.
 * A block being written goes with it. */
static void device_close(gboolean drop)
{
	if (state == DEV_PLAYING || state == DEV_PAUSED || state == DEV_PLAYING_OUT)
	{
		if (drop && dev_drv->flush)
			dev_drv->flush();
		if (dev_drv->close)
			dev_drv->close();
		closed_drv = dev_drv;
		gettimeofday(&closed_at, NULL);
	}
	state = DEV_CLOSED;
	dev_drv = NULL;
	out.active = held.active = FALSE;
	resampler_reset(rs);
	history_reset();
	pthread_mutex_lock(&lock);
	in_flight = 0;
	set_delay_locked(0, FALSE);
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
}

/* The device the song wants, with the driver chosen now; if it won't
 * open, tried again later. */
static void try_open(void)
{
	OutputDriver *d;
	AFormat f;
	gint r, n;
	gboolean p;

	pthread_mutex_lock(&lock);
	d = drv;
	want_drv = d;
	f = want_fmt;
	r = want_rate;
	n = want_nch;
	p = paused;
	pthread_mutex_unlock(&lock);

	/* Another driver after one that lingers: the card isn't free yet. */
	if (d && closed_drv && d != closed_drv && (closed_drv->flags & OUTPUT_DRIVER_LINGERS) &&
	    ms_since(&closed_at) < HANDOVER_MS)
	{
		if (state != DEV_OPENING)
			g_message("output: waiting for %s to let the card go",
				  driver_name(closed_drv));
		state = DEV_OPENING;
		retry_at = closed_at;
		retry_ms = HANDOVER_MS;
		return;
	}
	if (d && device_open(d, f, r, n))
	{
		state = DEV_PLAYING;
		if (p)
		{
			if (d->pause)
				d->pause(TRUE);
			state = DEV_PAUSED;
		}
		return;
	}
	retry_ms = state == DEV_OPENING ? CLAMP(retry_ms * 2, RETRY_FIRST_MS, RETRY_MAX_MS)
		: RETRY_FIRST_MS;
	state = DEV_OPENING;
	gettimeofday(&retry_at, NULL);
}

/* A device that failed or stalled: what it held is dropped, and it is
 * opened again as if it never had been. */
static void device_lost(const gchar *what)
{
	g_warning("output: %s %s; opening it again", driver_name(dev_drv), what);
	device_close(TRUE);
	state = DEV_OPENING;
	retry_ms = RETRY_FIRST_MS;
	gettimeofday(&retry_at, NULL);
}

/* The next block from the ring, processed, to be written. FALSE if
 * there is none to take. */
static gboolean take_block(void)
{
	gint block, n, first, length;
	guint gen;
	AFormat fmt;
	gint rate, nch;
	void *data;

	pthread_mutex_lock(&lock);
	if (used <= 0 || prebuffering)
	{
		pthread_mutex_unlock(&lock);
		return FALSE;
	}
	block = MAX(in_bps * BLOCK_MS / 1000 / in_frame, 1) * in_frame;
	n = MIN(used, block);
	n -= n % in_frame;
	if (n <= 0)
	{
		pthread_mutex_unlock(&lock);
		return FALSE;
	}
	if (work_size < n)
	{
		work_size = n;
		work = g_realloc(work, work_size);
	}
	first = MIN(n, ring_size - rd);
	memcpy(work, ring + rd, first);
	memcpy(work + first, ring, n - first);
	rd = (rd + n) % ring_size;
	used -= n;
	in_flight = n;
	gen = generation;
	fmt = in_fmt;
	rate = in_rate;
	nch = in_nch;
	pthread_cond_broadcast(&cond);		/* room in the ring */
	pthread_mutex_unlock(&lock);

	/* Another stream, or another place in it: the equalizer starts afresh. */
	if (gen != eq_generation)
	{
		eq_filter_reset();
		eq_generation = gen;
	}
	data = work;
	length = process_block(&data, n, fmt, rate, nch);
	out.active = TRUE;
	out.data = data;
	out.length = MAX(length, 0);
	out.offset = 0;
	out.in_bytes = n;
	out.gen = gen;
	return TRUE;
}

/* What the converter holds, to be written: the stream ends here. FALSE if
 * it holds none. */
static gboolean take_drain(void)
{
	void *data;
	gint nch = resampler_channels(rs);
	gint length = resampler_drain(rs, &data);

	if (length <= 0)
		return FALSE;
	length = device_format(&data, length, FMT_S16_NE, nch);
	out.active = TRUE;
	out.data = data;
	out.length = MAX(length, 0);
	out.offset = 0;
	out.in_bytes = 0;
	out.gen = 0;
	return TRUE;
}

/* Some of the block to the device. FALSE if there was nothing to write. */
static gboolean write_some(void)
{
	gint taken = 0, d;
	gboolean done;

	if (!out.active && !take_block())
		return FALSE;
	if (out.offset < out.length)
	{
		taken = dev_drv->write(out.data + out.offset, out.length - out.offset);
		if (taken < 0 && !driver_idles(dev_drv))
		{
			/* A disk writer's file isn't begun again: what it couldn't take
			 * is gone, and it is said once a song. */
			if (!write_failed)
				g_warning("output: %s couldn't write (%s); the rest of the song is lost",
					  driver_name(dev_drv), g_strerror(-taken));
			write_failed = TRUE;
			out.offset = out.length;
		}
		else if (taken < 0)
		{
			device_lost("stopped playing");
			return TRUE;
		}
		else if (taken == 0)
		{
			if (!stalling)
			{
				stalling = TRUE;
				gettimeofday(&stall_since, NULL);
			}
			else if (ms_since(&stall_since) >= STALL_MS)
			{
				device_lost("has taken nothing for 3 seconds");
				return TRUE;
			}
		}
		else
		{
			stalling = FALSE;
			start_sent = FALSE;
			history_add(out.data + out.offset, taken);
			out.offset += taken;
		}
	}
	done = out.offset >= out.length;
	/* What the converter holds is as far off as what the device does. */
	d = (dev_drv->delay ? dev_drv->delay() : 0) + resampler_held_ms(rs);

	pthread_mutex_lock(&lock);
	if (done)
	{
		if (out.in_bytes > 0)
		{
			if (out.gen == generation)
				delivered += out.in_bytes;
			in_flight = 0;
		}
		out.active = FALSE;
		/* A replay done, the block it went before goes on. */
		if (held.active)
		{
			out = held;
			held.active = FALSE;
		}
	}
	set_delay_locked(d, TRUE);
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
	return TRUE;
}

static void *pipeline_loop(void *arg)
{
	(void) arg;
	pthread_mutex_lock(&lock);
	while (!quit)
	{
		gboolean drop, close, want_pause, want_dev, have_stream, mismatch = FALSE, busy = FALSE;
		OutputDriver *wd, *chosen;
		AFormat wf;
		gint wr, wn, wait_ms = 10;
		guint wserial, gen;

		if (used > 0 && prebuffering &&
		    (used >= prebuffer_bytes || ms_since(&last_write) >= PREBUFFER_IDLE_MS))
			prebuffering = FALSE;
		drop = req_drop;
		close = req_close;
		req_drop = req_close = FALSE;
		want_pause = paused;
		have_stream = stream_open;
		/* A song open, or the end of one still to play. */
		want_dev = want_valid && (stream_open || used > 0);
		wd = want_drv;
		chosen = drv;
		wf = want_fmt;
		wr = want_rate;
		wn = want_nch;
		wserial = want_serial;
		gen = generation;
		pthread_mutex_unlock(&lock);

		/* A seek or a stop: what the device holds goes. A stop lets the
		 * device go, but one that can idle is kept for what comes next. */
		if (drop || close)
		{
			gboolean open = state == DEV_PLAYING || state == DEV_PAUSED ||
				state == DEV_PLAYING_OUT;

			out.active = held.active = FALSE;
			resampler_reset(rs);
			if (close && (!open || !driver_idles(dev_drv)))
				device_close(TRUE);
			else if (open)
			{
				if (dev_drv->flush)
					dev_drv->flush();
				history_reset();
				stalling = FALSE;
				start_sent = FALSE;
				pthread_mutex_lock(&lock);
				set_delay_locked(0, FALSE);
				pthread_mutex_unlock(&lock);
				if (close)
					state = DEV_PLAYING_OUT;
			}
		}

		/* Another device for the next song: another driver or format, or
		 * a driver that has a device per song. */
		if (want_dev && dev_drv &&
		    (state == DEV_PLAYING || state == DEV_PAUSED || state == DEV_PLAYING_OUT))
			mismatch = dev_drv != wd || dev_want_fmt != wf || dev_want_rate != wr ||
				dev_want_nch != wn || (!driver_idles(dev_drv) && dev_serial != wserial);

		/* Pause and resume. */
		if (want_pause && state == DEV_PLAYING)
		{
			gint dropped = dev_drv->pause ? dev_drv->pause(TRUE) : 0;

			replay_bytes = MAX(dropped, 0) * sample_size(dev_fmt) * dev_nch;
			state = DEV_PAUSED;
			stalling = FALSE;
		}
		else if (!want_pause && state == DEV_PAUSED)
		{
			if (dev_drv->pause)
				dev_drv->pause(FALSE);
			if (replay_bytes > 0)
				replay_last(replay_bytes, gen);
			replay_bytes = 0;
			state = DEV_PLAYING;
			stalling = FALSE;
		}

		switch (state)
		{
		case DEV_CLOSED:
			if (want_dev)
			{
				try_open();
				if (state != DEV_OPENING)
					dev_serial = wserial;
				busy = TRUE;
			}
			else if (chosen && driver_idles(chosen) && chosen != idle_tried)
			{
				/* Nothing to play: opened anyway, so that it has set
				 * itself up before the first song. Tried once; a song
				 * that wants it tries for as long as it takes. */
				gint rate = driver_rate(chosen, NULL);

				idle_tried = chosen;
				if (device_open(chosen, FMT_S16_NE, rate ? rate : IDLE_RATE, IDLE_NCH))
					state = DEV_PLAYING_OUT;
				busy = TRUE;
			}
			break;

		case DEV_OPENING:
			if (!want_dev)
				state = DEV_CLOSED;
			else if (ms_since(&retry_at) >= retry_ms)
			{
				try_open();
				if (state != DEV_OPENING)
					dev_serial = wserial;
				busy = TRUE;
			}
			break;

		case DEV_PLAYING:
			if (mismatch && !out.active)
			{
				/* The next song is for another device: this one plays out. */
				if (dev_drv->start)
					dev_drv->start();
				state = DEV_PLAYING_OUT;
				busy = TRUE;
			}
			else if (write_some())
				busy = TRUE;
			else
			{
				/* Nothing to write: what the device holds is heard. */
				if (!start_sent && dev_drv->start)
					dev_drv->start();
				start_sent = TRUE;
				if (!have_stream)
					state = DEV_PLAYING_OUT;
			}
			break;

		case DEV_PLAYING_OUT:
		{
			gint held_ms = dev_drv->delay ? dev_drv->delay() : 0;

			pthread_mutex_lock(&lock);
			set_delay_locked(held_ms + resampler_held_ms(rs), TRUE);
			pthread_mutex_unlock(&lock);
			if (want_dev && !mismatch)
			{
				/* The next song, same device and format: no gap. */
				state = DEV_PLAYING;
				busy = TRUE;
			}
			else if (resampler_holding(rs) && (want_dev || held_ms < DRAIN_MS) &&
				 take_drain())
			{
				/* The converter's last few ms, before the device runs out
				 * or goes to another song. */
				state = DEV_PLAYING;
				busy = TRUE;
			}
			else if (held_ms == 0 &&
				 (want_dev || !driver_idles(dev_drv) || chosen != dev_drv))
			{
				/* Heard, and a song wants another device or format,
				 * or another driver was chosen, or this one can't idle. */
				device_close(FALSE);
				busy = want_dev;
			}
			else if (held_ms == 0)
				/* Open with nothing to play, till something is asked. */
				wait_ms = IDLE_POLL_MS;
			break;
		}

		case DEV_PAUSED:
			break;
		}

		pthread_mutex_lock(&lock);
		if (!busy && !quit && !req_drop && !req_close)
		{
			struct timespec ts;
			struct timeval now;

			gettimeofday(&now, NULL);
			ts.tv_sec = now.tv_sec;
			ts.tv_nsec = (now.tv_usec + wait_ms * 1000L) * 1000L;
			while (ts.tv_nsec >= 1000000000L)
			{
				ts.tv_sec++;
				ts.tv_nsec -= 1000000000L;
			}
			pthread_cond_timedwait(&cond, &lock, &ts);
		}
	}
	pthread_mutex_unlock(&lock);

	/* Quitting: what the device holds goes with it. */
	device_close(TRUE);
	return NULL;
}

/* ---- what input plugins write to: these only ask ---- */

static void stream_reset_locked(void)
{
	interrupted = FALSE;
	rd = used = 0;
	in_flight = 0;
	written = delivered = 0;
	generation++;
	prebuffering = TRUE;
	free_calls = 0;
	gettimeofday(&last_write, NULL);
	pthread_cond_broadcast(&cond);
}

static gint pipeline_open(AFormat fmt, gint rate, gint nch)
{
	AFormat wf;
	gint wr = rate, wn = nch, size, buffer_ms = DEFAULT_BUFFER_MS, prebuffer = DEFAULT_PREBUFFER, fixed;
	OutputDriver *d;

	if (rate <= 0 || nch <= 0)
		return 0;
	wanted_format(&wf, &wr, &wn);

	pthread_mutex_lock(&lock);
	d = drv;
	/* No driver at all is the one thing a song can't wait for. */
	if (!d)
	{
		pthread_mutex_unlock(&lock);
		return 0;
	}
	if (d->get_buffering)
		d->get_buffering(&buffer_ms, &prebuffer);
	/* A driver with a rate of its own keeps its device at it, in stereo:
	 * the song is converted, and the device isn't opened again for it. */
	if ((fixed = driver_rate(d, NULL)) > 0)
	{
		wr = fixed;
		wn = 2;
	}
	in_fmt = fmt;
	in_rate = rate;
	in_nch = nch;
	in_frame = sample_size(fmt) * nch;
	in_bps = in_frame * rate;
	size = MAX(buffer_ms, 200) * (in_bps / 1000);
	size -= size % in_frame;
	size = MAX(size, in_frame * 1024);
	if (size != ring_size)
	{
		g_free(ring);
		ring = g_malloc(size);
		ring_size = size;
	}
	prebuffer_bytes = (gint) ((gint64) ring_size * CLAMP(prebuffer, 0, 90) / 100);
	prebuffer_bytes -= prebuffer_bytes % in_frame;
	stream_reset_locked();
	base_time = 0;
	paused = FALSE;
	stream_open = TRUE;
	song_ended = FALSE;
	/* The device this song wants: the thread sees to it. */
	want_valid = TRUE;
	want_drv = d;
	want_fmt = wf;
	want_rate = wr;
	want_nch = wn;
	want_serial++;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
	return 1;
}
static void pipeline_write(void *ptr, gint length)
{
	guchar *p = ptr;

	pthread_mutex_lock(&lock);
	while (length > 0 && stream_open && !quit)
	{
		gint room = ring_size - used, n, wr, first;

		if (room <= 0)
		{
			/* A plugin that wrote without asking for room: wait for it. */
			prebuffering = FALSE;
			pthread_cond_broadcast(&cond);
			pthread_cond_wait(&cond, &lock);
			continue;
		}
		n = MIN(length, room);
		wr = (rd + used) % ring_size;
		first = MIN(n, ring_size - wr);
		memcpy(ring + wr, p, first);
		memcpy(ring, p + first, n - first);
		used += n;
		written += n;
		p += n;
		length -= n;
	}
	gettimeofday(&last_write, NULL);
	free_calls = 0;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
}


static void pipeline_close(void)
{
	pthread_mutex_lock(&lock);
	if (!stream_open)
	{
		pthread_mutex_unlock(&lock);
		return;
	}
	if (song_ended)
		/* What the song wrote plays out; the next may follow it. */
		prebuffering = FALSE;
	else
	{
		/* Stopped or skipped: dropped, and the device closed. */
		stream_reset_locked();
		want_valid = FALSE;
		req_drop = req_close = TRUE;
	}
	song_ended = FALSE;
	stream_open = FALSE;
	paused = FALSE;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
}

static void pipeline_flush(gint time)
{
	pthread_mutex_lock(&lock);
	stream_reset_locked();
	base_time = time;
	if (paused)
		pause_time = time;
	req_drop = TRUE;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
}

static void pipeline_pause(short p)
{
	pthread_mutex_lock(&lock);
	if (!stream_open || !p == !paused)
	{
		pthread_mutex_unlock(&lock);
		return;
	}
	if (p)
	{
		pause_time = output_time_locked();
		/* What the device holds is heard after it resumes. */
		dev_delay = delay_now_locked();
		paused = TRUE;
	}
	else
	{
		paused = FALSE;
		gettimeofday(&dev_delay_at, NULL);	/* counts down again from now */
	}
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
}

static gint pipeline_buffer_free(void)
{
	gint room;

	pthread_mutex_lock(&lock);
	room = stream_open ? ring_size - used : 0;
	/* Asked twice without writing: the plugin has no more for now. */
	if (++free_calls >= 2 && prebuffering && used > 0)
	{
		prebuffering = FALSE;
		pthread_cond_broadcast(&cond);
	}
	pthread_mutex_unlock(&lock);
	return room;
}

/*
 * Whether there is audio still to give the device. An input plugin
 * waits for this to go FALSE before it closes at the end of a song: the
 * song's last audio is then on the device, and the next one can follow
 * it. A paused song is still playing.
 */
static gint pipeline_buffer_playing(void)
{
	gint playing;

	pthread_mutex_lock(&lock);
	if (prebuffering && used > 0)
	{
		prebuffering = FALSE;
		pthread_cond_broadcast(&cond);
	}
	playing = stream_open && (paused || used > 0 || in_flight > 0);
	pthread_mutex_unlock(&lock);
	return playing;
}

static gint pipeline_output_time(void)
{
	gint t;

	pthread_mutex_lock(&lock);
	t = output_time_locked();
	pthread_mutex_unlock(&lock);
	return t;
}

static gint pipeline_written_time(void)
{
	gint t;

	pthread_mutex_lock(&lock);
	t = base_time + bytes_to_ms(written);
	pthread_mutex_unlock(&lock);
	return t;
}

static OutputPlugin pipeline_plugin =
{
	NULL,
	NULL,
	"XMMS output pipeline",
	NULL,			/* init */
	NULL,			/* about */
	NULL,			/* configure */
	output_get_volume,
	output_set_volume,
	pipeline_open,
	pipeline_write,
	pipeline_close,
	pipeline_flush,
	pipeline_pause,
	pipeline_buffer_free,
	pipeline_buffer_playing,
	pipeline_output_time,
	pipeline_written_time,
	NULL,			/* cleanup */
};

/* ---- XMMS's side ---- */

OutputPlugin *output_pipeline(void)
{
	return &pipeline_plugin;
}

/* The song now playing has played out, as far as the input plugin goes:
 * its end is to be heard, not dropped, when the plugin closes. */
void output_song_ended(void)
{
	pthread_mutex_lock(&lock);
	if (stream_open)
		song_ended = TRUE;
	pthread_mutex_unlock(&lock);
}

/*
 * Another driver chosen. It takes over when it would cut nothing off:
 * at the next song, once the device has played the last one out, or
 * after a stop. A device that is still being tried is tried with it.
 */
void output_set_driver(OutputDriver *d)
{
	pthread_mutex_lock(&lock);
	drv = d;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
}

void output_start(void)
{
	pthread_mutex_lock(&lock);
	if (thread_up)
	{
		pthread_mutex_unlock(&lock);
		return;
	}
	cbuf = xmms_convert_buffers_new();
	rs = resampler_new();
	quit = FALSE;
	thread_up = pthread_create(&thread, NULL, pipeline_loop, NULL) == 0;
	pthread_mutex_unlock(&lock);
}

/* Quitting: the thread drops what the device holds, closes it and ends,
 * which takes at most about one write's wait. */
void output_stop(void)
{
	pthread_mutex_lock(&lock);
	if (!thread_up)
	{
		pthread_mutex_unlock(&lock);
		return;
	}
	quit = TRUE;
	stream_open = FALSE;
	want_valid = FALSE;
	req_drop = req_close = TRUE;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
	pthread_join(thread, NULL);

	pthread_mutex_lock(&lock);
	thread_up = FALSE;
	xmms_convert_buffers_destroy(cbuf);
	cbuf = NULL;
	resampler_free(rs);
	rs = NULL;
	g_free(work);
	work = NULL;
	work_size = 0;
	g_free(ring);
	ring = NULL;
	ring_size = 0;
	g_free(history);
	history = NULL;
	history_size = 0;
	g_free(replay);
	replay = NULL;
	pthread_mutex_unlock(&lock);
}

/*
 * A seek or a stop is about to be asked of the input plugin: one waiting
 * in output_pass_audio() for room, as it would be for ever while paused,
 * goes on, and so it does till the plugin flushes or closes the output.
 */
void output_interrupt(void)
{
	pthread_mutex_lock(&lock);
	interrupted = TRUE;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&lock);
}

void output_interrupt_done(void)
{
	pthread_mutex_lock(&lock);
	interrupted = FALSE;
	pthread_mutex_unlock(&lock);
}

/* The audio, written once there is room for it. FALSE if interrupted. */
gboolean output_pass_audio(void *data, gint length)
{
	pthread_mutex_lock(&lock);
	while (stream_open && !quit && !interrupted &&
	       ring_size - used < MIN(length, ring_size))
	{
		/* Full, and waiting to start: it starts now. */
		if (prebuffering)
			prebuffering = FALSE;
		pthread_cond_broadcast(&cond);
		pthread_cond_wait(&cond, &lock);
	}
	if (!stream_open || quit || interrupted)
	{
		pthread_mutex_unlock(&lock);
		return FALSE;
	}
	pthread_mutex_unlock(&lock);
	pipeline_write(data, length);
	return TRUE;
}
