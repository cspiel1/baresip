/**
 * @file comvideo.c  Commend specific video source
 *
 * Copyright (C) 2020 Commend.com
 */

/**
 * @defgroup comvideo comvideo
 *
 * Commend specific video source and h264 codec implementation using
 * GStreamer video codecs.
 *
 * The video stream is captured from the camerad process via a DBus interface.
 *
 \verbatim
  comvideo_camerad_dbus_name com.commend.camerad.Service # camerad DBus name
  comvideo_camerad_dbus_path /commend                    # camerad DBus path
 \endverbatim
 *
 * Configuration:
 * \verbatim
  comvideo_keyint 60                             # keyframe interval in seconds
 *
 *
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <glib.h>
#include "comvideo.h"

#define MODULE_NAME                 "comvideo"

#define PROPERTY_VIDEO_DBUS_NAME    "comvideo_video_dbus_name"
#define DEFAULT_VIDEO_DBUS_NAME     "com.commend.videoserver.Service"

#define PROPERTY_VIDEO_DBUS_PATH    "comvideo_video_dbus_path"
#define DEFAULT_VIDEO_DBUS_PATH     "/commend"

#define PROPERTY_CAMERAD_DBUS_NAME  "comvideo_camerad_dbus_name"
#define DEFAULT_CAMERAD_DBUS_NAME   "com.commend.camerad.Service"

#define PROPERTY_CAMERAD_DBUS_PATH  "comvideo_camerad_dbus_path"
#define DEFAULT_CAMERAD_DBUS_PATH   "/commend"

struct comvideo_data comvideo_codec;

static struct vidsrc *vid_src;
static struct vidisp *vid_disp;

static struct vidcodec h264 = {
	.name       = "H264",
	.variant    = NULL,
	.encupdh    = encode_h264_update,
	.ench       = encode_h264,
	.decupdh    = decode_h264_update,
	.dech       = decode_h264,
	.fmtp_ench  = comvideo_fmtp_enc,
	.fmtp_cmph  = NULL,
	.packetizeh = packetize_h264
};


struct vidisp_st {
	const struct vidisp *vd;        /**< Inheritance (1st)     */
	struct vidsz size;              /**< Current size          */

	GstVideoClientStream *client_stream;
	GstAppsrcH264Converter *converter;
	char *peer;
	char *identifier;
};


static void src_destructor(void *arg)
{
	struct vidsrc_st *st;
	GstCameraSrc *src;

	st = arg;
	src = comvideo_codec.camera_src;

	info("comvideo: begin destructor video source: %p source list: %p\n",
	     st, comvideo_codec.sources);

	mtx_lock(&comvideo_codec.lock_src);
	comvideo_codec.sources = g_list_remove(comvideo_codec.sources, st);
	mtx_unlock(&comvideo_codec.lock_src);

	if (!comvideo_codec.sources && src) {
		gst_camera_src_set_sample_cb(
			src,
			GST_CAMERA_SRC_CODEC_H264,
			0,
			NULL, NULL);

		if (comvideo_codec.camerad_client) {
			camerad_client_remove_src(
				comvideo_codec.camerad_client,
				src);
		}

		g_object_unref(src);

		comvideo_codec.camera_src = NULL;
	}

	info("comvideo: end destructor video source: %p source list: %p\n",
	     st, comvideo_codec.sources);
}


static int src_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		     struct vidsrc_prm *prm,
		     const struct vidsz *size,
		     const char *fmt, const char *dev,
		     vidsrc_frame_h *frameh,
		     vidsrc_packet_h  *packeth,
		     vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	GstCameraSrc *src;
	struct config *cfg;
	uint32_t keyint = DEFAULT_KEYFRAME_INTERVAL;

	(void) dev;
	(void) fmt;
	(void) errorh;

	if (!stp || !size || !frameh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), src_destructor);
	if (!st)
		return ENOMEM;

	info("comvideo: begin allocate src: %p source list: %p\n",
	     st, comvideo_codec.sources);

	cfg = conf_config();

	st->vs = vs;
	st->sz = *size;
	st->frameh = frameh;
	st->packeth = packeth;
	st->arg = arg;
	st->pixfmt = 1;
	st->fps = (u_int32_t) prm->fps;
	st->bitrate = cfg->video.bitrate;

	conf_get_u32(conf_cur(), "comvideo_keyint", &keyint);

	if (!comvideo_codec.camera_src) {
		src = camerad_client_add_src_full(
			comvideo_codec.camerad_client,
			GST_CAMERA_COMPONENT_RTP,
			GST_CAMERA_COMPONENT_CODEC_H264,
			st->sz.w, st->sz.h,
			st->fps, st->bitrate, keyint);

		if (src) {
			gst_camera_src_set_sample_cb(
				src,
				GST_CAMERA_SRC_CODEC_H264,
				st->bitrate,
				(camera_new_sample)
					camera_h264_sample_received,
				NULL);
		}

		comvideo_codec.camera_src = src;
	}

	mtx_lock(&comvideo_codec.lock_src);
	comvideo_codec.sources = g_list_append(comvideo_codec.sources, st);
	mtx_unlock(&comvideo_codec.lock_src);

	*stp = st;

	info("comvideo: end allocate src: %p  source list: %p\n", st,
	     comvideo_codec.sources);

	return 0;
}


static void
disp_enable(struct vidisp_st *st, bool disp_enabled)
{
	if (!st->client_stream)
		return;

	g_object_set(
		st->client_stream,
		"enabled", disp_enabled,
		NULL);

}


static void stop_stream(struct vidisp_st *st)
{

	if (st->converter) {
		gst_object_unref(st->converter);
		st->converter = NULL;
	}

	disp_enable(st, FALSE);

	if (st->client_stream) {
		gst_video_client_stream_stop(st->client_stream);
		g_object_unref(st->client_stream);
		st->client_stream = NULL;
	}
}


static void disp_destructor(void *arg)
{
	struct vidisp_st *st = arg;

	debug("comvideo: stop display\n");

	stop_stream(st);

	mem_deref(st->peer);
	mem_deref(st->identifier);
}


static int disp_alloc(struct vidisp_st **stp, const struct vidisp *vd,
		      struct vidisp_prm *prm, const char *dev,
		      vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;
	int err = 0;

	(void) prm;
	(void) vd;
	(void) dev;
	(void) resizeh;
	(void) arg;

	st = mem_zalloc(sizeof(*st), disp_destructor);
	if (!st)
		return ENOMEM;

	st->peer = NULL;
	st->identifier = NULL;

	*stp = st;

	return err;
}


static void
disp_map_call_id(struct call *call, void *arg)
{
	struct vidisp_st *st = arg;
	const char *id = call_id(call);
	const char *peer_uri = call_peeruri(call);

	if (st->identifier)
		return;

	if (!str_cmp(peer_uri, st->peer))
		str_dup(&st->identifier, id);
}


static int
disp_find_identifier(struct vidisp_st *st, const char *peer)
{
	int err;

	if (st->identifier)
		return 0;

	if (!st->peer) {
		err = str_dup(&st->peer, peer);
		if (err)
			return err;
	}

	uag_filter_calls(disp_map_call_id, NULL, st);
	return 0;
}


static int
disp_create_client_stream(struct vidisp_st *st)
{
	if (!st->client_stream)
		st->client_stream = gst_video_client_create_stream(
					comvideo_codec.video_client,
					10,
					st->identifier, "video/x-h264");

	if (!st->client_stream) {
		warning("comvideo: failed to create client stream\n");
		return ENODEV;
	}

	disp_enable(st,TRUE);

	st->converter = gst_appsrc_h264_converter_new(st->client_stream);
	if (!st->converter) {
		warning("comvideo: failed to create h264 converter\n");
		g_object_unref(st->client_stream);
		st->client_stream = NULL;
		return ENODEV;
	}

	return 0;
}


static int
disp_frame(struct vidisp_st *st, const char *peer,
		     const struct vidframe *frame, uint64_t timestamp)
{
	int err = 0;

	if (!st) {
		warning("comvideo: vidisp_st is NULL\n");
		return EINVAL;
	}

	if (!st->identifier)
		err = disp_find_identifier(st, peer);

	if (err)
		return err;

	if (!st->client_stream || !st->converter)
		err = disp_create_client_stream(st);

	if (err)
		return err;

	if (!st->client_stream || !st->converter)
		return ENODEV;

	if (gst_appsrc_h264_converter_got_error(st->converter)) {
		warning("comvideo: h264 converter got error -> retry\n");
		stop_stream(st);
		return 0;
	}

	if (frame->data[0]) {

		uint16_t low = frame->linesize[0];
		uint32_t high = ((uint32_t) frame->linesize[1]) << 16;

		unsigned long buf_size = (unsigned long) low + high;

		gst_appsrc_h264_converter_send_frame(
			st->converter, frame->data[0],
			buf_size, frame->size.w,
			frame->size.h, timestamp);
	}
	else {
		warning("comvideo: frame data is NULL\n");
	}

	return 0;
}


static int module_init(void)
{
	struct conf *conf;
	int err;

	err  = mtx_init(&comvideo_codec.lock_src, mtx_plain) != thrd_success;
	if (err)
		return err;

	if (!gst_is_initialized()) {
		gst_init(NULL, NULL);
	}

	conf = conf_cur();

	if (conf_get_str(conf, PROPERTY_VIDEO_DBUS_NAME,
			 comvideo_codec.video_dbus_name, DBUS_PROPERTY_SIZE)) {
		g_strlcpy(comvideo_codec.video_dbus_name,
			  DEFAULT_VIDEO_DBUS_NAME,
			  DBUS_PROPERTY_SIZE);
	}

	if (conf_get_str(conf, PROPERTY_VIDEO_DBUS_PATH,
			 comvideo_codec.video_dbus_path, DBUS_PROPERTY_SIZE)) {
		g_strlcpy(comvideo_codec.video_dbus_path,
			  DEFAULT_VIDEO_DBUS_PATH,
			  DBUS_PROPERTY_SIZE);
	}

	comvideo_codec.video_client =
		gst_video_client_new(
			comvideo_codec.video_dbus_name,
			comvideo_codec.video_dbus_path);

	if (conf_get_str(conf, PROPERTY_CAMERAD_DBUS_NAME,
			 comvideo_codec.camerad_dbus_name,
			 DBUS_PROPERTY_SIZE)) {
		g_strlcpy(comvideo_codec.camerad_dbus_name,
			  DEFAULT_CAMERAD_DBUS_NAME,
			  DBUS_PROPERTY_SIZE);
	}

	if (conf_get_str(conf, PROPERTY_CAMERAD_DBUS_PATH,
			 comvideo_codec.camerad_dbus_path,
			 DBUS_PROPERTY_SIZE)) {
		g_strlcpy(comvideo_codec.camerad_dbus_path,
			  DEFAULT_CAMERAD_DBUS_PATH,
			  DBUS_PROPERTY_SIZE);
	}

	comvideo_codec.camera_src = NULL;
	comvideo_codec.sources = NULL;

	comvideo_codec.camerad_client =
		camerad_client_new(
			comvideo_codec.camerad_dbus_name,
			comvideo_codec.camerad_dbus_path);

	vidcodec_register(baresip_vidcodecl(), &h264);

	vidisp_register(&vid_disp, baresip_vidispl(),
			MODULE_NAME, disp_alloc, NULL, disp_frame, NULL);

	return vidsrc_register(&vid_src, baresip_vidsrcl(),
			       MODULE_NAME, src_alloc, NULL);
}


static int module_close(void) {
	vid_src = mem_deref(vid_src);
	vid_disp = mem_deref(vid_disp);
	vidcodec_unregister(&h264);

	g_object_unref(comvideo_codec.camerad_client);
	g_object_unref(comvideo_codec.video_client);

	mtx_destroy(&comvideo_codec.lock_src);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(comvideo) = {
	MODULE_NAME,
	"vidcodec",
	module_init,
	module_close
};
