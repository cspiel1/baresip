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
#include <v4l2camera/v4l2camera-object.h>
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
	char *name;
	char *id;
	int err;
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
		gchar *uid = g_uuid_string_random();
		src = camerad_client_add_src(
			comvideo_codec.camerad_client,
			uid,
			V4L2_TYPE_RTP,
			V4L2_CODEC_H264,
			st->sz.w, st->sz.h,
			st->fps, st->bitrate, keyint,
			camera_h264_sample_received, NULL);

		g_free(uid);
		if (!src) {
			warning("comvideo: failed to create camera source\n");
			mem_deref(st);
			return ENODEV;
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

	info("comvideo: stop display\n");

	stop_stream(st);

	mem_deref(st->peer);
	mem_deref(st->name);
	mem_deref(st->id);
}


static int disp_alloc(struct vidisp_st **stp, const struct vidisp *vd,
		      struct vidisp_prm *prm, const char *dev,
		      vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;

	(void) prm;
	(void) vd;
	(void) dev;
	(void) resizeh;
	(void) arg;

	st = mem_zalloc(sizeof(*st), disp_destructor);
	if (!st)
		return ENOMEM;

	st->peer = NULL;
	st->name = NULL;
	st->id   = NULL;

	if (!gst_video_client_is_running(comvideo_codec.video_client)) {
		warning("comvideo: no video server available. "
			"no video display for this call.\n");
		st->err = EIO;
	}

	*stp = st;

	return 0;
}


static void
disp_map_call_id(struct call *call, void *arg)
{
	struct vidisp_st *st = arg;
	const char *id = call_id(call);
	const char *peer_uri = call_peeruri(call);

	if (st->id)
		return;

	if (!str_cmp(peer_uri, st->peer)) {
		re_sdprintf(&st->id, "%s", id);
	}
}


static int
disp_find_identifier(struct vidisp_st *st, const char *peer)
{
	int err;
	static int count = 0;

	if (st->id && st->name)
		return 0;

	if (!st->peer) {
		err = str_dup(&st->peer, peer);
		if (err)
			return err;
	}

	if (!st->id)
		uag_filter_calls(disp_map_call_id, NULL, st);

	if (!st->id) {
		warning("comvideo: failed to find identifier for peer %s\n",
			peer);
		return ENODEV;
	}

	if (!st->name)
		re_sdprintf(&st->name, "%s_%d", st->id, count++);

	if (!st->name)
		return ENOMEM;

	return 0;
}


static int
disp_create_client_stream(struct vidisp_st *st)
{
	if (!st->client_stream)
		st->client_stream = gst_video_client_create_stream(
					comvideo_codec.video_client,
					10, st->name, st->id,
					"video/x-h264");

	if (!st->client_stream) {
		warning("comvideo: failed to create client stream\n");
		return ENODEV;
	}

	disp_enable(st, TRUE);

	int err = gst_video_client_stream_get_error(st->client_stream);
	if (err)
		goto out;

	st->converter = gst_appsrc_h264_converter_new(st->client_stream);
	if (!st->converter)
		err = ENODEV;

out:
	if (err) {
		warning("comvideo: client stream has error\n");
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

	if (st->err) {
		/* Currently video.c ignores the returned error.
		 * This means that the call resumes without video playback. */
		return st->err;
	}

	if (!st->id || !st->name)
		err = disp_find_identifier(st, peer);

	if (err)
		goto out;

	if (!st->client_stream || !st->converter)
		err = disp_create_client_stream(st);

	if (err)
		goto out;

	if (!st->client_stream || !st->converter) {
		err = ENODEV;
		goto out;
	}

	if (gst_appsrc_h264_converter_got_error(st->converter)) {
		/* Currently video.c ignores the returned error.
		 * This means that the call resumes without video playback. */
		warning("comvideo: h264 converter got error. "
			"no video display for this call.\n");

		err = EIO;
		goto out;
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

out:
	st->err = err;
	return err;
}


static bool call_has_external_video(struct call *call, char **urlp,
				    bool *insecure, bool *hncheck)
{
	const struct contacts *contacts = baresip_contacts();
	struct contact *con = contact_find(contacts,
					   call_peerroute(call));
	if (!con)
		return false;

	struct sip_addr *addr = contact_addr(con);
	if (!addr || !pl_isset(&addr->params))
		return false;

	struct pl pl1 = PL_INIT;
	if (msg_param_decode(&addr->params, "video_url", &pl1))
		return false;

	struct pl pl2 = PL_INIT;
	struct pl pl3 = PL_INIT;
	msg_param_decode(&addr->params, "video_insecure", &pl2);
	msg_param_decode(&addr->params, "video_hncheck",  &pl3);

	if (insecure) {
		bool ins = false;
		pl_bool(&ins, &pl1);
		*insecure = ins;
	}

	if (hncheck) {
		bool hnc = true;
		pl_bool(&hnc, &pl2);
		*hncheck = hnc;
	}

	if (urlp) {
		int err = pl_strdup(urlp, &pl1);
		if (err)
			return false;
	}

	return true;
}


static void apply_contact_external_video(struct call *call)
{
	bool insecure;
	bool hnc;
	char *url;
	if (!call_has_external_video(call, &url, &insecure, &hnc))
		return;

	info("comvideo: start external video for call %s peer %s url %s\n",
	     call_id(call), call_peerroute(call), url);
	gst_video_client_add_external_stream(
			comvideo_codec.video_client, 0, call_peerroute(call),
			call_id(call), url, insecure, hnc);
	mem_deref(url);
}


static void stop_external_video(struct call *call)
{
	if (!call_has_external_video(call, NULL, NULL, NULL))
		return;

	info("comvideo: stop external video for call %s route %s\n",
	     call_id(call), call_peerroute(call));
	gst_video_client_remove_external_stream(
			comvideo_codec.video_client, call_id(call));
}


/**
 * This event handler is used to switch on/off the external video for the call
 * if there is a matching contact with the video_url parameter. If there is no
 * contact or no video_url parameter, then the event handler does nothing.
 *
 * @param ev     Baresip event value
 * @param event  Baresip event object
 * @param arg    User argument (unused)
 */
static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	struct ua      *ua  = bevent_get_ua(event);
	struct account *acc = ua_account(ua);
	struct call   *call = bevent_get_call(event);
	const char    *prm  = bevent_get_text(event);
	(void)arg;

	debug("comvid: [ ua=%s call=%s ] event: %s (%s)\n",
	     account_aor(acc), call_id(call), bevent_str(ev), prm);

	switch (ev) {
	case BEVENT_CALL_INCOMING:
		apply_contact_external_video(call);
	break;
	case BEVENT_CALL_ESTABLISHED:
		if (!call_is_outgoing(call))
			break;

		apply_contact_external_video(call);
	break;
	case BEVENT_CALL_HOLD:
		stop_external_video(call);
	break;
	case BEVENT_CALL_RESUME:
		apply_contact_external_video(call);
	break;
	case BEVENT_CALL_CLOSED:
		stop_external_video(call);
	break;
	case BEVENT_MODULE: {
		if (call_state(call) != CALL_STATE_INCOMING)
			break;

		struct pl mod, evname, callid;
		int err;
		err = re_regex(prm, strlen(prm), "[^,]*,[^,]*,[~]*",
				&mod, &evname, &callid);
		if (err)
			break;

		if (pl_strcmp(&mod, "commod") ||
			pl_strcmp(&evname, "switchearly"))
			break;

		stop_external_video(call);
		struct call *tocall = uag_call_find_pl(&callid);
		if (tocall)
			apply_contact_external_video(tocall);
	}
	break;
	default:
	break;
	}
}


static int module_init(void)
{
	struct conf *conf;
	int err;

	err  = mtx_init(&comvideo_codec.lock_src, mtx_plain) != thrd_success;
	err |= bevent_register(event_handler, NULL);
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
	bevent_unregister(event_handler);

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
