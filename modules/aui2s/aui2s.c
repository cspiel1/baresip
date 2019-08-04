/**
 * @file aui2s freeRTOS I2S audio driver module
 *
 * Copyright (C) 2019 cspiel.at
 */
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "freertos/FreeRTOS.h"
#include "driver/i2s.h"
#include "aui2s.h"


static struct ausrc *ausrc = NULL;
static struct auplay *auplay = NULL;

/*---------------------------------------------------------------
                            CONFIG
---------------------------------------------------------------*/

static int aui2s_init(void)
{
	int err;

	err  = ausrc_register(&ausrc, baresip_ausrcl(),
			      "aui2s", aui2s_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(),
			       "aui2s", aui2s_play_alloc);


	return err;
}


static enum I2SOnMask _i2s_on = I2O_NONE;


int aui2s_start(uint32_t srate, enum I2SOnMask playrec)
{
	esp_err_t err;
	bool start = _i2s_on == I2O_NONE;
	_i2s_on = _i2s_on | playrec;

	if (srate * 4 % DMA_SIZE) {
		warning("aui2s: srate*4 % DMA_SIZE != 0\n");
		return EINVAL;
	}

	info("%s start with _i2s_on=%d", __func__, _i2s_on);
	if (start) {
		i2s_config_t i2s_config = {
			.mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX,
			.sample_rate =  srate,
			.bits_per_sample = 32,
			.communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
			.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
			.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // lowest interrupt priority
			.dma_buf_count = 2,
			.dma_buf_len = DMA_SIZE,
			.use_apll = 0 // APPL DISABLE
		};
		//install and start i2s driver
		err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
		if (err) {
			warning("aui2s: could not install i2s driver (%s)",
					esp_err_to_name(err));
			return EINVAL;
		}
		i2s_pin_config_t pins = {
			.bck_io_num = 26,
			.ws_io_num = 25,
			.data_out_num = 22,
			.data_in_num = 23
		};
		i2s_set_pin(I2S_PORT, &pins);

		i2s_zero_dma_buffer(I2S_PORT);
	}

	return 0;
}


int aui2s_stop(enum I2SOnMask playrec)
{
	_i2s_on &= (~playrec);

	info("%s _i2s_on=%d", __func__, _i2s_on);
	if (_i2s_on == I2O_NONE) {
		info("%s: %d before i2s_driver_uninstall\n", __func__, __LINE__);
		i2s_driver_uninstall(I2S_PORT);
		info("%s: %d after i2s_driver_uninstall\n", __func__, __LINE__);
	}

	return 0;
}


static int aui2s_close(void)
{
	ausrc  = mem_deref(ausrc);
	auplay = mem_deref(auplay);


	return 0;
}


const struct mod_export DECL_EXPORTS(aui2s) = {
	"aui2s",
	"sound",
	aui2s_init,
	aui2s_close
};
