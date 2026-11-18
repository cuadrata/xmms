#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "httpstream.h"
#include "mpg123-plugin.h"
#include "libxmms/util.h"
#include "xmms/i18n.h"
#include "libxmms/configfile.h"
#include "libxmms/dirbrowser.h"
#include "libxmms/titlestring.h"

static GtkWidget *mpg123_configurewin = NULL;
static GtkWidget *vbox, *notebook;
static GtkWidget *decode_vbox, *decode_hbox1;
static GtkWidget *decode_res_frame, *decode_res_vbox, *decode_res_16, *decode_res_8;
static GtkWidget *decode_ch_frame, *decode_ch_vbox, *decode_ch_stereo, *decode_ch_mono;
static GtkWidget *decode_freq_frame, *decode_freq_vbox, *decode_freq_1to1,
         *decode_freq_1to2, *decode_freq_1to4;
static GtkWidget *option_frame, *option_vbox, *detect_by_content, *detect_by_extension, *detect_by_both;

static GtkWidget *title_id3_box, *title_tag_desc;
static GtkWidget *title_override, *title_id3_entry, *title_id3v2_disable;


static void mpg123_configurewin_ok(GtkWidget * widget, gpointer data)
{
	ConfigFile *cfg;

	if (GTK_TOGGLE_BUTTON(decode_res_16)->active)
		mpg123_cfg.resolution = 16;
	else if (GTK_TOGGLE_BUTTON(decode_res_8)->active)
		mpg123_cfg.resolution = 8;

	if (GTK_TOGGLE_BUTTON(decode_ch_stereo)->active)
		mpg123_cfg.channels = 2;
	else if (GTK_TOGGLE_BUTTON(decode_ch_mono)->active)
		mpg123_cfg.channels = 1;

	if (GTK_TOGGLE_BUTTON(decode_freq_1to1)->active)
		mpg123_cfg.downsample = 0;
	else if (GTK_TOGGLE_BUTTON(decode_freq_1to2)->active)
		mpg123_cfg.downsample = 1;
	if (GTK_TOGGLE_BUTTON(decode_freq_1to4)->active)
		mpg123_cfg.downsample = 2;

	if (GTK_TOGGLE_BUTTON(detect_by_content)->active)
		mpg123_cfg.detect_by = DETECT_CONTENT;
	else if (GTK_TOGGLE_BUTTON(detect_by_extension)->active)
		mpg123_cfg.detect_by = DETECT_EXTENSION;
	else if (GTK_TOGGLE_BUTTON(detect_by_both)->active)
		mpg123_cfg.detect_by = DETECT_BOTH;
	else mpg123_cfg.detect_by = DETECT_EXTENSION;

	http_stream_config_page_apply();

	mpg123_cfg.title_override = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(title_override));
	mpg123_cfg.disable_id3v2 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(title_id3v2_disable));
	g_free(mpg123_cfg.id3_format);
	mpg123_cfg.id3_format = g_strdup(gtk_entry_get_text(GTK_ENTRY(title_id3_entry)));

	cfg = xmms_cfg_open_default_file();
	xmms_cfg_write_int(cfg, "MPG123", "resolution", mpg123_cfg.resolution);
	xmms_cfg_write_int(cfg, "MPG123", "channels", mpg123_cfg.channels);
	xmms_cfg_write_int(cfg, "MPG123", "downsample", mpg123_cfg.downsample);
	xmms_cfg_write_boolean(cfg, "MPG123", "title_override", mpg123_cfg.title_override);
	xmms_cfg_write_boolean(cfg, "MPG123", "disable_id3v2", mpg123_cfg.disable_id3v2);
	xmms_cfg_write_string(cfg, "MPG123", "id3_format", mpg123_cfg.id3_format);
	xmms_cfg_write_int(cfg, "MPG123", "detect_by", mpg123_cfg.detect_by);
	xmms_cfg_write_default_file(cfg);
	xmms_cfg_free(cfg);
	gtk_widget_destroy(mpg123_configurewin);
}


static void title_override_cb(GtkWidget * w, gpointer data)
{
	gboolean override;
	override = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(title_override));
	gtk_widget_set_sensitive(title_id3_box, override);
	gtk_widget_set_sensitive(title_tag_desc, override);
}

static void configure_destroy(GtkWidget * w, gpointer data)
{
}

void mpg123_configure(void)
{
	GtkWidget *title_frame, *title_id3_vbox, *title_id3_label;
	GtkWidget *bbox, *ok, *cancel;

	if (mpg123_configurewin != NULL)
	{
		gdk_window_raise(mpg123_configurewin->window);
		return;
	}
	mpg123_configurewin = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_signal_connect(GTK_OBJECT(mpg123_configurewin), "destroy", GTK_SIGNAL_FUNC(gtk_widget_destroyed), &mpg123_configurewin);
	gtk_signal_connect(GTK_OBJECT(mpg123_configurewin), "destroy", GTK_SIGNAL_FUNC(configure_destroy), &mpg123_configurewin);
	gtk_window_set_title(GTK_WINDOW(mpg123_configurewin), _("MPG123 Configuration"));
	gtk_window_set_policy(GTK_WINDOW(mpg123_configurewin), FALSE, FALSE, FALSE);
	/*  gtk_window_set_position(GTK_WINDOW(mpg123_configurewin), GTK_WIN_POS_MOUSE); */
	gtk_container_border_width(GTK_CONTAINER(mpg123_configurewin), 10);

	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(mpg123_configurewin), vbox);

	notebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

	decode_vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(decode_vbox), 5);

	decode_hbox1 = gtk_hbox_new(TRUE, 5);
	gtk_box_pack_start(GTK_BOX(decode_vbox), decode_hbox1, FALSE, FALSE, 0);

	decode_res_frame = gtk_frame_new(_("Resolution:"));
	gtk_box_pack_start(GTK_BOX(decode_hbox1), decode_res_frame, TRUE, TRUE, 0);

	decode_res_vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(decode_res_vbox), 5);
	gtk_container_add(GTK_CONTAINER(decode_res_frame), decode_res_vbox);

	decode_res_16 = gtk_radio_button_new_with_label(NULL, _("16 bit"));
	if (mpg123_cfg.resolution == 16)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(decode_res_16), TRUE);
	gtk_box_pack_start(GTK_BOX(decode_res_vbox), decode_res_16, FALSE, FALSE, 0);

	decode_res_8 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(decode_res_16)), _("8 bit"));
	if (mpg123_cfg.resolution == 8)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(decode_res_8), TRUE);

	gtk_box_pack_start(GTK_BOX(decode_res_vbox), decode_res_8, FALSE, FALSE, 0);

	decode_ch_frame = gtk_frame_new(_("Channels:"));
	gtk_box_pack_start(GTK_BOX(decode_hbox1), decode_ch_frame, TRUE, TRUE, 0);

	decode_ch_vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(decode_ch_vbox), 5);
	gtk_container_add(GTK_CONTAINER(decode_ch_frame), decode_ch_vbox);

	decode_ch_stereo = gtk_radio_button_new_with_label(NULL, _("Stereo (if available)"));
	if (mpg123_cfg.channels == 2)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(decode_ch_stereo), TRUE);

	gtk_box_pack_start(GTK_BOX(decode_ch_vbox), decode_ch_stereo, FALSE, FALSE, 0);

	decode_ch_mono = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(decode_ch_stereo)), _("Mono"));
	if (mpg123_cfg.channels == 1)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(decode_ch_mono), TRUE);

	gtk_box_pack_start(GTK_BOX(decode_ch_vbox), decode_ch_mono, FALSE, FALSE, 0);

	decode_freq_frame = gtk_frame_new(_("Down sample:"));
	gtk_box_pack_start(GTK_BOX(decode_vbox), decode_freq_frame, FALSE, FALSE, 0);

	decode_freq_vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(decode_freq_vbox), 5);
	gtk_container_add(GTK_CONTAINER(decode_freq_frame), decode_freq_vbox);

	decode_freq_1to1 = gtk_radio_button_new_with_label(NULL, _("1:1 (44 kHz)"));
	if (mpg123_cfg.downsample == 0)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(decode_freq_1to1), TRUE);
	gtk_box_pack_start(GTK_BOX(decode_freq_vbox), decode_freq_1to1, FALSE, FALSE, 0);

	decode_freq_1to2 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(decode_freq_1to1)), _("1:2 (22 kHz)"));
	if (mpg123_cfg.downsample == 1)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(decode_freq_1to2), TRUE);
	gtk_box_pack_start(GTK_BOX(decode_freq_vbox), decode_freq_1to2, FALSE, FALSE, 0);

	decode_freq_1to4 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(decode_freq_1to1)), _("1:4 (11 kHz)"));
	if (mpg123_cfg.downsample == 2)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(decode_freq_1to4), TRUE);

	gtk_box_pack_start(GTK_BOX(decode_freq_vbox), decode_freq_1to4, FALSE, FALSE, 0);


	option_frame = gtk_frame_new(_("Options"));
	gtk_box_pack_start(GTK_BOX(decode_vbox), option_frame, FALSE, FALSE, 0);

	option_vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(option_vbox), 5);
	gtk_container_add(GTK_CONTAINER(option_frame), option_vbox);

	detect_by_content = gtk_radio_button_new_with_label(NULL,
			_("Content"));

	detect_by_extension = gtk_radio_button_new_with_label(
			gtk_radio_button_group(GTK_RADIO_BUTTON(detect_by_content)),
			_("Extension"));

	detect_by_both = gtk_radio_button_new_with_label(
			gtk_radio_button_group(GTK_RADIO_BUTTON(detect_by_content)),
			_("Extension and content"));

	switch (mpg123_cfg.detect_by)
	{
		case DETECT_CONTENT:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(detect_by_content), TRUE);
			break;
		case DETECT_BOTH:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(detect_by_both), TRUE);
			break;
		case DETECT_EXTENSION:
		default:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(detect_by_extension), TRUE);
			break;
	}

	gtk_box_pack_start(GTK_BOX(option_vbox), detect_by_content, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(option_vbox), detect_by_extension, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(option_vbox), detect_by_both, FALSE, FALSE, 0);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), decode_vbox, gtk_label_new(_("Decoder")));

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), http_stream_config_page(), gtk_label_new(_("Streaming")));

	title_frame = gtk_frame_new(_("ID3 Tags:"));
	gtk_container_border_width(GTK_CONTAINER(title_frame), 5);

	title_id3_vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_border_width(GTK_CONTAINER(title_id3_vbox), 5);
	gtk_container_add(GTK_CONTAINER(title_frame), title_id3_vbox);

	title_id3v2_disable = gtk_check_button_new_with_label(_("Disable ID3V2 tags"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(title_id3v2_disable),
				     mpg123_cfg.disable_id3v2);
	gtk_box_pack_start(GTK_BOX(title_id3_vbox), title_id3v2_disable, FALSE, FALSE, 0);

	title_override = gtk_check_button_new_with_label(_("Override generic titles"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(title_override), mpg123_cfg.title_override);
	gtk_signal_connect(GTK_OBJECT(title_override), "clicked", GTK_SIGNAL_FUNC(title_override_cb), NULL);
	gtk_box_pack_start(GTK_BOX(title_id3_vbox), title_override, FALSE, FALSE, 0);

	title_id3_box = gtk_hbox_new(FALSE, 5);
	gtk_widget_set_sensitive(title_id3_box, mpg123_cfg.title_override);
	gtk_box_pack_start(GTK_BOX(title_id3_vbox), title_id3_box, FALSE, FALSE, 0);

	title_id3_label = gtk_label_new(_("ID3 format:"));
	gtk_box_pack_start(GTK_BOX(title_id3_box), title_id3_label, FALSE, FALSE, 0);

	title_id3_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(title_id3_entry), mpg123_cfg.id3_format);
	gtk_box_pack_start(GTK_BOX(title_id3_box), title_id3_entry, TRUE, TRUE, 0);

	title_tag_desc = xmms_titlestring_descriptions("pafFetnygc", 2);
	gtk_widget_set_sensitive(title_tag_desc, mpg123_cfg.title_override);
	gtk_box_pack_start(GTK_BOX(title_id3_vbox), title_tag_desc, FALSE, FALSE, 0);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), title_frame, gtk_label_new(_("Title")));

	bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
	gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

	ok = gtk_button_new_with_label(_("OK"));
	gtk_signal_connect(GTK_OBJECT(ok), "clicked", GTK_SIGNAL_FUNC(mpg123_configurewin_ok), NULL);
	GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), ok, TRUE, TRUE, 0);
	gtk_widget_grab_default(ok);

	cancel = gtk_button_new_with_label(_("Cancel"));
	gtk_signal_connect_object(GTK_OBJECT(cancel), "clicked", GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(mpg123_configurewin));
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), cancel, TRUE, TRUE, 0);

	gtk_widget_show_all(mpg123_configurewin);
}
