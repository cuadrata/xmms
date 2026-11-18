/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2002  Peter Alm, Mikael Alm, Olle Hallnas,
 *                           Thomas Nilsson and 4Front Technologies
 *  Copyright (C) 1999-2002  Haavard Kvaalen
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
#include <pthread.h>

struct OutputPluginData *op_data;

OutputDriver *get_current_output_plugin(void)
{
	return op_data->current_output_plugin;
}

void set_current_output_plugin(int i)
{
	GList *node = g_list_nth(op_data->output_list, i);
	if (node)
		op_data->current_output_plugin = node->data;
	else
		op_data->current_output_plugin = NULL;
	output_set_driver(op_data->current_output_plugin);
}

GList *get_output_list(void)
{
	return op_data->output_list;
}

void output_about(int i)
{
	OutputDriver *out = (OutputDriver *) g_list_nth(op_data->output_list, i)->data;
	if (out && out->about)
		out->about();
}

void output_configure(int i)
{
	OutputDriver *out = (OutputDriver *) g_list_nth(op_data->output_list, i)->data;
	if (out && out->configure)
		out->configure();
}

/*
 * Software volume.
 *
 * XMMS used to leave volume and balance to the output plugin, which drove a
 * hardware mixer (/dev/mixer, ALSA's PCM control). Where the plugin could
 * not attach one -- a sound server, a card without that control -- the
 * sliders did nothing, and balance, which starts from the current volume,
 * could not work either. Now XMMS keeps the volume itself, and the
 * output pipeline applies it with xmms_apply_volume() as it hands samples
 * to the device -- after its buffer, so a change is heard as soon as the
 * device's buffer has played out, not seconds later. Volume and balance
 * are per-application and never touch a hardware mixer. At 100% on both
 * channels the samples pass through untouched.
 *
 * The percentages map to gain with a cubic curve, as PulseAudio and
 * PipeWire do, so 50% is about -18 dB rather than a barely audible linear
 * half, and levels match what desktop mixers show.
 */
static gint soft_volume_l = 100, soft_volume_r = 100;
/* Set by the interface, read by the pipeline's thread. */
static pthread_mutex_t volume_lock = PTHREAD_MUTEX_INITIALIZER;

void output_get_volume(int *l, int *r)
{
	pthread_mutex_lock(&volume_lock);
	*l = soft_volume_l;
	*r = soft_volume_r;
	pthread_mutex_unlock(&volume_lock);
}

void output_set_volume(int l, int r)
{
	pthread_mutex_lock(&volume_lock);
	soft_volume_l = CLAMP(l, 0, 100);
	soft_volume_r = CLAMP(r, 0, 100);
	pthread_mutex_unlock(&volume_lock);
}

void output_read_volume_config(ConfigFile *cfgfile)
{
	gint v;

	pthread_mutex_lock(&volume_lock);
	if (xmms_cfg_read_int(cfgfile, "xmms", "volume_left", &v))
		soft_volume_l = CLAMP(v, 0, 100);
	if (xmms_cfg_read_int(cfgfile, "xmms", "volume_right", &v))
		soft_volume_r = CLAMP(v, 0, 100);
	pthread_mutex_unlock(&volume_lock);
}

void output_write_volume_config(ConfigFile *cfgfile)
{
	xmms_cfg_write_int(cfgfile, "xmms", "volume_left", soft_volume_l);
	xmms_cfg_write_int(cfgfile, "xmms", "volume_right", soft_volume_r);
}

/* Gain for a volume percentage, in 1/65536 units. */
static gint soft_gain(gint percent)
{
	gdouble v = percent / 100.0;

	return (gint) (v * v * v * 65536.0 + 0.5);
}

static gint soft_scale_sample(gint sample, gint gain)
{
	return (gint) (((gint64) sample * gain) / 65536);
}

static void soft_scale(guchar *data, gint length, AFormat fmt, gint channels,
		       gint gain_l, gint gain_r)
{
	/* Mono has no balance: use the louder channel's volume. */
	gint gain_m = MAX(gain_l, gain_r);
	gint nch = channels > 0 ? channels : 1;
	gint i, n, gain;

	if (fmt == FMT_U16_NE)
		fmt = G_BYTE_ORDER == G_LITTLE_ENDIAN ? FMT_U16_LE : FMT_U16_BE;
	if (fmt == FMT_S16_NE)
		fmt = G_BYTE_ORDER == G_LITTLE_ENDIAN ? FMT_S16_LE : FMT_S16_BE;

	n = (fmt == FMT_U8 || fmt == FMT_S8) ? length : length / 2;
	for (i = 0; i < n; i++)
	{
		gint ch = i % nch;
		gint s;

		gain = nch == 1 ? gain_m : ch == 0 ? gain_l : ch == 1 ? gain_r : gain_m;
		switch (fmt)
		{
		case FMT_U8:
			data[i] = (guchar) (soft_scale_sample(data[i] - 128, gain) + 128);
			break;
		case FMT_S8:
			data[i] = (guchar) soft_scale_sample((gint8) data[i], gain);
			break;
		case FMT_S16_LE:
		case FMT_U16_LE:
			s = data[2 * i] | (data[2 * i + 1] << 8);
			s = fmt == FMT_U16_LE ? s - 32768 : (gint16) s;
			s = soft_scale_sample(s, gain);
			if (fmt == FMT_U16_LE)
				s += 32768;
			data[2 * i] = s & 0xff;
			data[2 * i + 1] = (s >> 8) & 0xff;
			break;
		case FMT_S16_BE:
		case FMT_U16_BE:
			s = (data[2 * i] << 8) | data[2 * i + 1];
			s = fmt == FMT_U16_BE ? s - 32768 : (gint16) s;
			s = soft_scale_sample(s, gain);
			if (fmt == FMT_U16_BE)
				s += 32768;
			data[2 * i] = (s >> 8) & 0xff;
			data[2 * i + 1] = s & 0xff;
			break;
		default:
			return;
		}
	}
}

/*
 * Called by the output pipeline on the samples it is about to hand to
 * the device, in place. fmt and channels describe the data at that point.
 */
void xmms_apply_volume(gpointer data, gint length, AFormat fmt, gint channels)
{
	gint l, r;

	output_get_volume(&l, &r);

	if (l == 100 && r == 100)
		return;
	soft_scale(data, length, fmt, channels, soft_gain(l), soft_gain(r));
}
