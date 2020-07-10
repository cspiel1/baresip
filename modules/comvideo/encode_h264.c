
/**
 * @file encode_h264.c  Commend specific video source
 *
 * Copyright (C) 2020 Commend.com
 */

#include "comvideo.h"
#include <stdint.h>


struct videnc_state {
	struct videnc_param encprm;
	videnc_packet_h *pkth;
	const struct video *vid;
	unsigned pktsize;
	uint32_t packetization_mode;
	bool got_keyframe;
	int wrncnt;
};


static void encode_h264_sample(struct videnc_state *st,
			       const struct vidpacket *packet);


/* note: dummy function, not used */
int encode_h264(struct videnc_state *st, bool update,
		const struct vidframe *frame, uint64_t timestamp)
{
	(void) st;
	(void) update;
	(void) frame;
	(void) timestamp;
	return 0;
}


int packetize_h264(struct videnc_state *st, const struct vidpacket *packet)
{
	encode_h264_sample(st, packet);

	if (likely(!packet->picup))
		return 0;

	debug("comvideo: keyframe requested\n");
	if (comvideo_codec.camerad_client
	    && comvideo_codec.camera_src) {
		camerad_client_force_keyframe(
			comvideo_codec.camerad_client,
			gst_camera_src_get_id(comvideo_codec.camera_src));
	}

	return 0;
}


static void param_handler(const struct pl *name, const struct pl *val,
			  void *arg)
{
	struct videnc_state *st = arg;

	if (0 == pl_strcasecmp(name, "packetization-mode")) {
		st->packetization_mode = pl_u32(val);

		if (st->packetization_mode != 0 &&
		    st->packetization_mode != 1 ) {
			warning("comvideo: illegal packetization-mode %u\n",
				st->packetization_mode);
		}
	}
}


int encode_h264_update(struct videnc_state **vesp, const struct vidcodec *vc,
		       struct videnc_param *prm, const char *fmtp,
		       videnc_packet_h *pkth, const struct video *vid)
{
	struct videnc_state *st;

	(void) fmtp;

	if (!vesp || !vc || !prm || !pkth)
		return EINVAL;

	if (*vesp)
		return 0;

	st = mem_zalloc(sizeof(*st), NULL);
	if (!st)
		return ENOMEM;

	st->encprm = *prm;
	st->pkth = pkth;
	st->vid = vid;
	st->pktsize = prm->pktsize;

	if (str_isset(fmtp)) {
		struct pl sdp_fmtp;
		pl_set_str(&sdp_fmtp, fmtp);
		fmt_param_apply(&sdp_fmtp, param_handler, st);
	}

	info("comvideo: video encoder %s: %.2f fps, %d bit/s, pktsize=%u\n",
	     vc->name, prm->fps, prm->bitrate, prm->pktsize);
	*vesp = st;

	return 0;
}


static uint8_t vidpacket_nal_type(const struct vidpacket *packet)
{
	/* Check if the packet is a keyframe */
	if (packet->buf && packet->size < RTP_HEADER_SIZE)
		return 0;

	uint8_t nal_type = packet->buf[RTP_HEADER_SIZE] & 0x1f;
	if (nal_type == 28) {
		/* FU-A NALU */
		nal_type = packet->buf[RTP_HEADER_SIZE + 1] & 0x1f;
	}

	return nal_type;
}


static void
encode_h264_sample(struct videnc_state *st, const struct vidpacket *packet)
{
	guint8 *sample = packet->buf;
	gsize  size = packet->size;

	uint8_t *hdr = sample;
	uint8_t *pld = sample + RTP_HEADER_SIZE;
	size_t pld_len = size - RTP_HEADER_SIZE;

	guint32 ts = 0;
	bool marker;

	ts |= hdr[4] << 24;
	ts |= hdr[5] << 16;
	ts |= hdr[6] << 8;
	ts |= hdr[7];

	marker = hdr[1] >> 7;

	st->got_keyframe |= packet->keyframe;
	uint8_t nal_type = vidpacket_nal_type(packet);
	if (nal_type==1 && !st->got_keyframe && (st->wrncnt++ % 10) == 0)
		warning("comvideo: got no keyframe from camerad\n");

	st->pkth(marker, ts, hdr, 0, pld, pld_len, st->vid);
}


void
camera_h264_sample_received(GstCameraSrc *src, GstBuffer *buffer, void *unused)
{
	GstMapInfo map_info;

	(void) src;
	(void) unused;

	gst_buffer_map(buffer, &map_info, (GstMapFlags) (GST_MAP_READ));

	mtx_lock(&comvideo_codec.lock_src);

	if (map_info.size >= RTP_HEADER_SIZE &&
	    map_info.data && comvideo_codec.sources) {
		struct vidpacket vp = {
			.buf = map_info.data,
			.size = map_info.size,
			.picup = false,
		};

		vp.keyframe = (vidpacket_nal_type(&vp) == 5);
		for (GList *l = comvideo_codec.sources; l != NULL; l=l->next) {
			struct vidsrc_st *stl = (struct vidsrc_st *) l->data;
			stl->packeth(&vp, stl->arg);
		}
	}

	mtx_unlock(&comvideo_codec.lock_src);

	gst_buffer_unmap(buffer, &map_info);
}
