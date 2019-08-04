/**
 * @file aui2s.h freeRTOS I2S audio driver module - internal interface
 *
 * Copyright (C) 2019 cspiel.at
 */

#define I2S_PORT           (0)
#define DMA_SIZE           (640)

enum I2SOnMask {
	I2O_NONE = 0,
	I2O_PLAY = 1,
	I2O_RECO = 2,
	I2O_BOTH = 3
};


int aui2s_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
			     struct media_ctx **ctx,
			     struct ausrc_prm *prm, const char *device,
			     ausrc_read_h *rh, ausrc_error_h *errh, void *arg);


int aui2s_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg);


int aui2s_start(uint32_t srate, enum I2SOnMask playrec);


int aui2s_stop(enum I2SOnMask playrec);

