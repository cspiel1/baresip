/**
 * @file comvideo.h  Commend specific video source
 *
 * Copyright (C) 2020 Commend.com
 */

#ifndef BARESIP_COMVIDEO_SRC_H
#define BARESIP_COMVIDEO_SRC_H

#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <cameradclient/cameradclient.h>
#include <videoclient/videoclient.h>

#define DBUS_PROPERTY_SIZE          128

struct comvideo_data
{
	GstVideoClient *video_client;
	CameradClient *camerad_client;
	GstCameraSrc *camera_src;

	GList *sources;

	char camerad_dbus_name[DBUS_PROPERTY_SIZE];
	char camerad_dbus_path[DBUS_PROPERTY_SIZE];

	char video_dbus_name[DBUS_PROPERTY_SIZE];
	char video_dbus_path[DBUS_PROPERTY_SIZE];

	mtx_t lock_src;
};

extern struct comvideo_data  comvideo_codec;

struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */
	struct vidsz sz;
	u_int32_t pixfmt;
	u_int32_t fps;
	u_int32_t bitrate;

	vidsrc_frame_h *frameh;
	vidsrc_packet_h *packeth;
	void *arg;
};

/*
 * Encode
 */

struct videnc_state;


void
camera_h264_sample_received(GstCameraSrc *src, GstBuffer *buffer,
			    void *unused);


int
encode_h264(struct videnc_state *st, bool update,
	    const struct vidframe *frame, uint64_t timestamp);


int packetize_h264(struct videnc_state *st, const struct vidpacket *packet);

int
encode_h264_update(struct videnc_state **vesp, const struct vidcodec *vc,
		   struct videnc_param *prm, const char *fmtp,
		   videnc_packet_h *pkth, const struct video *vid);

/*
 * Decode
 */

int decode_h264(struct viddec_state *st, struct vidframe *frame,
		struct viddec_packet *pkt);


int decode_h264_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		       const char *fmtp, const struct video *vid);


/*
 * SDP
 */

bool comvideo_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg);


int comvideo_fmtp_enc(
	struct mbuf *mb, const struct sdp_format *fmt,
	bool offer, void *arg);

#endif //BARESIP_COMVIDEO_SRC_H
