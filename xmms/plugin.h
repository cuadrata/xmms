/*  XMMS - Cross-platform multimedia player
 *  Copyright (C) 1998-2000  Peter Alm, Mikael Alm, Olle Hallnas, Thomas Nilsson and 4Front Technologies
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef PLUGIN_H
#define PLUGIN_H

#include <glib.h>

typedef enum
{
	FMT_U8, FMT_S8, FMT_U16_LE, FMT_U16_BE, FMT_U16_NE, FMT_S16_LE, FMT_S16_BE, FMT_S16_NE
}
AFormat;

typedef struct
{
	void *handle;		/* Filled in by xmms */
	char *filename;		/* Filled in by xmms */
	char *description;	/* The description that is shown in the preferences box */
	void (*init) (void);
	void (*about) (void);	/* Show the about box */
	void (*configure) (void);	/* Show the configuration dialog */
	void (*get_volume) (int *l, int *r);
	void (*set_volume) (int l, int r);	/* Set the volume */
	int (*open_audio) (AFormat fmt, int rate, int nch);	/* Open the device, if the device can't handle the given 
								   parameters the plugin is responsible for downmixing
								   the data to the right format before outputting it */
	void (*write_audio) (void *ptr, int length);	/* The input plugin calls this to write data to the output 
							   buffer */
	void (*close_audio) (void);	/* No comment... */
	void (*flush) (int time);	/* Flush the buffer and set the plugins internal timers to time */
	void (*pause) (short paused);	/* Pause or unpause the output */
	int (*buffer_free) (void);	/* Return the amount of data that can be written to the buffer,
					   two calls to this without a call to write_audio should make
					   the plugin output audio directly */
	int (*buffer_playing) (void);	/* Returns TRUE if the plugin currently is playing some audio,
					   otherwise return FALSE */
	int (*output_time) (void);	/* Return the current playing time */
	int (*written_time) (void);	/* Return the length of all the data that has been written to
					   the buffer */
	void (*cleanup) (void);	/* Called when the plugin is unloaded */
}
OutputPlugin;

/*
 * The OutputPlugin above is what an input plugin writes its audio to;
 * XMMS gives it one of its own, its output pipeline. The pipeline
 * buffers the audio, runs the equalizer, the effect plugins and the
 * volume over it, converts it to the format the device took, and keeps
 * the time, then gives it to an output driver, which only plays it.
 * An output plugin is a driver; volume is XMMS's, never the device's.
 *
 * Only the pipeline's own thread calls a driver, one call at a time, and
 * no call may wait more than about 100 ms: a busy device fails to open
 * at once, and a full one takes part of a write, or none, and says so.
 * Failures are reported by what the calls return, never printed: XMMS
 * says what went wrong, once.
 */
#define OUTPUT_DRIVER_VERSION	4

/* Driver flags */
#define OUTPUT_DRIVER_PER_SONG	(1 << 0)	/* opened and closed for each song, as
						   the disk writer is: never shared
						   between songs */
#define OUTPUT_DRIVER_NO_CLOCK	(1 << 1)	/* takes audio as fast as it is given,
						   not as fast as it plays */
#define OUTPUT_DRIVER_LINGERS	(1 << 2)	/* the card may stay held a few seconds
						   after closing, as a sound server
						   holds it: another driver waits */

/* How well songs at another rate are converted to the device's */
#define OUTPUT_QUALITY_BEST	0
#define OUTPUT_QUALITY_MEDIUM	1	/* for very high rates or slow machines */

typedef struct
{
	void *handle;		/* Filled in by xmms */
	char *filename;		/* Filled in by xmms */
	char *description;	/* The description that is shown in the preferences box */
	int version;		/* OUTPUT_DRIVER_VERSION */
	int flags;		/* OUTPUT_DRIVER_* */
	void (*init) (void);	/* Called when the plugin is loaded */
	void (*cleanup) (void);	/* Called when the plugin is unloaded */
	void (*about) (void);
	void (*configure) (void);
	/*
	 * Open the device for this format, failing at once if it is busy or
	 * missing: never wait for it. A device that can't take the format
	 * may settle on another, as near as it can, and say which: XMMS
	 * converts. 0, or a negative errno-style code.
	 */
	int (*open) (AFormat *fmt, int *rate, int *nch);
	/*
	 * Play this audio: wait at most about 100 ms for room, then take what
	 * fits. The bytes taken, 0 if there was no room yet, or negative if
	 * the device is lost.
	 */
	int (*write) (void *data, int length);
	/* What the device holds, in milliseconds: how long until audio
	 * written now is heard. Never waits. */
	int (*delay) (void);
	/*
	 * Play what it holds now, though less than it waits for before it
	 * starts: nothing more is coming for now. Never waits. May be NULL.
	 */
	void (*start) (void);
	/*
	 * Pause or go on. A device that can't hold what it has while paused
	 * drops it: return how many frames it dropped, and XMMS plays them
	 * again when it goes on. 0 if it held them. Never waits.
	 */
	int (*pause) (int paused);
	void (*flush) (void);	/* Drop what the device holds, at once */
	void (*close) (void);	/* Let the device go; it was flushed or has played out */
	/*
	 * XMMS's buffer, ahead of the device: how much audio it holds, in
	 * milliseconds, and how full it gets before a song starts, in percent.
	 * Asked as each song starts. NULL for 3000 ms and 25%.
	 */
	void (*get_buffering) (int *buffer_ms, int *prebuffer);
	/*
	 * The rate to open the device at, whatever the song's, which XMMS
	 * converts to, and how well (OUTPUT_QUALITY_*). With a rate, the
	 * device is opened in stereo too. Rate 0 for each song's own rate
	 * and channels, opening the device again when they change. Asked as
	 * each song starts and before the first. NULL for each song's own.
	 */
	void (*get_rate) (int *rate, int *quality);
}
OutputDriver;

typedef struct
{
	void *handle;		/* Filled in by xmms */
	char *filename;		/* Filled in by xmms */
	char *description;	/* The description that is shown in the preferences box */
	void (*init) (void);	/* Called when the plugin is loaded */
	void (*cleanup) (void);	/* Called when the plugin is unloaded */
	void (*about) (void);	/* Show the about box */
	void (*configure) (void);	/* Show the configure box */
	int (*mod_samples) (gpointer *data, gint length, AFormat fmt, gint srate, gint nch);	/* Modify samples */
	void (*query_format) (AFormat *fmt,gint *rate, gint *nch);
}
EffectPlugin;

typedef enum
{
	INPUT_VIS_ANALYZER, INPUT_VIS_SCOPE, INPUT_VIS_VU, INPUT_VIS_OFF
}
InputVisType;

typedef struct
{
	void *handle;		/* Filled in by xmms */
	char *filename;		/* Filled in by xmms */
	char *description;	/* The description that is shown in the preferences box */
	void (*init) (void);	/* Called when the plugin is loaded */
	void (*about) (void);	/* Show the about box */
	void (*configure) (void);
	int (*is_our_file) (char *filename);	/* Return 1 if the plugin can handle the file */
	GList *(*scan_dir) (char *dirname);	/* The files to play for a directory the plugin
						   owns, or NULL: nothing in the tree uses it now */
	void (*play_file) (char *filename);	/* Guess what... */
	void (*stop) (void);	/* Tricky one */
	void (*pause) (short paused);	/* Pause or unpause */
	void (*seek) (int time);	/* Seek to the specified time */
	void (*set_eq) (int on, float preamp, float *bands);	/* Set the equalizer, most plugins won't be able to do this */
	int (*get_time) (void);	/* Get the time, usually returns the output plugins output time */
	void (*get_volume) (int *l, int *r);	/* Input-plugin specific volume functions, just provide a NULL if */
	void (*set_volume) (int l, int r);	/*  you want the output plugin to handle it */
	void (*cleanup) (void);			/* Called when xmms exit */
	InputVisType (*get_vis_type) (void); /* OBSOLETE, DO NOT USE! */
	void (*add_vis_pcm) (int time, AFormat fmt, int nch, int length, void *ptr); /* Send data to the visualization plugins 
											Preferably 512 samples/block */
	void (*set_info) (char *title, int length, int rate, int freq, int nch);	/* Fill in the stuff that is shown in the player window
											   set length to -1 if it's unknown. Filled in by xmms */
	void (*set_info_text) (char *text);	/* Show some text in the song title box in the main window,
						   call it with NULL as argument to reset it to the song title.
						   Filled in by xmms */
	void (*get_song_info) (char *filename, char **title, int *length);	/* Function to grab the title string */
	void (*file_info_box) (char *filename);		/* Bring up an info window for the filename passed in */
	OutputPlugin *output;	/* Handle to the current output plugin. Filled in by xmms */
	/*
	 * Filled in by xmms: play decoded audio. It goes to the
	 * visualization plugins and is written to the output as soon as
	 * there is room, waiting till then. A seek or a stop interrupts the
	 * wait: then it returns FALSE, having written nothing, and does so
	 * till the plugin flushes or closes the output. This takes the place
	 * of calling add_vis_pcm(), then buffer_free() till there is room,
	 * then write_audio().
	 */
	int (*pass_audio) (AFormat fmt, int nch, int length, void *data);
}
InputPlugin;

/* So that input plugins can get the title formatting information */
gchar *xmms_get_gentitle_format(void);

/* So that an output plugin can tell whether it is the one in use */
OutputDriver *get_current_output_plugin(void);

/* So that output plugins can communicate with effect plugins */
EffectPlugin *get_current_effect_plugin(void);
int effects_enabled(void);

/*
 * Output plugins call this on the samples they are about to write to the
 * device, to apply XMMS's volume and balance (in place).
 */
void xmms_apply_volume(gpointer data, gint length, AFormat fmt, gint channels);

typedef struct
{
	void *handle;		/* Filled in by xmms */
	char *filename;		/* Filled in by xmms */
	int xmms_session;	/* The session ID for attaching to the control socket */
	char *description;	/* The description that is shown in the preferences box */
	void (*init) (void);	/* Called when the plugin is enabled */
	void (*about) (void);	/* Show the about box */
	void (*configure) (void);
	void (*cleanup) (void);	/* Called when the plugin is disabled or when xmms exits */
}
GeneralPlugin;

typedef struct _VisPlugin
{
	void *handle; 	/* Filled in by xmms */
	char *filename; /* Filled in by xmms */
	int xmms_session; /* The session ID for attaching to the control socket */
	char *description; /* The description that is shown in the preferences box */
	int num_pcm_chs_wanted; /* Numbers of PCM channels wanted in the call to render_pcm */
	int num_freq_chs_wanted; /* Numbers of freq channels wanted in the call to render_freq */
	void (*init)(void); /* Called when the plugin is enabled */
	void (*cleanup)(void); /* Called when the plugin is disabled */
	void (*about)(void); /* Show the about box */
	void (*configure)(void); /* Show the configure box */
	void (*disable_plugin)(struct _VisPlugin *); /* Call this with a pointer to your plugin to disable the plugin */
	void (*playback_start)(void); /* Called when playback starts */
	void (*playback_stop)(void); /* Called when playback stops */
	void (*render_pcm)(gint16 pcm_data[2][512]); /* Render the PCM data, don't do anything time consuming in here */
	void (*render_freq)(gint16 freq_data[2][256]); /* Render the freq data, don't do anything time consuming in here */
} VisPlugin;

/* The entry point a plugin exports, one for each kind of plugin. */
OutputDriver *get_output_driver_info(void);
EffectPlugin *get_eplugin_info(void);
InputPlugin *get_iplugin_info(void);
GeneralPlugin *get_gplugin_info(void);
VisPlugin *get_vplugin_info(void);

#endif
