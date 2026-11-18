#ifndef __VORBIS_H__
#define __VORBIS_H__

void vorbis_configure(void);

void vorbis_file_info_box(char *filename);

typedef struct {
	gboolean tag_override;
	gchar *tag_format;
	gboolean use_anticlip;
	gboolean use_replaygain;
	gint replaygain_mode;
	gboolean use_booster;
} vorbis_config_t;

enum {
	REPLAYGAIN_MODE_TRACK,
	REPLAYGAIN_MODE_ALBUM,
	REPLAYGAIN_MODE_LAST
};

#endif  /* __VORBIS_H__ */
