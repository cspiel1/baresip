/**
 * @file aui2s_src.c freeRTOS I2S audio driver module - recorder
 *
 * Copyright (C) 2019 Creytiv.com
 */
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2s.h"
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aui2s.h"


struct ausrc_st {
	const struct ausrc *as;  /* pointer to base-class (inheritance) */
	pthread_t thread;
	bool run;
	void *sampv;
	size_t sampc;
	size_t bytes;
	ausrc_read_h *rh;
	void *arg;
	struct ausrc_prm prm;

	uint32_t *pcm;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	/* Wait for termination of other thread */
	if (st->run) {
		info("aui2s: stopping recording thread\n");
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	mem_deref(st->sampv);
	mem_deref(st->pcm);
}


static void convert_pcm(struct ausrc_st *st, size_t i, size_t n)
{
	uint32_t j;
	uint16_t *sampv = st->sampv;
	for (j = 0; j < n; j++) {
		uint32_t v = st->pcm[j];
		uint16_t *o = sampv + i + j;
/*        if (j==0)*/
/*            info("mic=%d %02x %02x %02x %02x", v,*/
/*                (uint8_t) ((v & 0xff000000) >> 24),*/
/*                (uint8_t) ((v & 0xff0000) >> 16),*/
/*                (uint8_t) ((v & 0xff00) >> 8),*/
/*                (uint8_t) ((v & 0xff)) );*/
		*o = v >> 15;
		/* if negative fill with ff */
		if (v & 0x80000000)
			*o |= 0xfffe0000;
	}
}


static void *read_thread(void *arg)
{
	struct ausrc_st *st = arg;
	int err;

	err = aui2s_start(st->prm.srate, I2O_RECO);

	if (err) {
		warning("aui2s: could not start ausrc\n");
		return NULL;
	}
/*    REG_SET_BIT(  I2S_TIMING_REG(I2S_PORT),BIT(9));   |+  #include "soc/i2s_reg.h"   I2S_NUM -> 0 or 1+|*/
/*    Also make sure the Philips mode is active*/
/*    REG_SET_BIT( I2S_CONF_REG(I2S_PORT), I2S_RX_MSB_SHIFT);*/

	while (st->run) {
		size_t i;

		for (i = 0; i + DMA_SIZE / 4 <= st->sampc;) {
			size_t n = 0;
			i2s_read(I2S_PORT, st->pcm, DMA_SIZE, &n, portMAX_DELAY);

			if (n == 0)
				break;

			convert_pcm(st, i, n / 4);
			i += (n / 4);
		}

		st->rh(st->sampv, st->sampc, st->arg);
	}

	aui2s_stop(I2O_RECO);
	info("aui2s: stopped ausrc thread\n");

	return NULL;
}


int aui2s_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
			     struct media_ctx **ctx,
			     struct ausrc_prm *prm, const char *device,
			     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	size_t sampc;
	int err;

	(void) ctx;
	(void) device;
	(void) errh;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	if (prm->fmt!=AUFMT_S16LE) {
		warning("aui2s: unsupported sample format %s\n", aufmt_name(prm->fmt));
		return EINVAL;
	}

	sampc = prm->srate * prm->ch * prm->ptime / 1000;
	if (sampc % (DMA_SIZE / 4)) {
		warning("aui2s: sampc=%d has to be divisible by DMA_SIZE / 4\n",
				sampc);
		return EINVAL;
	}

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->prm = *prm;
	st->as  = as;
	st->rh  = rh;
	st->arg = arg;

	st->sampc = sampc;
	st->bytes = 2 * sampc;
	st->sampv = mem_zalloc(st->bytes, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}
	st->pcm = mem_zalloc(DMA_SIZE, NULL);
	if (!st->pcm) {
		err = ENOMEM;
		goto out;
	}

	st->run = true;
	info("%s starting src thread\n", __func__);
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("aui2s: recording\n");

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
