/**
 * @file aui2s_play.c freeRTOS I2S audio driver module - player
 *
 * Copyright (C) 2019 cspiel.at
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


struct auplay_st {
	const struct auplay *ap;  /* pointer to base-class (inheritance) */
	pthread_t thread;
	bool run;
	void *sampv;
	size_t sampc;
	size_t bytes;
	auplay_write_h *wh;
	void *arg;
	struct auplay_prm prm;

	uint32_t *pcm;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	/* Wait for termination of other thread */
	if (st->run) {
		info("aui2s: stopping playback thread\n");
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	mem_deref(st->sampv);
	mem_deref(st->pcm);
}


/* I2S example data  */
/*00 00 73 f3 00 80 75 f3 */
/*00 00 75 f3 00 00 71 f3 */
/*00 80 72 f3 00 00 7c f3 */
/*00 00 83 f3 00 80 7e f3 */
/*00 80 78 f3 00 80 76 f3 */
/*00 80 73 f3 00 80 6d f3 */
/*00 00 6a f3 00 80 6c f3 */
/*00 00 69 f3 00 00 63 f3 */
// -------------------------------------------------------------------------
static void convert_sampv(struct auplay_st *st, size_t i, size_t n)
{
	uint32_t j;
	int16_t *sampv = st->sampv;
	for (j = 0; j < n; j++) {
		uint32_t v = sampv[i+j];
/*        if (j==0) {*/
/*            info("spk=%d %08x %08x %08x %08x", v,*/
/*                    (uint32_t) ((v & 0xff000000) >> 24),*/
/*                    (uint32_t) ((v & 0xff0000) >> 8),*/
/*                    (uint32_t) ((v & 0xff00) << 8),*/
/*                    (uint32_t) ((v & 0xff) << 24) );*/
/*        st->pcm[j] = ((v & 0xff) << 24) | ((v & 0xff00) << 8);*/
		st->pcm[j] = v << 17;
	}
}

/*static void example_disp_buf(uint8_t* buf, int length)*/
/*{*/
/*    for (int i = 0; i < length; i++) {*/
/*        printf("%02x ", buf[i]);*/
/*        if ((i + 1) % 8 == 0) {*/
/*            printf("\n");*/
/*        }*/
/*    }*/
/*}*/

static void *write_thread(void *arg)
{
	struct auplay_st *st = arg;
	int err;

	err = aui2s_start(st->prm.srate, I2O_PLAY);
	if (err) {
		warning("aui2s: could not start auplay\n");
		return NULL;
	}
    i2s_set_clk(I2S_PORT, st->prm.srate, 32, 1);

	while (st->run) {
		size_t i;

		st->wh(st->sampv, st->sampc, st->arg);
		for (i = 0; i + DMA_SIZE / 4 <= st->sampc;) {
			size_t n;
			convert_sampv(st, i, DMA_SIZE / 4);

			i2s_write(I2S_PORT, (const uint8_t*) st->pcm, DMA_SIZE, &n, portMAX_DELAY);
			if (n != DMA_SIZE)
				warning("aui2s: written %lu bytes but expected %lu.", n,
						DMA_SIZE);

			if (n == 0)
				break;

/*            info("n=%lu %d %d", n, ((int16_t*) st->sampv)[0], ((int16_t*) st->sampv)[1]);*/
/*            example_disp_buf((uint8_t*) st->pcm, 8);*/

			i += (n / 4);
		}
	}

	aui2s_stop(I2O_PLAY);
	info("aui2s: stopped auplay thread\n");

	return NULL;
}


int aui2s_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err;

	(void) device;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	if (prm->fmt!=AUFMT_S16LE) {
		warning("aui2s: unsupported sample format %s\n", aufmt_name(prm->fmt));
		return EINVAL;
	}

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->prm = *prm;
	st->ap  = ap;
	st->wh  = wh;
	st->arg = arg;

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	st->bytes = 2 * st->sampc;
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
	info("%s starting play thread\n", __func__);
	err = pthread_create(&st->thread, NULL, write_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("aui2s: playback started\n");

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
