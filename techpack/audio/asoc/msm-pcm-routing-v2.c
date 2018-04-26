/* Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/asound.h>
#include <sound/pcm_params.h>
#include <sound/hwdep.h>
#include <sound/audio_effects.h>
#include <dsp/msm-dts-srs-tm-config.h>
#include <dsp/q6voice.h>
#include <dsp/q6adm-v2.h>
#include <dsp/q6asm-v2.h>
#include <dsp/q6afe-v2.h>
#include <dsp/q6lsm.h>
#include <dsp/q6core.h>
#include <dsp/audio_cal_utils.h>

#include "msm-pcm-routing-v2.h"
#include "msm-pcm-routing-devdep.h"
#include "msm-qti-pp-config.h"
#include "msm-dolby-dap-config.h"
#include "msm-ds2-dap-config.h"

#ifndef CONFIG_DOLBY_DAP
#undef DOLBY_ADM_COPP_TOPOLOGY_ID
#define DOLBY_ADM_COPP_TOPOLOGY_ID 0xFFFFFFFE
#endif

#ifndef CONFIG_DOLBY_DS2
#undef DS2_ADM_COPP_TOPOLOGY_ID
#define DS2_ADM_COPP_TOPOLOGY_ID 0xFFFFFFFF
#endif

static struct mutex routing_lock;

static struct cal_type_data *cal_data[MAX_ROUTING_CAL_TYPES];

static int fm_switch_enable;
static int hfp_switch_enable;
static int a2dp_switch_enable;
static int int0_mi2s_switch_enable;
static int int4_mi2s_switch_enable;
static int pri_mi2s_switch_enable;
static int sec_mi2s_switch_enable;
static int tert_mi2s_switch_enable;
static int quat_mi2s_switch_enable;
static int quin_mi2s_switch_enable;
static int fm_pcmrx_switch_enable;
static int usb_switch_enable;
static int lsm_port_index;
static int slim0_rx_aanc_fb_port;
static int msm_route_ec_ref_rx;
static int msm_ec_ref_ch = 4;
static int msm_ec_ref_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int msm_ec_ref_sampling_rate = 48000;
static uint32_t voc_session_id = ALL_SESSION_VSID;
static int msm_route_ext_ec_ref;
static bool is_custom_stereo_on;
static bool is_ds2_on;
static bool swap_ch;
static int msm_ec_ref_port_id;

#define WEIGHT_0_DB 0x4000
/* all the FEs which can support channel mixer */
static struct msm_pcm_channel_mixer channel_mixer[MSM_FRONTEND_DAI_MM_SIZE];
/* input BE for each FE */
static int channel_input[MSM_FRONTEND_DAI_MM_SIZE][ADM_MAX_CHANNELS];

enum {
	MADNONE,
	MADAUDIO,
	MADBEACON,
	MADULTRASOUND,
	MADSWAUDIO,
};

#define ADM_LSM_PORT_INDEX 9

#define SLIMBUS_0_TX_TEXT "SLIMBUS_0_TX"
#define SLIMBUS_1_TX_TEXT "SLIMBUS_1_TX"
#define SLIMBUS_2_TX_TEXT "SLIMBUS_2_TX"
#define SLIMBUS_3_TX_TEXT "SLIMBUS_3_TX"
#define SLIMBUS_4_TX_TEXT "SLIMBUS_4_TX"
#define SLIMBUS_5_TX_TEXT "SLIMBUS_5_TX"
#define TERT_MI2S_TX_TEXT "TERT_MI2S_TX"
#define QUAT_MI2S_TX_TEXT "QUAT_MI2S_TX"
#define PRI_TDM_TX_3_TEXT "PRI_TDM_TX_3"
#define PRI_TDM_TX_2_TEXT "PRI_TDM_TX_2"
#define ADM_LSM_TX_TEXT "ADM_LSM_TX"
#define INT3_MI2S_TX_TEXT "INT3_MI2S_TX"

#define LSM_FUNCTION_TEXT "LSM Function"
static const char * const lsm_port_text[] = {
	"None",
	SLIMBUS_0_TX_TEXT, SLIMBUS_1_TX_TEXT, SLIMBUS_2_TX_TEXT,
	SLIMBUS_3_TX_TEXT, SLIMBUS_4_TX_TEXT, SLIMBUS_5_TX_TEXT,
	TERT_MI2S_TX_TEXT, QUAT_MI2S_TX_TEXT, ADM_LSM_TX_TEXT,
	INT3_MI2S_TX_TEXT, PRI_TDM_TX_2_TEXT, PRI_TDM_TX_3_TEXT,
};

struct msm_pcm_route_bdai_pp_params {
	u16 port_id; /* AFE port ID */
	unsigned long pp_params_config;
	bool mute_on;
	int latency;
};

static struct msm_pcm_route_bdai_pp_params
	msm_bedais_pp_params[MSM_BACKEND_DAI_PP_PARAMS_REQ_MAX] = {
	{HDMI_RX, 0, 0, 0},
	{DISPLAY_PORT_RX, 0, 0, 0},
};

/*
 * The be_dai_name_table is passed to HAL so that it can specify the
 * BE ID for the BE it wants to enable based on the name. Thus there
 * is a matching table and structure in HAL that need to be updated
 * if any changes to these are made.
 */
struct msm_pcm_route_bdai_name {
	unsigned int be_id;
	char be_name[LPASS_BE_NAME_MAX_LENGTH];
};
static struct msm_pcm_route_bdai_name be_dai_name_table[MSM_BACKEND_DAI_MAX];

static int msm_routing_send_device_pp_params(int port_id,  int copp_idx,
					     int fe_id);

static int msm_routing_get_bit_width(unsigned int format)
{
	int bit_width;

	switch (format) {
	case SNDRV_PCM_FORMAT_S32_LE:
		bit_width = 32;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
		bit_width = 24;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		bit_width = 16;
	}
	return bit_width;
}

static bool msm_is_resample_needed(int input_sr, int output_sr)
{
	bool rc = false;

	if (input_sr != output_sr)
		rc = true;

	pr_debug("perform resampling (%s) for copp rate (%d)afe rate (%d)",
		(rc ? "oh yes" : "not really"),
		input_sr, output_sr);

	return rc;
}

static void msm_pcm_routing_cfg_pp(int port_id, int copp_idx, int topology,
				   int channels)
{
	int rc = 0;

	switch (topology) {
	case SRS_TRUMEDIA_TOPOLOGY_ID:
		pr_debug("%s: SRS_TRUMEDIA_TOPOLOGY_ID\n", __func__);
		msm_dts_srs_tm_init(port_id, copp_idx);
		break;
	case DS2_ADM_COPP_TOPOLOGY_ID:
		pr_debug("%s: DS2_ADM_COPP_TOPOLOGY %d\n",
			 __func__, DS2_ADM_COPP_TOPOLOGY_ID);
		rc = msm_ds2_dap_init(port_id, copp_idx, channels,
				      is_custom_stereo_on);
		if (rc < 0)
			pr_err("%s: DS2 topo_id 0x%x, port %d, CS %d rc %d\n",
				__func__, topology, port_id,
				is_custom_stereo_on, rc);
		break;
	case DOLBY_ADM_COPP_TOPOLOGY_ID:
		if (is_ds2_on) {
			pr_debug("%s: DS2_ADM_COPP_TOPOLOGY\n", __func__);
			rc = msm_ds2_dap_init(port_id, copp_idx, channels,
				is_custom_stereo_on);
			if (rc < 0)
				pr_err("%s:DS2 topo_id 0x%x, port %d, rc %d\n",
					__func__, topology, port_id, rc);
		} else {
			pr_debug("%s: DOLBY_ADM_COPP_TOPOLOGY_ID\n", __func__);
			rc = msm_dolby_dap_init(port_id, copp_idx, channels,
						is_custom_stereo_on);
			if (rc < 0)
				pr_err("%s: DS1 topo_id 0x%x, port %d, rc %d\n",
					__func__, topology, port_id, rc);
		}
		break;
	case ADM_CMD_COPP_OPEN_TOPOLOGY_ID_AUDIOSPHERE:
		pr_debug("%s: TOPOLOGY_ID_AUDIOSPHERE\n", __func__);
		rc = msm_qti_pp_asphere_init(port_id, copp_idx);
		if (rc < 0)
			pr_err("%s: topo_id 0x%x, port %d, copp %d, rc %d\n",
				__func__, topology, port_id, copp_idx, rc);
		break;
	default:
		/* custom topology specific feature param handlers */
		break;
	}
}

static void msm_pcm_routing_deinit_pp(int port_id, int topology)
{
	switch (topology) {
	case SRS_TRUMEDIA_TOPOLOGY_ID:
		pr_debug("%s: SRS_TRUMEDIA_TOPOLOGY_ID\n", __func__);
		msm_dts_srs_tm_deinit(port_id);
		break;
	case DS2_ADM_COPP_TOPOLOGY_ID:
		pr_debug("%s: DS2_ADM_COPP_TOPOLOGY_ID %d\n",
			 __func__, DS2_ADM_COPP_TOPOLOGY_ID);
		msm_ds2_dap_deinit(port_id);
		break;
	case DOLBY_ADM_COPP_TOPOLOGY_ID:
		if (is_ds2_on) {
			pr_debug("%s: DS2_ADM_COPP_TOPOLOGY_ID\n", __func__);
			msm_ds2_dap_deinit(port_id);
		} else {
			pr_debug("%s: DOLBY_ADM_COPP_TOPOLOGY_ID\n", __func__);
			msm_dolby_dap_deinit(port_id);
		}
		break;
	case ADM_CMD_COPP_OPEN_TOPOLOGY_ID_AUDIOSPHERE:
		pr_debug("%s: TOPOLOGY_ID_AUDIOSPHERE\n", __func__);
		msm_qti_pp_asphere_deinit(port_id);
		break;
	default:
		/* custom topology specific feature deinit handlers */
		break;
	}
}

static void msm_pcm_routng_cfg_matrix_map_pp(struct route_payload payload,
					     int path_type, int perf_mode)
{
	int itr = 0, rc = 0;

	if ((path_type == ADM_PATH_PLAYBACK) &&
	    (perf_mode == LEGACY_PCM_MODE) &&
	    is_custom_stereo_on) {
		for (itr = 0; itr < payload.num_copps; itr++) {
			if ((payload.port_id[itr] != SLIMBUS_0_RX) &&
			    (payload.port_id[itr] != RT_PROXY_PORT_001_RX)) {
				continue;
			}

			rc = msm_qti_pp_send_stereo_to_custom_stereo_cmd(
				payload.port_id[itr],
				payload.copp_idx[itr],
				payload.session_id,
				Q14_GAIN_ZERO_POINT_FIVE,
				Q14_GAIN_ZERO_POINT_FIVE,
				Q14_GAIN_ZERO_POINT_FIVE,
				Q14_GAIN_ZERO_POINT_FIVE);
			if (rc < 0)
				pr_err("%s: err setting custom stereo\n",
					__func__);
		}
	}
}

#define SLIMBUS_EXTPROC_RX AFE_PORT_INVALID
struct msm_pcm_routing_bdai_data msm_bedais[MSM_BACKEND_DAI_MAX] = {
	{ PRIMARY_I2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_PRI_I2S_RX},
	{ PRIMARY_I2S_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_PRI_I2S_TX},
	{ SLIMBUS_0_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_0_RX},
	{ SLIMBUS_0_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_0_TX},
	{ HDMI_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_HDMI},
	{ INT_BT_SCO_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_INT_BT_SCO_RX},
	{ INT_BT_SCO_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_INT_BT_SCO_TX},
	{ INT_FM_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_INT_FM_RX},
	{ INT_FM_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_INT_FM_TX},
	{ RT_PROXY_PORT_001_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_AFE_PCM_RX},
	{ RT_PROXY_PORT_001_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_AFE_PCM_TX},
	{ AFE_PORT_ID_PRIMARY_PCM_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_AUXPCM_RX},
	{ AFE_PORT_ID_PRIMARY_PCM_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_AUXPCM_TX},
	{ VOICE_PLAYBACK_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_VOICE_PLAYBACK_TX},
	{ VOICE2_PLAYBACK_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_VOICE2_PLAYBACK_TX},
	{ VOICE_RECORD_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INCALL_RECORD_RX},
	{ VOICE_RECORD_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INCALL_RECORD_TX},
	{ MI2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_MI2S_RX},
	{ MI2S_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_MI2S_TX},
	{ SECONDARY_I2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SEC_I2S_RX},
	{ SLIMBUS_1_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_1_RX},
	{ SLIMBUS_1_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_1_TX},
	{ SLIMBUS_2_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_2_RX},
	{ SLIMBUS_2_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_2_TX},
	{ SLIMBUS_3_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_3_RX},
	{ SLIMBUS_3_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_3_TX},
	{ SLIMBUS_4_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_4_RX},
	{ SLIMBUS_4_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_4_TX},
	{ SLIMBUS_5_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_5_RX},
	{ SLIMBUS_5_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_5_TX},
	{ SLIMBUS_6_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_6_RX},
	{ SLIMBUS_6_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_6_TX},
	{ SLIMBUS_7_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_7_RX},
	{ SLIMBUS_7_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_7_TX},
	{ SLIMBUS_8_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_8_RX},
	{ SLIMBUS_8_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_SLIMBUS_8_TX},
	{ SLIMBUS_EXTPROC_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_STUB_RX},
	{ SLIMBUS_EXTPROC_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_STUB_TX},
	{ SLIMBUS_EXTPROC_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_STUB_1_TX},
	{ AFE_PORT_ID_QUATERNARY_MI2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_MI2S_RX},
	{ AFE_PORT_ID_QUATERNARY_MI2S_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_MI2S_TX},
	{ AFE_PORT_ID_SECONDARY_MI2S_RX,  0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_MI2S_RX},
	{ AFE_PORT_ID_SECONDARY_MI2S_TX,  0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_MI2S_TX},
	{ AFE_PORT_ID_PRIMARY_MI2S_RX,    0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_MI2S_RX},
	{ AFE_PORT_ID_PRIMARY_MI2S_TX,    0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_MI2S_TX},
	{ AFE_PORT_ID_TERTIARY_MI2S_RX,   0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_MI2S_RX},
	{ AFE_PORT_ID_TERTIARY_MI2S_TX,   0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_MI2S_TX},
	{ AUDIO_PORT_ID_I2S_RX,           0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_AUDIO_I2S_RX},
	{ AFE_PORT_ID_SECONDARY_PCM_RX,	  0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_AUXPCM_RX},
	{ AFE_PORT_ID_SECONDARY_PCM_TX,   0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_AUXPCM_TX},
	{ AFE_PORT_ID_SPDIF_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SPDIF_RX},
	{ AFE_PORT_ID_SECONDARY_MI2S_RX_SD1, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_MI2S_RX_SD1},
	{ AFE_PORT_ID_QUINARY_MI2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_MI2S_RX},
	{ AFE_PORT_ID_QUINARY_MI2S_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_MI2S_TX},
	{ AFE_PORT_ID_SENARY_MI2S_TX,   0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SENARY_MI2S_TX},
	{ AFE_PORT_ID_PRIMARY_TDM_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_RX_0},
	{ AFE_PORT_ID_PRIMARY_TDM_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_TX_0},
	{ AFE_PORT_ID_PRIMARY_TDM_RX_1, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_RX_1},
	{ AFE_PORT_ID_PRIMARY_TDM_TX_1, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_TX_1},
	{ AFE_PORT_ID_PRIMARY_TDM_RX_2, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_RX_2},
	{ AFE_PORT_ID_PRIMARY_TDM_TX_2, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_TX_2},
	{ AFE_PORT_ID_PRIMARY_TDM_RX_3, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_RX_3},
	{ AFE_PORT_ID_PRIMARY_TDM_TX_3, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_TX_3},
	{ AFE_PORT_ID_PRIMARY_TDM_RX_4, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_RX_4},
	{ AFE_PORT_ID_PRIMARY_TDM_TX_4, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_TX_4},
	{ AFE_PORT_ID_PRIMARY_TDM_RX_5, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_RX_5},
	{ AFE_PORT_ID_PRIMARY_TDM_TX_5, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_TX_5},
	{ AFE_PORT_ID_PRIMARY_TDM_RX_6, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_RX_6},
	{ AFE_PORT_ID_PRIMARY_TDM_TX_6, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_TX_6},
	{ AFE_PORT_ID_PRIMARY_TDM_RX_7, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_RX_7},
	{ AFE_PORT_ID_PRIMARY_TDM_TX_7, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_PRI_TDM_TX_7},
	{ AFE_PORT_ID_SECONDARY_TDM_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_RX_0},
	{ AFE_PORT_ID_SECONDARY_TDM_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_TX_0},
	{ AFE_PORT_ID_SECONDARY_TDM_RX_1, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_RX_1},
	{ AFE_PORT_ID_SECONDARY_TDM_TX_1, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_TX_1},
	{ AFE_PORT_ID_SECONDARY_TDM_RX_2, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_RX_2},
	{ AFE_PORT_ID_SECONDARY_TDM_TX_2, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_TX_2},
	{ AFE_PORT_ID_SECONDARY_TDM_RX_3, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_RX_3},
	{ AFE_PORT_ID_SECONDARY_TDM_TX_3, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_TX_3},
	{ AFE_PORT_ID_SECONDARY_TDM_RX_4, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_RX_4},
	{ AFE_PORT_ID_SECONDARY_TDM_TX_4, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_TX_4},
	{ AFE_PORT_ID_SECONDARY_TDM_RX_5, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_RX_5},
	{ AFE_PORT_ID_SECONDARY_TDM_TX_5, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_TX_5},
	{ AFE_PORT_ID_SECONDARY_TDM_RX_6, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_RX_6},
	{ AFE_PORT_ID_SECONDARY_TDM_TX_6, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_TX_6},
	{ AFE_PORT_ID_SECONDARY_TDM_RX_7, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_RX_7},
	{ AFE_PORT_ID_SECONDARY_TDM_TX_7, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_SEC_TDM_TX_7},
	{ AFE_PORT_ID_TERTIARY_TDM_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_RX_0},
	{ AFE_PORT_ID_TERTIARY_TDM_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_TX_0},
	{ AFE_PORT_ID_TERTIARY_TDM_RX_1, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_RX_1},
	{ AFE_PORT_ID_TERTIARY_TDM_TX_1, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_TX_1},
	{ AFE_PORT_ID_TERTIARY_TDM_RX_2, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_RX_2},
	{ AFE_PORT_ID_TERTIARY_TDM_TX_2, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_TX_2},
	{ AFE_PORT_ID_TERTIARY_TDM_RX_3, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_RX_3},
	{ AFE_PORT_ID_TERTIARY_TDM_TX_3, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_TX_3},
	{ AFE_PORT_ID_TERTIARY_TDM_RX_4, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_RX_4},
	{ AFE_PORT_ID_TERTIARY_TDM_TX_4, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_TX_4},
	{ AFE_PORT_ID_TERTIARY_TDM_RX_5, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_RX_5},
	{ AFE_PORT_ID_TERTIARY_TDM_TX_5, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_TX_5},
	{ AFE_PORT_ID_TERTIARY_TDM_RX_6, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_RX_6},
	{ AFE_PORT_ID_TERTIARY_TDM_TX_6, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_TX_6},
	{ AFE_PORT_ID_TERTIARY_TDM_RX_7, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_RX_7},
	{ AFE_PORT_ID_TERTIARY_TDM_TX_7, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_TDM_TX_7},
	{ AFE_PORT_ID_QUATERNARY_TDM_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_RX_0},
	{ AFE_PORT_ID_QUATERNARY_TDM_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_TX_0},
	{ AFE_PORT_ID_QUATERNARY_TDM_RX_1, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_RX_1},
	{ AFE_PORT_ID_QUATERNARY_TDM_TX_1, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_TX_1},
	{ AFE_PORT_ID_QUATERNARY_TDM_RX_2, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_RX_2},
	{ AFE_PORT_ID_QUATERNARY_TDM_TX_2, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_TX_2},
	{ AFE_PORT_ID_QUATERNARY_TDM_RX_3, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_RX_3},
	{ AFE_PORT_ID_QUATERNARY_TDM_TX_3, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_TX_3},
	{ AFE_PORT_ID_QUATERNARY_TDM_RX_4, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_RX_4},
	{ AFE_PORT_ID_QUATERNARY_TDM_TX_4, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_TX_4},
	{ AFE_PORT_ID_QUATERNARY_TDM_RX_5, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_RX_5},
	{ AFE_PORT_ID_QUATERNARY_TDM_TX_5, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_TX_5},
	{ AFE_PORT_ID_QUATERNARY_TDM_RX_6, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_RX_6},
	{ AFE_PORT_ID_QUATERNARY_TDM_TX_6, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_TX_6},
	{ AFE_PORT_ID_QUATERNARY_TDM_RX_7, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_RX_7},
	{ AFE_PORT_ID_QUATERNARY_TDM_TX_7, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_TDM_TX_7},
	{ AFE_PORT_ID_QUINARY_TDM_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_RX_0},
	{ AFE_PORT_ID_QUINARY_TDM_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_TX_0},
	{ AFE_PORT_ID_QUINARY_TDM_RX_1, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_RX_1},
	{ AFE_PORT_ID_QUINARY_TDM_TX_1, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_TX_1},
	{ AFE_PORT_ID_QUINARY_TDM_RX_2, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_RX_2},
	{ AFE_PORT_ID_QUINARY_TDM_TX_2, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_TX_2},
	{ AFE_PORT_ID_QUINARY_TDM_RX_3, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_RX_3},
	{ AFE_PORT_ID_QUINARY_TDM_TX_3, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_TX_3},
	{ AFE_PORT_ID_QUINARY_TDM_RX_4, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_RX_4},
	{ AFE_PORT_ID_QUINARY_TDM_TX_4, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_TX_4},
	{ AFE_PORT_ID_QUINARY_TDM_RX_5, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_RX_5},
	{ AFE_PORT_ID_QUINARY_TDM_TX_5, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_TX_5},
	{ AFE_PORT_ID_QUINARY_TDM_RX_6, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_RX_6},
	{ AFE_PORT_ID_QUINARY_TDM_TX_6, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_TX_6},
	{ AFE_PORT_ID_QUINARY_TDM_RX_7, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_RX_7},
	{ AFE_PORT_ID_QUINARY_TDM_TX_7, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_TDM_TX_7},
	{ INT_BT_A2DP_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT_BT_A2DP_RX},
	{ AFE_PORT_ID_USB_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_USB_AUDIO_RX},
	{ AFE_PORT_ID_USB_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_USB_AUDIO_TX},
	{ DISPLAY_PORT_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_DISPLAY_PORT},
	{ AFE_PORT_ID_TERTIARY_PCM_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_AUXPCM_RX},
	{ AFE_PORT_ID_TERTIARY_PCM_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_TERT_AUXPCM_TX},
	{ AFE_PORT_ID_QUATERNARY_PCM_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_AUXPCM_RX},
	{ AFE_PORT_ID_QUATERNARY_PCM_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUAT_AUXPCM_TX},
	{ AFE_PORT_ID_QUINARY_PCM_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_AUXPCM_RX},
	{ AFE_PORT_ID_QUINARY_PCM_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_QUIN_AUXPCM_TX},
	{ AFE_PORT_ID_INT0_MI2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT0_MI2S_RX},
	{ AFE_PORT_ID_INT0_MI2S_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT0_MI2S_TX},
	{ AFE_PORT_ID_INT1_MI2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT1_MI2S_RX},
	{ AFE_PORT_ID_INT1_MI2S_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT1_MI2S_TX},
	{ AFE_PORT_ID_INT2_MI2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT2_MI2S_RX},
	{ AFE_PORT_ID_INT2_MI2S_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT2_MI2S_TX},
	{ AFE_PORT_ID_INT3_MI2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT3_MI2S_RX},
	{ AFE_PORT_ID_INT3_MI2S_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT3_MI2S_TX},
	{ AFE_PORT_ID_INT4_MI2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT4_MI2S_RX},
	{ AFE_PORT_ID_INT4_MI2S_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT4_MI2S_TX},
	{ AFE_PORT_ID_INT5_MI2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT5_MI2S_RX},
	{ AFE_PORT_ID_INT5_MI2S_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT5_MI2S_TX},
	{ AFE_PORT_ID_INT6_MI2S_RX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT6_MI2S_RX},
	{ AFE_PORT_ID_INT6_MI2S_TX, 0, {0}, {0}, 0, 0, 0, 0, {0},
	  LPASS_BE_INT6_MI2S_TX},
	{ AFE_LOOPBACK_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_AFE_LOOPBACK_TX},
	{ RT_PROXY_PORT_002_RX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_PROXY_RX},
	{ RT_PROXY_PORT_002_TX, 0, {0}, {0}, 0, 0, 0, 0, {0}, LPASS_BE_PROXY_TX},
};

/* Track ASM playback & capture sessions of DAI
 * Track LSM listen sessions
 */
static struct msm_pcm_routing_fdai_data
	fe_dai_map[MSM_FRONTEND_DAI_MAX][2] = {
	/* MULTIMEDIA1 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA2 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA3 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA4 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA5 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA6 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA7*/
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA8 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA9 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA10 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA11 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA12 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA13 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA14 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA15 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA16 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA17 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA18 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA19 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA20 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA28 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA29 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA30 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA31 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA32 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* MULTIMEDIA33 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* VOIP */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* AFE_RX */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* AFE_TX */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* VOICE_STUB */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* DTMF_RX */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* QCHAT */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* VOLTE_STUB */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* LSM1 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* LSM2 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* LSM3 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* LSM4 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* LSM5 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* LSM6 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* LSM7 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* LSM8 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* VOICE2_STUB */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* VOICEMMODE1 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
	/* VOICEMMODE2 */
	{{0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} },
	 {0, INVALID_SESSION, LEGACY_PCM_MODE, {NULL, NULL} } },
};

static unsigned long session_copp_map[MSM_FRONTEND_DAI_MAX][2]
				     [MSM_BACKEND_DAI_MAX];
static struct msm_pcm_routing_app_type_data app_type_cfg[MAX_APP_TYPES];
static struct msm_pcm_routing_app_type_data lsm_app_type_cfg[MAX_APP_TYPES];
static struct msm_pcm_stream_app_type_cfg
	fe_dai_app_type_cfg[MSM_FRONTEND_DAI_MAX][2][MSM_BACKEND_DAI_MAX];

static int last_be_id_configured[MSM_FRONTEND_DAI_MAX][MAX_SESSION_TYPES];

/* The caller of this should aqcuire routing lock */
void msm_pcm_routing_get_bedai_info(int be_idx,
				    struct msm_pcm_routing_bdai_data *be_dai)
{
	if (be_idx >= 0 && be_idx < MSM_BACKEND_DAI_MAX)
		memcpy(be_dai, &msm_bedais[be_idx],
		       sizeof(struct msm_pcm_routing_bdai_data));
}

/* The caller of this should aqcuire routing lock */
void msm_pcm_routing_get_fedai_info(int fe_idx, int sess_type,
				    struct msm_pcm_routing_fdai_data *fe_dai)
{
	if ((sess_type == SESSION_TYPE_TX) || (sess_type == SESSION_TYPE_RX))
		memcpy(fe_dai, &fe_dai_map[fe_idx][sess_type],
		       sizeof(struct msm_pcm_routing_fdai_data));
}

void msm_pcm_routing_acquire_lock(void)
{
	mutex_lock(&routing_lock);
}

void msm_pcm_routing_release_lock(void)
{
	mutex_unlock(&routing_lock);
}

static int msm_pcm_routing_get_app_type_idx(int app_type)
{
	int idx;

	pr_debug("%s: app_type: %d\n", __func__, app_type);
	for (idx = 0; idx < MAX_APP_TYPES; idx++) {
		if (app_type_cfg[idx].app_type == app_type)
			return idx;
	}
	pr_info("%s: App type not available, fallback to default\n", __func__);
	return 0;
}

static int msm_pcm_routing_get_lsm_app_type_idx(int app_type)
{
	int idx;

	pr_debug("%s: app_type: %d\n", __func__, app_type);
	for (idx = 0; idx < MAX_APP_TYPES; idx++) {
		if (lsm_app_type_cfg[idx].app_type == app_type)
			return idx;
	}
	pr_debug("%s: App type not available, fallback to default\n", __func__);
	return 0;
}

static int get_port_id(int port_id)
{
	return (port_id == AFE_LOOPBACK_TX ? msm_ec_ref_port_id : port_id);
}

static bool is_mm_lsm_fe_id(int fe_id)
{
	bool rc = true;

	if (fe_id > MSM_FRONTEND_DAI_MM_MAX_ID &&
		((fe_id < MSM_FRONTEND_DAI_LSM1) ||
		 (fe_id > MSM_FRONTEND_DAI_LSM8))) {
		rc = false;
	}
	return rc;
}

/*
 * msm_pcm_routing_send_chmix_cfg
 *
 * Receives fe_id, ip_channel_cnt, op_channel_cnt, channel weight, session_type
 * use_default_chmap and channel map to map in channel mixer and send to
 * adm programmable matrix.
 *
 * fe_id - Passed value, frontend id which is wanted
 * ip_channel_cnt - Passed value, number of input channels
 * op_channel_cnt - Passed value, number of output channels
 * ch_wght_coeff - Passed reference, weights for each output channel
 * session_type - Passed value, session_type for RX or TX
 * use_default_chmap - true if default channel map  to be used
 * ch_map - input/output channel map for playback/capture session respectively
 */

int msm_pcm_routing_send_chmix_cfg(int fe_id, int ip_channel_cnt,
				   int op_channel_cnt, int *ch_wght_coeff,
				   int session_type, bool use_default_chmap,
				   char *channel_map)
{
	int rc = 0, idx = 0, i, j;
	int be_index = 0, port_id, index = 0;
	unsigned int session_id = 0;

	pr_debug("%s: fe_id[%d] ip_ch[%d] op_ch[%d] sess_type [%d]\n",
		 __func__, fe_id, ip_channel_cnt, op_channel_cnt, session_type);

	if (!use_default_chmap && (channel_map == NULL)) {
		pr_err("%s: No valid chan map and can't use default\n",
			__func__);
		return -EINVAL;
	}

	if ((ch_wght_coeff == NULL) || (op_channel_cnt > ADM_MAX_CHANNELS) ||
	     (ip_channel_cnt > ADM_MAX_CHANNELS)) {
		pr_err("%s: Invalid channels or null coefficients\n", __func__);
		return -EINVAL;
	}

	for (be_index = 0; be_index < MSM_BACKEND_DAI_MAX; be_index++) {
		port_id = msm_bedais[be_index].port_id;
		if (!msm_bedais[be_index].active ||
			!test_bit(fe_id, &msm_bedais[be_index].fe_sessions[0]))
			continue;

		session_id = fe_dai_map[fe_id][session_type].strm_id;
		channel_mixer[fe_id].input_channels[0] = ip_channel_cnt;
		channel_mixer[fe_id].output_channel = op_channel_cnt;
		channel_mixer[fe_id].rule = 0;

		for (j = 0; j < op_channel_cnt; j++) {
			for (i = 0; i < ip_channel_cnt; i++)
				channel_mixer[fe_id].channel_weight[j][i] =
						ch_wght_coeff[index++];
		}
		for (idx = 0; idx < MAX_COPPS_PER_PORT; idx++) {
			unsigned long copp =
				session_copp_map[fe_id][session_type][be_index];
			if (!test_bit(idx, &copp))
				continue;
			rc = adm_programable_channel_mixer(port_id,
					idx, session_id, session_type,
					channel_mixer + fe_id, 0,
					use_default_chmap, channel_map);
			if (rc < 0)
				pr_err("%s: err setting channel mix config\n",
					__func__);
		}
	}
	return 0;
}

int msm_pcm_routing_reg_stream_app_type_cfg(
	int fedai_id, int session_type, int be_id,
	struct msm_pcm_stream_app_type_cfg *cfg_data)
{
	int ret = 0;

	if (cfg_data == NULL) {
		pr_err("%s: Received NULL pointer for cfg_data\n", __func__);
		ret = -EINVAL;
		goto done;
	}

	pr_debug("%s: fedai_id %d, session_type %d, be_id %d, app_type %d, acdb_dev_id %d, sample_rate %d\n",
		__func__, fedai_id, session_type, be_id,
		cfg_data->app_type, cfg_data->acdb_dev_id,
		cfg_data->sample_rate);

	if (!is_mm_lsm_fe_id(fedai_id)) {
		pr_err("%s: Invalid machine driver ID %d\n",
			__func__, fedai_id);
		ret = -EINVAL;
		goto done;
	}
	if (session_type != SESSION_TYPE_RX &&
		session_type != SESSION_TYPE_TX) {
		pr_err("%s: Invalid session type %d\n",
			__func__, session_type);
		ret = -EINVAL;
		goto done;
	}
	if (be_id < 0 || be_id >= MSM_BACKEND_DAI_MAX) {
		pr_err("%s: Received out of bounds be_id %d\n",
			__func__, be_id);
		ret = -EINVAL;
		goto done;
	}

	fe_dai_app_type_cfg[fedai_id][session_type][be_id] = *cfg_data;

	/*
	 * Store the BE ID of the configuration information set as the latest so
	 * the get mixer control knows what to return.
	 */
	last_be_id_configured[fedai_id][session_type] = be_id;

done:
	return ret;
}
EXPORT_SYMBOL(msm_pcm_routing_reg_stream_app_type_cfg);

/**
 * msm_pcm_routing_get_stream_app_type_cfg
 *
 * Receives fedai_id, session_type, be_id, and populates app_type,
 * acdb_dev_id, & sample rate. Returns 0 on success. On failure returns
 * -EINVAL and does not alter passed values.
 *
 * fedai_id - Passed value, front end ID for which app type config is wanted
 * session_type - Passed value, session type for which app type config
 *                is wanted
 * be_id - Returned value, back end device id the app type config data is for
 * cfg_data - Returned value, configuration data used by app type config
 */
int msm_pcm_routing_get_stream_app_type_cfg(
	int fedai_id, int session_type, int *bedai_id,
	struct msm_pcm_stream_app_type_cfg *cfg_data)
{
	int be_id;
	int ret = 0;

	if (bedai_id == NULL) {
		pr_err("%s: Received NULL pointer for backend ID\n", __func__);
		ret = -EINVAL;
		goto done;
	} else if (cfg_data == NULL) {
		pr_err("%s: NULL pointer sent for cfg_data\n", __func__);
		ret = -EINVAL;
		goto done;
	} else if (!is_mm_lsm_fe_id(fedai_id)) {
		pr_err("%s: Invalid FE ID %d\n", __func__, fedai_id);
		ret = -EINVAL;
		goto done;
	} else if (session_type != SESSION_TYPE_RX &&
		   session_type != SESSION_TYPE_TX) {
		pr_err("%s: Invalid session type %d\n", __func__, session_type);
		ret = -EINVAL;
		goto done;
	}

	be_id = last_be_id_configured[fedai_id][session_type];
	if (be_id < 0 || be_id >= MSM_BACKEND_DAI_MAX) {
		pr_err("%s: Invalid BE ID %d\n", __func__, be_id);
		ret = -EINVAL;
		goto done;
	}

	*bedai_id = be_id;
	*cfg_data = fe_dai_app_type_cfg[fedai_id][session_type][be_id];
	pr_debug("%s: fedai_id %d, session_type %d, be_id %d, app_type %d, acdb_dev_id %d, sample_rate %d\n",
		__func__, fedai_id, session_type, *bedai_id,
		cfg_data->app_type, cfg_data->acdb_dev_id,
		cfg_data->sample_rate);
done:
	return ret;
}
EXPORT_SYMBOL(msm_pcm_routing_get_stream_app_type_cfg);

static struct cal_block_data *msm_routing_find_topology_by_path(int path,
								int cal_index)
{
	struct list_head		*ptr, *next;
	struct cal_block_data		*cal_block = NULL;
	pr_debug("%s\n", __func__);

	list_for_each_safe(ptr, next,
		&cal_data[cal_index]->cal_blocks) {

		cal_block = list_entry(ptr,
			struct cal_block_data, list);

		if (cal_utils_is_cal_stale(cal_block))
			continue;

		if (((struct audio_cal_info_adm_top *)cal_block
			->cal_info)->path == path) {
			return cal_block;
		}
	}
	pr_debug("%s: Can't find topology for path %d\n", __func__, path);
	return NULL;
}

static struct cal_block_data *msm_routing_find_topology(int path,
							int app_type,
							int acdb_id,
							int cal_index,
							bool exact)
{
	struct list_head *ptr, *next;
	struct cal_block_data *cal_block = NULL;
	struct audio_cal_info_adm_top *cal_info;

	pr_debug("%s\n", __func__);

	list_for_each_safe(ptr, next,
		&cal_data[cal_index]->cal_blocks) {

		cal_block = list_entry(ptr,
			struct cal_block_data, list);

		if (cal_utils_is_cal_stale(cal_block))
			continue;

		cal_info = (struct audio_cal_info_adm_top *)
			cal_block->cal_info;
		if ((cal_info->path == path)  &&
			(cal_info->app_type == app_type) &&
			(cal_info->acdb_id == acdb_id)) {
			return cal_block;
		}
	}
	pr_debug("%s: Can't find topology for path %d, app %d, "
		 "acdb_id %d %s\n",  __func__, path, app_type, acdb_id,
		 exact ? "fail" : "defaulting to search by path");
	return exact ? NULL : msm_routing_find_topology_by_path(path,
								cal_index);
}

static int msm_routing_find_topology_on_index(int session_type, int app_type,
					      int acdb_dev_id,  int idx,
					      bool exact)
{
	int topology = -EINVAL;
	struct cal_block_data *cal_block = NULL;

	mutex_lock(&cal_data[idx]->lock);
	cal_block = msm_routing_find_topology(session_type, app_type,
					      acdb_dev_id, idx, exact);
	if (cal_block != NULL) {
		topology = ((struct audio_cal_info_adm_top *)
			    cal_block->cal_info)->topology;
	}
	mutex_unlock(&cal_data[idx]->lock);
	return topology;
}

/*
 * Retrieving cal_block will mark cal_block as stale.
 * Hence it cannot be reused or resent unless the flag
 * is reset.
 */
static int msm_routing_get_adm_topology(int fedai_id, int session_type,
					int be_id)
{
	int topology = NULL_COPP_TOPOLOGY;
	int app_type = 0, acdb_dev_id = 0;

	pr_debug("%s: fedai_id %d, session_type %d, be_id %d\n",
	       __func__, fedai_id, session_type, be_id);

	if (cal_data == NULL)
		goto done;

	app_type = fe_dai_app_type_cfg[fedai_id][session_type][be_id].app_type;
	acdb_dev_id =
		fe_dai_app_type_cfg[fedai_id][session_type][be_id].acdb_dev_id;
	pr_debug("%s: Check for exact LSM topology\n", __func__);
	topology = msm_routing_find_topology_on_index(session_type,
					       app_type,
					       acdb_dev_id,
					       ADM_LSM_TOPOLOGY_CAL_TYPE_IDX,
					       true /*exact*/);
	if (topology < 0) {
		pr_debug("%s: Check for compatible topology\n", __func__);
		topology = msm_routing_find_topology_on_index(session_type,
						      app_type,
						      acdb_dev_id,
						      ADM_TOPOLOGY_CAL_TYPE_IDX,
						      false /*exact*/);
		if (topology < 0)
			topology = NULL_COPP_TOPOLOGY;
	}
done:
	pr_debug("%s: Using topology %d\n", __func__, topology);
	return topology;
}

static uint8_t is_be_dai_extproc(int be_dai)
{
	if (be_dai == MSM_BACKEND_DAI_EXTPROC_RX ||
	   be_dai == MSM_BACKEND_DAI_EXTPROC_TX ||
	   be_dai == MSM_BACKEND_DAI_EXTPROC_EC_TX)
		return 1;
	else
		return 0;
}

static void msm_pcm_routing_build_matrix(int fedai_id, int sess_type,
					 int path_type, int perf_mode,
					 uint32_t passthr_mode)
{
	int i, port_type, j, num_copps = 0;
	struct route_payload payload;

	port_type = ((path_type == ADM_PATH_PLAYBACK ||
		      path_type == ADM_PATH_COMPRESSED_RX) ?
		MSM_AFE_PORT_TYPE_RX : MSM_AFE_PORT_TYPE_TX);

	for (i = 0; i < MSM_BACKEND_DAI_MAX; i++) {
		if (!is_be_dai_extproc(i) &&
		   (afe_get_port_type(msm_bedais[i].port_id) == port_type) &&
		   (msm_bedais[i].active) &&
		   (test_bit(fedai_id, &msm_bedais[i].fe_sessions[0]))) {
			int port_id = get_port_id(msm_bedais[i].port_id);
			for (j = 0; j < MAX_COPPS_PER_PORT; j++) {
				unsigned long copp =
				      session_copp_map[fedai_id][sess_type][i];
				if (test_bit(j, &copp)) {
					payload.port_id[num_copps] = port_id;
					payload.copp_idx[num_copps] = j;
					payload.app_type[num_copps] =
						fe_dai_app_type_cfg
							[fedai_id][sess_type][i]
								.app_type;
					payload.acdb_dev_id[num_copps] =
						fe_dai_app_type_cfg
							[fedai_id][sess_type][i]
								.acdb_dev_id;
					payload.sample_rate[num_copps] =
						fe_dai_app_type_cfg
							[fedai_id][sess_type][i]
								.sample_rate;
					num_copps++;
				}
			}
		}
	}

	if (num_copps) {
		payload.num_copps = num_copps;
		payload.session_id = fe_dai_map[fedai_id][sess_type].strm_id;
		adm_matrix_map(path_type, payload, perf_mode, passthr_mode);
		msm_pcm_routng_cfg_matrix_map_pp(payload, path_type, perf_mode);
	}
}

void msm_pcm_routing_reg_psthr_stream(int fedai_id, int dspst_id,
				      int stream_type)
{
	int i, session_type, path_type, port_type;
	u32 mode = 0;

	if (fedai_id > MSM_FRONTEND_DAI_MM_MAX_ID) {
		/* bad ID assigned in machine driver */
		pr_err("%s: bad MM ID\n", __func__);
		return;
	}

	if (stream_type == SNDRV_PCM_STREAM_PLAYBACK) {
		session_type = SESSION_TYPE_RX;
		path_type = ADM_PATH_PLAYBACK;
		port_type = MSM_AFE_PORT_TYPE_RX;
	} else {
		session_type = SESSION_TYPE_TX;
		path_type = ADM_PATH_LIVE_REC;
		port_type = MSM_AFE_PORT_TYPE_TX;
	}

	mutex_lock(&routing_lock);

	fe_dai_map[fedai_id][session_type].strm_id = dspst_id;
	for (i = 0; i < MSM_BACKEND_DAI_MAX; i++) {
		if (!is_be_dai_extproc(i) &&
		    (afe_get_port_type(msm_bedais[i].port_id) == port_type) &&
		    (msm_bedais[i].active) &&
		    (test_bit(fedai_id, &msm_bedais[i].fe_sessions[0]))) {
			mode = afe_get_port_type(msm_bedais[i].port_id);
			adm_connect_afe_port(mode, dspst_id,
					     msm_bedais[i].port_id);
			break;
		}
	}
	mutex_unlock(&routing_lock);
}

static bool route_check_fe_id_adm_support(int fe_id)
{
	bool rc = true;

	if ((fe_id >= MSM_FRONTEND_DAI_LSM1) &&
		 (fe_id <= MSM_FRONTEND_DAI_LSM8)) {
		/* fe id is listen while port is set to afe */
		if (lsm_port_index != ADM_LSM_PORT_INDEX) {
			pr_debug("%s: fe_id %d, lsm mux slim port %d\n",
				__func__, fe_id, lsm_port_index);
			rc = false;
		}
	}

	return rc;
}

int msm_pcm_routing_reg_phy_compr_stream(int fe_id, int perf_mode,
					  int dspst_id, int stream_type,
					  uint32_t passthr_mode)
{
	int i, j, session_type, path_type, port_type, topology;
	int num_copps = 0;
	struct route_payload payload;
	u32 channels, sample_rate;
	u16 bit_width = 16;
	bool is_lsm;

	pr_debug("%s:fe_id[%d] perf_mode[%d] id[%d] stream_type[%d] passt[%d]",
		 __func__, fe_id, perf_mode, dspst_id,
		 stream_type, passthr_mode);
	if (!is_mm_lsm_fe_id(fe_id)) {
		/* bad ID assigned in machine driver */
		pr_err("%s: bad MM ID %d\n", __func__, fe_id);
		return -EINVAL;
	}

	if (!route_check_fe_id_adm_support(fe_id)) {
		/* ignore adm open if not supported for fe_id */
		pr_debug("%s: No ADM support for fe id %d\n", __func__, fe_id);
		return 0;
	}

	if (stream_type == SNDRV_PCM_STREAM_PLAYBACK) {
		session_type = SESSION_TYPE_RX;
		if (passthr_mode != LEGACY_PCM)
			path_type = ADM_PATH_COMPRESSED_RX;
		else
			path_type = ADM_PATH_PLAYBACK;
		port_type = MSM_AFE_PORT_TYPE_RX;
	} else if (stream_type == SNDRV_PCM_STREAM_CAPTURE) {
		session_type = SESSION_TYPE_TX;
		if ((passthr_mode != LEGACY_PCM) && (passthr_mode != LISTEN))
			path_type = ADM_PATH_COMPRESSED_TX;
		else
			path_type = ADM_PATH_LIVE_REC;
		port_type = MSM_AFE_PORT_TYPE_TX;
	} else {
		pr_err("%s: invalid stream type %d\n", __func__, stream_type);
		return -EINVAL;
	}

	is_lsm = (fe_id >= MSM_FRONTEND_DAI_LSM1) &&
			 (fe_id <= MSM_FRONTEND_DAI_LSM8);
	mutex_lock(&routing_lock);

	payload.num_copps = 0; /* only RX needs to use payload */
	fe_dai_map[fe_id][session_type].strm_id = dspst_id;
	/* re-enable EQ if active */
	msm_qti_pp_send_eq_values(fe_id);
	for (i = 0; i < MSM_BACKEND_DAI_MAX; i++) {
		if (test_bit(fe_id, &msm_bedais[i].fe_sessions[0]))
			msm_bedais[i].passthr_mode[fe_id] = passthr_mode;

		if (!is_be_dai_extproc(i) &&
			(afe_get_port_type(msm_bedais[i].port_id) ==
			port_type) &&
			(msm_bedais[i].active) &&
			(test_bit(fe_id, &msm_bedais[i].fe_sessions[0]))) {
			int app_type, app_type_idx, copp_idx, acdb_dev_id;
			int port_id = get_port_id(msm_bedais[i].port_id);

			/*
			 * check if ADM needs to be configured with different
			 * channel mapping than backend
			 */
			if (!msm_bedais[i].adm_override_ch)
				channels = msm_bedais[i].channel;
			else
				channels = msm_bedais[i].adm_override_ch;

			bit_width = msm_routing_get_bit_width(
						msm_bedais[i].format);
			app_type =
			fe_dai_app_type_cfg[fe_id][session_type][i].app_type;
			if (app_type && is_lsm) {
				app_type_idx =
				msm_pcm_routing_get_lsm_app_type_idx(app_type);
				sample_rate =
				fe_dai_app_type_cfg[fe_id][session_type][i]
					.sample_rate;
				bit_width =
				lsm_app_type_cfg[app_type_idx].bit_width;
			} else if (app_type) {
				app_type_idx =
					msm_pcm_routing_get_app_type_idx(
						app_type);
				sample_rate =
			fe_dai_app_type_cfg[fe_id][session_type][i].sample_rate;
				bit_width =
					app_type_cfg[app_type_idx].bit_width;
			} else {
				sample_rate = msm_bedais[i].sample_rate;
			}
			acdb_dev_id =
			fe_dai_app_type_cfg[fe_id][session_type][i].acdb_dev_id;
			topology = msm_routing_get_adm_topology(fe_id,
								session_type,
								i);
			if ((passthr_mode == COMPRESSED_PASSTHROUGH_DSD)
			     || (passthr_mode ==
			     COMPRESSED_PASSTHROUGH_GEN))
				topology = COMPRESSED_PASSTHROUGH_NONE_TOPOLOGY;
			pr_debug("%s: Before adm open topology %d\n", __func__,
				topology);

			copp_idx =
				adm_open(port_id, path_type, sample_rate,
					 channels, topology, perf_mode,
					 bit_width, app_type, acdb_dev_id,
					 session_type);
			if ((copp_idx < 0) ||
				(copp_idx >= MAX_COPPS_PER_PORT)) {
				pr_err("%s:adm open failed coppid:%d\n",
				__func__, copp_idx);
				mutex_unlock(&routing_lock);
				return -EINVAL;
			}
			pr_debug("%s: set idx bit of fe:%d, type: %d, be:%d\n",
				 __func__, fe_id, session_type, i);
			set_bit(copp_idx,
				&session_copp_map[fe_id][session_type][i]);

			if (msm_is_resample_needed(
				sample_rate,
				msm_bedais[i].sample_rate))
				adm_copp_mfc_cfg(port_id, copp_idx,
					msm_bedais[i].sample_rate);

			for (j = 0; j < MAX_COPPS_PER_PORT; j++) {
				unsigned long copp =
				session_copp_map[fe_id][session_type][i];
				if (test_bit(j, &copp)) {
					payload.port_id[num_copps] = port_id;
					payload.copp_idx[num_copps] = j;
					payload.app_type[num_copps] =
						fe_dai_app_type_cfg
							[fe_id][session_type][i]
								.app_type;
					payload.acdb_dev_id[num_copps] =
						fe_dai_app_type_cfg
							[fe_id][session_type][i]
								.acdb_dev_id;
					payload.sample_rate[num_copps] =
						fe_dai_app_type_cfg
							[fe_id][session_type][i]
								.sample_rate;
					num_copps++;
				}
			}
			if (passthr_mode != COMPRESSED_PASSTHROUGH_DSD
			    && passthr_mode !=
			    COMPRESSED_PASSTHROUGH_GEN) {
				msm_routing_send_device_pp_params(port_id,
				copp_idx, fe_id);
			}
		}
	}
	if (num_copps) {
		payload.num_copps = num_copps;
		payload.session_id = fe_dai_map[fe_id][session_type].strm_id;
		adm_matrix_map(path_type, payload, perf_mode, passthr_mode);
		msm_pcm_routng_cfg_matrix_map_pp(payload, path_type, perf_mode);
	}
	mutex_unlock(&routing_lock);
	return 0;
}

static u32 msm_pcm_routing_get_voc_sessionid(u16 val)
{
	u32 session_id;

	switch (val) {
	case MSM_FRONTEND_DAI_QCHAT:
		session_id = voc_get_session_id(QCHAT_SESSION_NAME);
		break;
	case MSM_FRONTEND_DAI_VOIP:
		session_id = voc_get_session_id(VOIP_SESSION_NAME);
		break;
	case MSM_FRONTEND_DAI_VOICEMMODE1:
		session_id = voc_get_session_id(VOICEMMODE1_NAME);
		break;
	case MSM_FRONTEND_DAI_VOICEMMODE2:
		session_id = voc_get_session_id(VOICEMMODE2_NAME);
		break;
	default:
		session_id = 0;
	}

	pr_debug("%s session_id 0x%x", __func__, session_id);
	return session_id;
}

static int msm_pcm_routing_channel_mixer(int fe_id, bool perf_mode,
				int dspst_id, int stream_type)
{
	int copp_idx = 0;
	int sess_type = 0;
	int i = 0, j = 0, be_id;
	int ret = 0;
	bool use_default_chmap = true;
	char *ch_map = NULL;

	if (fe_id >= MSM_FRONTEND_DAI_MM_SIZE) {
		pr_err("%s: invalid FE %d\n", __func__, fe_id);
		return 0;
	}

	if (!(channel_mixer[fe_id].enable)) {
		pr_debug("%s: channel mixer not enabled for FE %d\n",
			__func__, fe_id);
		return 0;
	}

	if (stream_type == SNDRV_PCM_STREAM_PLAYBACK)
		sess_type = SESSION_TYPE_RX;
	else
		sess_type = SESSION_TYPE_TX;

	for (i = 0; i < ADM_MAX_CHANNELS && channel_input[fe_id][i] > 0;
		++i) {
		be_id = channel_input[fe_id][i] - 1;
		channel_mixer[fe_id].input_channels[i] =
						msm_bedais[be_id].channel;

		if ((msm_bedais[be_id].active) &&
			test_bit(fe_id,
			&msm_bedais[be_id].fe_sessions[0])) {
			unsigned long copp =
				session_copp_map[fe_id][sess_type][be_id];
			for (j = 0; j < MAX_COPPS_PER_PORT; j++) {
				if (test_bit(j, &copp)) {
					copp_idx = j;
					break;
				}
			}

			pr_debug("%s: fe %d, be %d, channel %d, copp %d\n",
				__func__,
				fe_id, be_id, msm_bedais[be_id].channel,
				copp_idx);
			ret = adm_programable_channel_mixer(
					msm_bedais[be_id].port_id,
					copp_idx, dspst_id, sess_type,
					channel_mixer + fe_id, i,
					use_default_chmap, ch_map);
		}
	}

	return ret;
}

int msm_pcm_routing_reg_phy_stream(int fedai_id, int perf_mode,
					int dspst_id, int stream_type)
{
	int i, j, session_type, path_type, port_type, topology, num_copps = 0;
	struct route_payload payload;
	u32 channels, sample_rate;
	uint16_t bits_per_sample = 16;
	uint32_t passthr_mode = LEGACY_PCM;
	int ret = 0;

	if (fedai_id > MSM_FRONTEND_DAI_MM_MAX_ID) {
		/* bad ID assigned in machine driver */
		pr_err("%s: bad MM ID %d\n", __func__, fedai_id);
		return -EINVAL;
	}

	if (stream_type == SNDRV_PCM_STREAM_PLAYBACK) {
		session_type = SESSION_TYPE_RX;
		path_type = ADM_PATH_PLAYBACK;
		port_type = MSM_AFE_PORT_TYPE_RX;
	} else {
		session_type = SESSION_TYPE_TX;
		path_type = ADM_PATH_LIVE_REC;
		port_type = MSM_AFE_PORT_TYPE_TX;
	}

	mutex_lock(&routing_lock);

	payload.num_copps = 0; /* only RX needs to use payload */
	fe_dai_map[fedai_id][session_type].strm_id = dspst_id;
	fe_dai_map[fedai_id][session_type].perf_mode = perf_mode;

	/* re-enable EQ if active */
	msm_qti_pp_send_eq_values(fedai_id);
	for (i = 0; i < MSM_BACKEND_DAI_MAX; i++) {
		if (!is_be_dai_extproc(i) &&
		   (afe_get_port_type(msm_bedais[i].port_id) == port_type) &&
		   (msm_bedais[i].active) &&
		   (test_bit(fedai_id, &msm_bedais[i].fe_sessions[0]))) {
			int app_type, app_type_idx, copp_idx, acdb_dev_id;
			int port_id = get_port_id(msm_bedais[i].port_id);

			/*
			 * check if ADM needs to be configured with different
			 * channel mapping than backend
			 */
			if (!msm_bedais[i].adm_override_ch)
				channels = msm_bedais[i].channel;
			else
				channels = msm_bedais[i].adm_override_ch;
			msm_bedais[i].passthr_mode[fedai_id] =
				LEGACY_PCM;

			bits_per_sample = msm_routing_get_bit_width(
						msm_bedais[i].format);

			app_type =
			fe_dai_app_type_cfg[fedai_id][session_type][i].app_type;
			if (app_type) {
				app_type_idx =
				msm_pcm_routing_get_app_type_idx(app_type);
				sample_rate =
				fe_dai_app_type_cfg[fedai_id][session_type][i]
					.sample_rate;
				bits_per_sample =
					app_type_cfg[app_type_idx].bit_width;
			} else
				sample_rate = msm_bedais[i].sample_rate;

			acdb_dev_id =
			fe_dai_app_type_cfg[fedai_id][session_type][i]
				.acdb_dev_id;
			topology = msm_routing_get_adm_topology(fedai_id,
								session_type,
								i);
			copp_idx = adm_open(port_id, path_type,
					    sample_rate, channels, topology,
					    perf_mode, bits_per_sample,
					    app_type, acdb_dev_id,
					    session_type);
			if ((copp_idx < 0) ||
				(copp_idx >= MAX_COPPS_PER_PORT)) {
				pr_err("%s: adm open failed copp_idx:%d\n",
				       __func__, copp_idx);
				mutex_unlock(&routing_lock);
				return -EINVAL;
			}
			pr_debug("%s: setting idx bit of fe:%d, type: %d, be:%d\n",
				 __func__, fedai_id, session_type, i);
			set_bit(copp_idx,
				&session_copp_map[fedai_id][session_type][i]);

			if (msm_is_resample_needed(
				sample_rate,
				msm_bedais[i].sample_rate))
				adm_copp_mfc_cfg(port_id, copp_idx,
					msm_bedais[i].sample_rate);

			for (j = 0; j < MAX_COPPS_PER_PORT; j++) {
				unsigned long copp =
				    session_copp_map[fedai_id][session_type][i];
				if (test_bit(j, &copp)) {
					payload.port_id[num_copps] = port_id;
					payload.copp_idx[num_copps] = j;
					payload.app_type[num_copps] =
						fe_dai_app_type_cfg
							[fedai_id][session_type]
							[i].app_type;
					payload.acdb_dev_id[num_copps] =
						fe_dai_app_type_cfg
							[fedai_id][session_type]
							[i].acdb_dev_id;
					payload.sample_rate[num_copps] =
						fe_dai_app_type_cfg
							[fedai_id][session_type]
							[i].sample_rate;
					num_copps++;
				}
			}
			if ((perf_mode == LEGACY_PCM_MODE) &&
				(msm_bedais[i].passthr_mode[fedai_id] ==
				LEGACY_PCM))
				msm_pcm_routing_cfg_pp(port_id, copp_idx,
						       topology, channels);
		}
	}
	if (num_copps) {
		payload.num_copps = num_copps;
		payload.session_id = fe_dai_map[fedai_id][session_type].strm_id;
		adm_matrix_map(path_type, payload, perf_mode, passthr_mode);
		msm_pcm_routng_cfg_matrix_map_pp(payload, path_type, perf_mode);
	}

	ret = msm_pcm_routing_channel_mixer(fedai_id, perf_mode,
				dspst_id, stream_type);
	mutex_unlock(&routing_lock);
	return ret;
}

int msm_pcm_routing_reg_phy_stream_v2(int fedai_id, int perf_mode,
				      int dspst_id, int stream_type,
				      struct msm_pcm_routing_evt event_info)
{
	if (msm_pcm_routing_reg_phy_stream(fedai_id, perf_mode, dspst_id,
				       stream_type)) {
		pr_err("%s: failed to reg phy stream\n", __func__);
		return -EINVAL;
	}

	if (stream_type == SNDRV_PCM_STREAM_PLAYBACK)
		fe_dai_map[fedai_id][SESSION_TYPE_RX].event_info = event_info;
	else
		fe_dai_map[fedai_id][SESSION_TYPE_TX].event_info = event_info;
	return 0;
}

void msm_pcm_routing_dereg_phy_stream(int fedai_id, int stream_type)
{
	int i, port_type, session_type, path_type, topology, port_id;
	struct msm_pcm_routing_fdai_data *fdai;

	if (!is_mm_lsm_fe_id(fedai_id)) {
		/* bad ID assigned in machine driver */
		pr_err("%s: bad MM ID\n", __func__);
		return;
	}

	if (stream_type == SNDRV_PCM_STREAM_PLAYBACK) {
		port_type = MSM_AFE_PORT_TYPE_RX;
		session_type = SESSION_TYPE_RX;
		path_type = ADM_PATH_PLAYBACK;
	} else {
		port_type = MSM_AFE_PORT_TYPE_TX;
		session_type = SESSION_TYPE_TX;
		path_type = ADM_PATH_LIVE_REC;
	}

	mutex_lock(&routing_lock);
	for (i = 0; i < MSM_BACKEND_DAI_MAX; i++) {
		if (!is_be_dai_extproc(i) &&
		   (afe_get_port_type(msm_bedais[i].port_id) == port_type) &&
		   (msm_bedais[i].active) &&
		   (test_bit(fedai_id, &msm_bedais[i].fe_sessions[0]))) {
			int idx;
			unsigned long copp =
				session_copp_map[fedai_id][session_type][i];
			fdai = &fe_dai_map[fedai_id][session_type];

			for (idx = 0; idx < MAX_COPPS_PER_PORT; idx++)
				if (test_bit(idx, &copp))
					break;

			if (idx >= MAX_COPPS_PER_PORT || idx < 0) {
				pr_debug("%s: copp idx is invalid, exiting\n",
								__func__);
				continue;
			}
			port_id = get_port_id(msm_bedais[i].port_id);
			topology = adm_get_topology_for_port_copp_idx(
					port_id, idx);
			adm_close(port_id, fdai->perf_mode, idx);
			pr_debug("%s:copp:%ld,idx bit fe:%d,type:%d,be:%d\n",
				 __func__, copp, fedai_id, session_type, i);
			clear_bit(idx,
				  &session_copp_map[fedai_id][session_type][i]);
			if ((topology == DOLBY_ADM_COPP_TOPOLOGY_ID ||
				topology == DS2_ADM_COPP_TOPOLOGY_ID) &&
			    (fdai->perf_mode == LEGACY_PCM_MODE) &&
			    (msm_bedais[i].passthr_mode[fedai_id] ==
					LEGACY_PCM))
				msm_pcm_routing_deinit_pp(port_id, topology);
		}
	}

	fe_dai_map[fedai_id][session_type].strm_id = INVALID_SESSION;
	fe_dai_map[fedai_id][session_type].be_srate = 0;
	mutex_unlock(&routing_lock);
}

/* Check if FE/BE route is set */
static bool msm_pcm_routing_route_is_set(u16 be_id, u16 fe_id)
{
	bool rc = false;

	if (!is_mm_lsm_fe_id(fe_id)) {
		/* recheck FE ID in the mixer control defined in this file */
		pr_err("%s: bad MM ID\n", __func__);
		return rc;
	}

	if (test_bit(fe_id, &msm_bedais[be_id].fe_sessions[0]))
		rc = true;

	return rc;
}

static void msm_pcm_routing_process_audio(u16 reg, u16 val, int set)
{
	int session_type, path_type, topology;
	u32 channels, sample_rate;
	uint16_t bits_per_sample = 16;
	struct msm_pcm_routing_fdai_data *fdai;
	uint32_t passthr_mode;
	bool is_lsm;

	pr_debug("%s: reg %x val %x set %x\n", __func__, reg, val, set);

	if (!is_mm_lsm_fe_id(val)) {
		/* recheck FE ID in the mixer control defined in this file */
		pr_err("%s: bad MM ID\n", __func__);
		return;
	}

	if (!route_check_fe_id_adm_support(val)) {
		/* ignore adm open if not supported for fe_id */
		pr_debug("%s: No ADM support for fe id %d\n", __func__, val);
		return;
	}

	passthr_mode = msm_bedais[reg].passthr_mode[val];
	if (afe_get_port_type(msm_bedais[reg].port_id) ==
		MSM_AFE_PORT_TYPE_RX) {
		session_type = SESSION_TYPE_RX;
		if (passthr_mode != LEGACY_PCM)
			path_type = ADM_PATH_COMPRESSED_RX;
		else
			path_type = ADM_PATH_PLAYBACK;
	} else {
		session_type = SESSION_TYPE_TX;
		if ((passthr_mode != LEGACY_PCM) && (passthr_mode != LISTEN))
			path_type = ADM_PATH_COMPRESSED_TX;
		else
			path_type = ADM_PATH_LIVE_REC;
	}
	is_lsm = (val >= MSM_FRONTEND_DAI_LSM1) &&
			 (val <= MSM_FRONTEND_DAI_LSM8);

	mutex_lock(&routing_lock);
	if (set) {
		if (!test_bit(val, &msm_bedais[reg].fe_sessions[0]) &&
			((msm_bedais[reg].port_id == VOICE_PLAYBACK_TX) ||
			(msm_bedais[reg].port_id == VOICE2_PLAYBACK_TX)))
			voc_start_playback(set, msm_bedais[reg].port_id);

		set_bit(val, &msm_bedais[reg].fe_sessions[0]);
		fdai = &fe_dai_map[val][session_type];
		if (msm_bedais[reg].active && fdai->strm_id !=
			INVALID_SESSION) {
			int app_type, app_type_idx, copp_idx, acdb_dev_id;
			int port_id = get_port_id(msm_bedais[reg].port_id);
			/*
			 * check if ADM needs to be configured with different
			 * channel mapping than backend
			 */
			if (!msm_bedais[reg].adm_override_ch)
				channels = msm_bedais[reg].channel;
			else
				channels = msm_bedais[reg].adm_override_ch;
			if (session_type == SESSION_TYPE_TX &&
			    fdai->be_srate &&
			    (fdai->be_srate != msm_bedais[reg].sample_rate)) {
				pr_debug("%s: flush strm %d diff BE rates\n",
					__func__, fdai->strm_id);

				if (fdai->event_info.event_func)
					fdai->event_info.event_func(
						MSM_PCM_RT_EVT_BUF_RECFG,
						fdai->event_info.priv_data);
				fdai->be_srate = 0; /* might not need it */
			}

			bits_per_sample = msm_routing_get_bit_width(
						msm_bedais[reg].format);

			app_type =
			fe_dai_app_type_cfg[val][session_type][reg].app_type;
			if (app_type && is_lsm) {
				app_type_idx =
				msm_pcm_routing_get_lsm_app_type_idx(app_type);
				sample_rate =
				fe_dai_app_type_cfg[val][session_type][reg]
					.sample_rate;
				bits_per_sample =
				lsm_app_type_cfg[app_type_idx].bit_width;
			} else if (app_type) {
				app_type_idx =
				msm_pcm_routing_get_app_type_idx(app_type);
				sample_rate =
				fe_dai_app_type_cfg[val][session_type][reg]
					.sample_rate;
				bits_per_sample =
					app_type_cfg[app_type_idx].bit_width;
			} else
				sample_rate = msm_bedais[reg].sample_rate;

			topology = msm_routing_get_adm_topology(val,
								session_type,
								reg);
			acdb_dev_id =
			fe_dai_app_type_cfg[val][session_type][reg].acdb_dev_id;
			copp_idx = adm_open(port_id, path_type,
					    sample_rate, channels, topology,
					    fdai->perf_mode, bits_per_sample,
					    app_type, acdb_dev_id,
					    session_type);
			if ((copp_idx < 0) ||
			    (copp_idx >= MAX_COPPS_PER_PORT)) {
				pr_err("%s: adm open failed\n", __func__);
				mutex_unlock(&routing_lock);
				return;
			}
			pr_debug("%s: setting idx bit of fe:%d, type: %d, be:%d\n",
				 __func__, val, session_type, reg);
			set_bit(copp_idx,
				&session_copp_map[val][session_type][reg]);

			if (msm_is_resample_needed(
				sample_rate,
				msm_bedais[reg].sample_rate))
				adm_copp_mfc_cfg(port_id, copp_idx,
					msm_bedais[reg].sample_rate);

			if (session_type == SESSION_TYPE_RX &&
			    fdai->event_info.event_func)
				fdai->event_info.event_func(
					MSM_PCM_RT_EVT_DEVSWITCH,
					fdai->event_info.priv_data);

			msm_pcm_routing_build_matrix(val, session_type,
						     path_type,
						     fdai->perf_mode,
						     passthr_mode);
			if ((fdai->perf_mode == LEGACY_PCM_MODE) &&
				(passthr_mode == LEGACY_PCM))
				msm_pcm_routing_cfg_pp(port_id, copp_idx,
						       topology, channels);
		}
	} else {
		if (test_bit(val, &msm_bedais[reg].fe_sessions[0]) &&
			((msm_bedais[reg].port_id == VOICE_PLAYBACK_TX) ||
			(msm_bedais[reg].port_id == VOICE2_PLAYBACK_TX)))
			voc_start_playback(set, msm_bedais[reg].port_id);
		clear_bit(val, &msm_bedais[reg].fe_sessions[0]);
		fdai = &fe_dai_map[val][session_type];
		if (msm_bedais[reg].active && fdai->strm_id !=
			INVALID_SESSION) {
			int idx;
			int port_id;
			unsigned long copp =
				session_copp_map[val][session_type][reg];
			for (idx = 0; idx < MAX_COPPS_PER_PORT; idx++)
				if (test_bit(idx, &copp))
					break;

			port_id = get_port_id(msm_bedais[reg].port_id);
			topology = adm_get_topology_for_port_copp_idx(port_id,
								      idx);
			adm_close(port_id, fdai->perf_mode, idx);
			pr_debug("%s: copp: %ld, reset idx bit fe:%d, type: %d, be:%d topology=0x%x\n",
				 __func__, copp, val, session_type, reg,
				 topology);
			clear_bit(idx,
				  &session_copp_map[val][session_type][reg]);
			if ((topology == DOLBY_ADM_COPP_TOPOLOGY_ID ||
				topology == DS2_ADM_COPP_TOPOLOGY_ID) &&
			    (fdai->perf_mode == LEGACY_PCM_MODE) &&
			    (passthr_mode == LEGACY_PCM))
				msm_pcm_routing_deinit_pp(port_id, topology);
			msm_pcm_routing_build_matrix(val, session_type,
						     path_type,
						     fdai->perf_mode,
						     passthr_mode);
		}
	}
	if ((msm_bedais[reg].port_id == VOICE_RECORD_RX)
			|| (msm_bedais[reg].port_id == VOICE_RECORD_TX))
		voc_start_record(msm_bedais[reg].port_id, set, voc_session_id);

	mutex_unlock(&routing_lock);
}

static int msm_routing_get_audio_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
	(struct soc_mixer_control *)kcontrol->private_value;

	if (test_bit(mc->rshift, &msm_bedais[mc->shift].fe_sessions[0]))
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	pr_debug("%s: shift %x rshift %x val %ld\n", __func__,
		mc->shift, mc->rshift,
		ucontrol->value.integer.value[0]);

	return 0;
}

static int msm_routing_put_audio_mixer(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_dapm_update *update = NULL;

	if (ucontrol->value.integer.value[0] &&
	   msm_pcm_routing_route_is_set(mc->shift, mc->rshift) == false) {
		msm_pcm_routing_process_audio(mc->shift, mc->rshift, 1);
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
			update);
	} else if (!ucontrol->value.integer.value[0] &&
		  msm_pcm_routing_route_is_set(mc->shift, mc->rshift) == true) {
		msm_pcm_routing_process_audio(mc->shift, mc->rshift, 0);
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
			update);
	}

	return 1;
}

static int msm_routing_get_listen_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
	(struct soc_mixer_control *)kcontrol->private_value;

	if (test_bit(mc->rshift, &msm_bedais[mc->shift].fe_sessions[0]))
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	pr_debug("%s: shift %x rshift %x val %ld\n", __func__,
		mc->shift, mc->rshift,
		ucontrol->value.integer.value[0]);

	return 0;
}

static int msm_routing_put_listen_mixer(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: shift %x rshift %x val %ld\n", __func__,
		mc->shift, mc->rshift,
		ucontrol->value.integer.value[0]);

	if (ucontrol->value.integer.value[0]) {
		if (msm_pcm_routing_route_is_set(mc->shift, mc->rshift)
								== false)
			msm_pcm_routing_process_audio(mc->shift, mc->rshift, 1);
		snd_soc_dapm_mixer_update_power(widget->dapm,
						kcontrol, 1, update);
	} else if (!ucontrol->value.integer.value[0]) {
		if (msm_pcm_routing_route_is_set(mc->shift, mc->rshift)
								== true)
			msm_pcm_routing_process_audio(mc->shift, mc->rshift, 0);
		snd_soc_dapm_mixer_update_power(widget->dapm,
						kcontrol, 0, update);
	}

	return 1;
}

static void msm_pcm_routing_process_voice(u16 reg, u16 val, int set)
{
	u32 session_id = 0;
	u16 path_type;
	struct media_format_info voc_be_media_format;

	pr_debug("%s: reg %x val %x set %x\n", __func__, reg, val, set);

	session_id = msm_pcm_routing_get_voc_sessionid(val);

	pr_debug("%s: FE DAI 0x%x session_id 0x%x\n",
		__func__, val, session_id);

	mutex_lock(&routing_lock);

	if (set)
		set_bit(val, &msm_bedais[reg].fe_sessions[0]);
	else
		clear_bit(val, &msm_bedais[reg].fe_sessions[0]);

	if (val == MSM_FRONTEND_DAI_DTMF_RX &&
	    afe_get_port_type(msm_bedais[reg].port_id) ==
						MSM_AFE_PORT_TYPE_RX) {
		pr_debug("%s(): set=%d port id=0x%x for dtmf generation\n",
			 __func__, set, msm_bedais[reg].port_id);
		afe_set_dtmf_gen_rx_portid(msm_bedais[reg].port_id, set);
	}

	if (afe_get_port_type(msm_bedais[reg].port_id) ==
						MSM_AFE_PORT_TYPE_RX)
		path_type = RX_PATH;
	else
		path_type = TX_PATH;

	if (set) {
		if (msm_bedais[reg].active) {
			voc_set_route_flag(session_id, path_type, 1);

			memset(&voc_be_media_format, 0,
			       sizeof(struct media_format_info));

			voc_be_media_format.port_id = msm_bedais[reg].port_id;
			voc_be_media_format.num_channels =
						msm_bedais[reg].channel;
			voc_be_media_format.sample_rate =
						msm_bedais[reg].sample_rate;
			voc_be_media_format.bits_per_sample =
						msm_bedais[reg].format;
			/* Defaulting this to 1 for voice call usecases */
			voc_be_media_format.channel_mapping[0] = 1;

			voc_set_device_config(session_id, path_type,
					      &voc_be_media_format);

			if (voc_get_route_flag(session_id, TX_PATH) &&
				voc_get_route_flag(session_id, RX_PATH))
				voc_enable_device(session_id);
		} else {
			pr_debug("%s BE is not active\n", __func__);
		}
	} else {
		voc_set_route_flag(session_id, path_type, 0);
		voc_disable_device(session_id);
	}

	mutex_unlock(&routing_lock);

}

static int msm_routing_get_voice_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
	(struct soc_mixer_control *)kcontrol->private_value;

	mutex_lock(&routing_lock);

	if (test_bit(mc->rshift, &msm_bedais[mc->shift].fe_sessions[0]))
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	mutex_unlock(&routing_lock);

	pr_debug("%s: shift %x rshift %x val %ld\n", __func__,
			mc->shift, mc->rshift,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int msm_routing_put_voice_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_dapm_update *update = NULL;

	if (ucontrol->value.integer.value[0]) {
		msm_pcm_routing_process_voice(mc->shift, mc->rshift, 1);
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
						update);
	} else {
		msm_pcm_routing_process_voice(mc->shift, mc->rshift, 0);
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
						update);
	}

	return 1;
}

static int msm_routing_get_voice_stub_mixer(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	mutex_lock(&routing_lock);

	if (test_bit(mc->rshift, &msm_bedais[mc->shift].fe_sessions[0]))
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	mutex_unlock(&routing_lock);

	pr_debug("%s: shift %x rshift %x val %ld\n", __func__,
		mc->shift, mc->rshift,
		ucontrol->value.integer.value[0]);

	return 0;
}

static int msm_routing_put_voice_stub_mixer(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_dapm_update *update = NULL;

	if (ucontrol->value.integer.value[0]) {
		mutex_lock(&routing_lock);
		set_bit(mc->rshift, &msm_bedais[mc->shift].fe_sessions[0]);
		mutex_unlock(&routing_lock);

		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
						update);
	} else {
		mutex_lock(&routing_lock);
		clear_bit(mc->rshift, &msm_bedais[mc->shift].fe_sessions[0]);
		mutex_unlock(&routing_lock);

		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
						update);
	}

	pr_debug("%s: shift %x rshift %x val %ld\n", __func__,
		mc->shift, mc->rshift,
		ucontrol->value.integer.value[0]);

	return 1;
}

/*
 * Return the mapping between port ID and backend ID to enable the AFE callback
 * to determine the acdb_dev_id from the port id
 */
int msm_pcm_get_be_id_from_port_id(int port_id)
{
	int i;
	int be_id = -EINVAL;

	for (i = 0; i < MSM_BACKEND_DAI_MAX; i++) {
		if (msm_bedais[i].port_id == port_id) {
			be_id = i;
			break;
		}
	}

	return be_id;
}

/*
 * Return the registered dev_acdb_id given a port ID to enable identifying the
 * correct AFE calibration information by comparing the header information.
 */
static int msm_pcm_get_dev_acdb_id_by_port_id(int port_id)
{
	int acdb_id = -EINVAL;
	int i = 0;
	int session;
	int port_type = afe_get_port_type(port_id);
	int be_id = msm_pcm_get_be_id_from_port_id(port_id);

	pr_debug("%s:port_id %d be_id %d, port_type 0x%x\n",
		  __func__, port_id, be_id, port_type);

	if (port_type == MSM_AFE_PORT_TYPE_TX) {
		session = SESSION_TYPE_TX;
	} else if (port_type == MSM_AFE_PORT_TYPE_RX) {
		session = SESSION_TYPE_RX;
	} else {
		pr_err("%s: Invalid port type %d\n", __func__, port_type);
		acdb_id = -EINVAL;
		goto exit;
	}

	if (be_id < 0) {
		pr_err("%s: Error getting backend id %d\n", __func__, be_id);
		goto exit;
	}

	mutex_lock(&routing_lock);
	i = find_first_bit(&msm_bedais[be_id].fe_sessions[0],
			   MSM_FRONTEND_DAI_MAX);
	if (i < MSM_FRONTEND_DAI_MAX)
		acdb_id = fe_dai_app_type_cfg[i][session][be_id].acdb_dev_id;

	pr_debug("%s: FE[%d] session[%d] BE[%d] acdb_id(%d)\n",
		 __func__, i, session, be_id, acdb_id);
	mutex_unlock(&routing_lock);
exit:
	return acdb_id;
}

static int msm_routing_get_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = fm_switch_enable;
	pr_debug("%s: FM Switch enable %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_put_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: FM Switch enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
						update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
						update);
	fm_switch_enable = ucontrol->value.integer.value[0];
	return 1;
}

static int msm_routing_get_hfp_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = hfp_switch_enable;
	pr_debug("%s: HFP Switch enable %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_put_hfp_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: HFP Switch enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol,
						1, update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol,
						0, update);
	hfp_switch_enable = ucontrol->value.integer.value[0];
	return 1;
}

static int msm_routing_a2dp_switch_mixer_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = a2dp_switch_enable;
	pr_debug("%s: A2DP Switch enable %ld\n", __func__,
		  ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_a2dp_switch_mixer_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: A2DP Switch enable %ld\n", __func__,
		  ucontrol->value.integer.value[0]);
	a2dp_switch_enable = ucontrol->value.integer.value[0];
	if (a2dp_switch_enable)
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol,
						1, update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol,
						0, update);
	return 1;
}

static int msm_routing_get_int0_mi2s_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = int0_mi2s_switch_enable;
	pr_debug("%s: INT0 MI2S Switch enable %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_put_int0_mi2s_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: INT0 MI2S Switch enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
						update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
						update);
	int0_mi2s_switch_enable = ucontrol->value.integer.value[0];
	return 1;
}

static int msm_routing_get_int4_mi2s_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = int4_mi2s_switch_enable;
	pr_debug("%s: INT4 MI2S Switch enable %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_put_int4_mi2s_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: INT4 MI2S Switch enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
						update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
						update);
	int4_mi2s_switch_enable = ucontrol->value.integer.value[0];
	return 1;
}

static int msm_routing_get_usb_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = usb_switch_enable;
	pr_debug("%s: HFP Switch enable %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_put_usb_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: USB Switch enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol,
						1, update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol,
						0, update);
	usb_switch_enable = ucontrol->value.integer.value[0];
	return 1;
}

static int msm_routing_get_pri_mi2s_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = pri_mi2s_switch_enable;
	pr_debug("%s: PRI MI2S Switch enable %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_put_pri_mi2s_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: PRI MI2S Switch enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
						update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
						update);
	pri_mi2s_switch_enable = ucontrol->value.integer.value[0];
	return 1;
}

static int msm_routing_get_sec_mi2s_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = sec_mi2s_switch_enable;
	pr_debug("%s: SEC MI2S Switch enable %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_put_sec_mi2s_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: SEC MI2S Switch enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
						update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
						update);
	sec_mi2s_switch_enable = ucontrol->value.integer.value[0];
	return 1;
}

static int msm_routing_get_tert_mi2s_switch_mixer(
				struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = tert_mi2s_switch_enable;
	pr_debug("%s: TERT MI2S Switch enable %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_put_tert_mi2s_switch_mixer(
				struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: TERT MI2S Switch enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
						update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
						update);
	tert_mi2s_switch_enable = ucontrol->value.integer.value[0];
	return 1;
}

static int msm_routing_get_quat_mi2s_switch_mixer(
				struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = quat_mi2s_switch_enable;
	pr_debug("%s: QUAT MI2S Switch enable %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_put_quat_mi2s_switch_mixer(
				struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: QUAT MI2S Switch enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
						update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
						update);
	quat_mi2s_switch_enable = ucontrol->value.integer.value[0];
	return 1;
}

static int msm_routing_get_quin_mi2s_switch_mixer(
				struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = quin_mi2s_switch_enable;
	pr_debug("%s: QUIN MI2S Switch enable %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_put_quin_mi2s_switch_mixer(
				struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: QUIN MI2S Switch enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
						update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
						update);
	quin_mi2s_switch_enable = ucontrol->value.integer.value[0];
	return 1;
}

static int msm_routing_get_fm_pcmrx_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = fm_pcmrx_switch_enable;
	pr_debug("%s: FM Switch enable %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_routing_put_fm_pcmrx_switch_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_dapm_update *update = NULL;

	pr_debug("%s: FM Switch enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 1,
						update);
	else
		snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, 0,
						update);
	fm_pcmrx_switch_enable = ucontrol->value.integer.value[0];
	return 1;
}

static int msm_routing_lsm_port_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = lsm_port_index;
	return 0;
}

static int msm_routing_lsm_port_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int mux = ucontrol->value.enumerated.item[0];
	int lsm_port = AFE_PORT_ID_SLIMBUS_MULTI_CHAN_5_TX;

	if (mux >= e->items) {
		pr_err("%s: Invalid mux value %d\n", __func__, mux);
		return -EINVAL;
	}

	pr_debug("%s: LSM enable %ld\n", __func__,
			ucontrol->value.integer.value[0]);
	switch (ucontrol->value.integer.value[0]) {
	case 1:
		lsm_port = AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_TX;
		break;
	case 2:
		lsm_port = AFE_PORT_ID_SLIMBUS_MULTI_CHAN_1_TX;
		break;
	case 3:
		lsm_port = AFE_PORT_ID_SLIMBUS_MULTI_CHAN_2_TX;
		break;
	case 4:
		lsm_port = AFE_PORT_ID_SLIMBUS_MULTI_CHAN_3_TX;
		break;
	case 5:
		lsm_port = AFE_PORT_ID_SLIMBUS_MULTI_CHAN_4_TX;
		break;
	case 6:
		lsm_port = AFE_PORT_ID_SLIMBUS_MULTI_CHAN_5_TX;
		break;
	case 7:
		lsm_port = AFE_PORT_ID_TERTIARY_MI2S_TX;
		break;
	case 8:
		lsm_port = AFE_PORT_ID_QUATERNARY_MI2S_TX;
		break;
	case 9:
		lsm_port = ADM_LSM_PORT_ID;
		break;
	case 10:
		lsm_port = AFE_PORT_ID_INT3_MI2S_TX;
		break;
	case 11:
		lsm_port = AFE_PORT_ID_PRIMARY_TDM_TX_2;
		break;
	case 12:
		lsm_port = AFE_PORT_ID_PRIMARY_TDM_TX_3;
		break;
	default:
		pr_err("Default lsm port");
		break;
	}
	set_lsm_port(lsm_port);
	lsm_port_index = ucontrol->value.integer.value[0];

	return 0;
}

static int msm_routing_lsm_func_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	int i;
	u16 port_id;
	enum afe_mad_type mad_type;

	pr_debug("%s: enter\n", __func__);
	for (i = 0; i < ARRAY_SIZE(lsm_port_text); i++)
		if (!strnstr(kcontrol->id.name, lsm_port_text[i],
			    strlen(lsm_port_text[i])))
			break;

	if (i-- == ARRAY_SIZE(lsm_port_text)) {
		WARN(1, "Invalid id name %s\n", kcontrol->id.name);
		return -EINVAL;
	}

	port_id = i * 2 + 1 + SLIMBUS_0_RX;

	/*Check for Tertiary/Quaternary/INT3 TX port*/
	if (strnstr(kcontrol->id.name, lsm_port_text[7],
			strlen(lsm_port_text[7])))
		port_id = AFE_PORT_ID_TERTIARY_MI2S_TX;

	if (strnstr(kcontrol->id.name, lsm_port_text[8],
			strlen(lsm_port_text[8])))
		port_id = AFE_PORT_ID_QUATERNARY_MI2S_TX;

	if (strnstr(kcontrol->id.name, lsm_port_text[10],
			strlen(lsm_port_text[10])))
		port_id = AFE_PORT_ID_INT3_MI2S_TX;

	mad_type = afe_port_get_mad_type(port_id);
	pr_debug("%s: port_id 0x%x, mad_type %d\n", __func__, port_id,
		 mad_type);
	switch (mad_type) {
	case MAD_HW_NONE:
		ucontrol->value.integer.value[0] = MADNONE;
		break;
	case MAD_HW_AUDIO:
		ucontrol->value.integer.value[0] = MADAUDIO;
		break;
	case MAD_HW_BEACON:
		ucontrol->value.integer.value[0] = MADBEACON;
		break;
	case MAD_HW_ULTRASOUND:
		ucontrol->value.integer.value[0] = MADULTRASOUND;
		break;
	case MAD_SW_AUDIO:
		ucontrol->value.integer.value[0] = MADSWAUDIO;
	break;
	default:
		WARN(1, "Unknown\n");
		return -EINVAL;
	}
	return 0;
}

static int msm_routing_lsm_func_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	int i;
	u16 port_id;
	enum afe_mad_type mad_type;

	pr_debug("%s: enter\n", __func__);
	for (i = 0; i < ARRAY_SIZE(lsm_port_text); i++)
		if (strnstr(kcontrol->id.name, lsm_port_text[i],
			    strlen(lsm_port_text[i])))
			break;

	if (i-- == ARRAY_SIZE(lsm_port_text)) {
		WARN(1, "Invalid id name %s\n", kcontrol->id.name);
		return -EINVAL;
	}

	port_id = i * 2 + 1 + SLIMBUS_0_RX;
	switch (ucontrol->value.integer.value[0]) {
	case MADNONE:
		mad_type = MAD_HW_NONE;
		break;
	case MADAUDIO:
		mad_type = MAD_HW_AUDIO;
		break;
	case MADBEACON:
		mad_type = MAD_HW_BEACON;
		break;
	case MADULTRASOUND:
		mad_type = MAD_HW_ULTRASOUND;
		break;
	case MADSWAUDIO:
		mad_type = MAD_SW_AUDIO;
		break;
	default:
		WARN(1, "Unknown\n");
		return -EINVAL;
	}

	/*Check for Tertiary/Quaternary/INT3 TX port*/
	if (strnstr(kcontrol->id.name, lsm_port_text[7],
			strlen(lsm_port_text[7])))
		port_id = AFE_PORT_ID_TERTIARY_MI2S_TX;

	if (strnstr(kcontrol->id.name, lsm_port_text[8],
			strlen(lsm_port_text[8])))
		port_id = AFE_PORT_ID_QUATERNARY_MI2S_TX;

	if (strnstr(kcontrol->id.name, lsm_port_text[10],
			strlen(lsm_port_text[10])))
		port_id = AFE_PORT_ID_INT3_MI2S_TX;

	pr_debug("%s: port_id 0x%x, mad_type %d\n", __func__, port_id,
		 mad_type);
	return afe_port_set_mad_type(port_id, mad_type);
}

static const char *const adm_override_chs_text[] = {"Zero", "One", "Two"};

static SOC_ENUM_SINGLE_EXT_DECL(slim_7_rx_adm_override_chs,
				adm_override_chs_text);

static int msm_routing_adm_get_backend_idx(struct snd_kcontrol *kcontrol)
{
	int backend_id;

	if (strnstr(kcontrol->id.name, "SLIM7_RX", sizeof("SLIM7_RX"))) {
		backend_id = MSM_BACKEND_DAI_SLIMBUS_7_RX;
	} else {
		pr_err("%s: unsupported backend id: %s",
			__func__, kcontrol->id.name);
		return -EINVAL;
	}

	return backend_id;
}
static int msm_routing_adm_channel_config_get(
					struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	int backend_id = msm_routing_adm_get_backend_idx(kcontrol);

	if (backend_id >= 0) {
		mutex_lock(&routing_lock);
		ucontrol->value.integer.value[0] =
			 msm_bedais[backend_id].adm_override_ch;
		pr_debug("%s: adm channel count %ld for BE:%d\n", __func__,
			 ucontrol->value.integer.value[0], backend_id);
		 mutex_unlock(&routing_lock);
	}

	return 0;
}

static int msm_routing_adm_channel_config_put(
					struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	int backend_id = msm_routing_adm_get_backend_idx(kcontrol);

	if (backend_id >= 0) {
		mutex_lock(&routing_lock);
		msm_bedais[backend_id].adm_override_ch =
				 ucontrol->value.integer.value[0];
		pr_debug("%s:updating BE :%d  adm channels: %d\n",
			  __func__, backend_id,
			  msm_bedais[backend_id].adm_override_ch);
		mutex_unlock(&routing_lock);
	}

	return 0;
}

static const struct snd_kcontrol_new adm_channel_config_controls[] = {
	SOC_ENUM_EXT("SLIM7_RX ADM Channels", slim_7_rx_adm_override_chs,
			msm_routing_adm_channel_config_get,
			msm_routing_adm_channel_config_put),
};

static int msm_routing_slim_0_rx_aanc_mux_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{

	mutex_lock(&routing_lock);
	ucontrol->value.integer.value[0] = slim0_rx_aanc_fb_port;
	mutex_unlock(&routing_lock);
	pr_debug("%s: AANC Mux Port %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
};

static int msm_routing_slim_0_rx_aanc_mux_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct aanc_data aanc_info;

	mutex_lock(&routing_lock);
	memset(&aanc_info, 0x00, sizeof(aanc_info));
	pr_debug("%s: AANC Mux Port %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	slim0_rx_aanc_fb_port = ucontrol->value.integer.value[0];
	if (ucontrol->value.integer.value[0] == 0) {
		aanc_info.aanc_active = false;
		aanc_info.aanc_tx_port = 0;
		aanc_info.aanc_rx_port = 0;
	} else {
		aanc_info.aanc_active = true;
		aanc_info.aanc_rx_port = SLIMBUS_0_RX;
		aanc_info.aanc_tx_port =
			(SLIMBUS_0_RX - 1 + (slim0_rx_aanc_fb_port * 2));
	}
	afe_set_aanc_info(&aanc_info);
	mutex_unlock(&routing_lock);
	return 0;
};
static int msm_routing_get_port_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int idx = 0, shift = 0;
	struct soc_mixer_control *mc =
	(struct soc_mixer_control *)kcontrol->private_value;

	idx = mc->rshift/(sizeof(msm_bedais[mc->shift].port_sessions[0]) * 8);
	shift = mc->rshift%(sizeof(msm_bedais[mc->shift].port_sessions[0]) * 8);

	if (idx >= BE_DAI_PORT_SESSIONS_IDX_MAX) {
		pr_err("%s: Invalid idx = %d\n", __func__, idx);
		return -EINVAL;
	}

	if (test_bit(shift,
		(unsigned long *)&msm_bedais[mc->shift].port_sessions[idx]))
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	pr_debug("%s: shift %x rshift %x val %ld\n", __func__,
	mc->shift, mc->rshift,
	ucontrol->value.integer.value[0]);

	return 0;
}

static int msm_routing_put_port_mixer(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int idx = 0, shift = 0;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	idx = mc->rshift/(sizeof(msm_bedais[mc->shift].port_sessions[0]) * 8);
	shift = mc->rshift%(sizeof(msm_bedais[mc->shift].port_sessions[0]) * 8);

	if (idx >= BE_DAI_PORT_SESSIONS_IDX_MAX) {
		pr_err("%s: Invalid idx = %d\n", __func__, idx);
		return -EINVAL;
	}

	pr_debug("%s: shift 0x%x rshift 0x%x val %ld idx %d reminder shift %d\n",
		 __func__, mc->shift, mc->rshift,
		 ucontrol->value.integer.value[0], idx, shift);

	if (ucontrol->value.integer.value[0]) {
		afe_loopback(1, msm_bedais[mc->shift].port_id,
			    msm_bedais[mc->rshift].port_id);
		set_bit(shift,
		(unsigned long *)&msm_bedais[mc->shift].port_sessions[idx]);
	} else {
		afe_loopback(0, msm_bedais[mc->shift].port_id,
			    msm_bedais[mc->rshift].port_id);
		clear_bit(shift,
		(unsigned long *)&msm_bedais[mc->shift].port_sessions[idx]);
	}

	return 1;
}

static int msm_pcm_get_channel_rule_index(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u16 fe_id = 0;

	fe_id = ((struct soc_mixer_control *)
			kcontrol->private_value)->shift;
	if (fe_id >= MSM_FRONTEND_DAI_MM_SIZE) {
		pr_err("%s: invalid FE %d\n", __func__, fe_id);
		return -EINVAL;
	}

	ucontrol->value.integer.value[0] = channel_mixer[fe_id].rule;

	return 0;
}

static int msm_pcm_put_channel_rule_index(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u16 fe_id = 0;

	fe_id = ((struct soc_mixer_control *)
			kcontrol->private_value)->shift;
	if (fe_id >= MSM_FRONTEND_DAI_MM_SIZE) {
		pr_err("%s: invalid FE %d\n", __func__, fe_id);
		return -EINVAL;
	}

	channel_mixer[fe_id].rule = ucontrol->value.integer.value[0];

	return 1;
}

static int msm_pcm_get_out_chs(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u16 fe_id = 0;

	fe_id = ((struct soc_multi_mixer_control *)
			kcontrol->private_value)->shift;
	if (fe_id >= MSM_FRONTEND_DAI_MM_SIZE) {
		pr_err("%s: invalid FE %d\n", __func__, fe_id);
		return -EINVAL;
	}

	ucontrol->value.integer.value[0] =
		channel_mixer[fe_id].output_channel;
	return 0;
}

static int msm_pcm_put_out_chs(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u16 fe_id = 0;

	fe_id = ((struct soc_multi_mixer_control *)
			kcontrol->private_value)->shift;
	if (fe_id >= MSM_FRONTEND_DAI_MM_SIZE) {
		pr_err("%s: invalid FE %d\n", __func__, fe_id);
		return -EINVAL;
	}

	pr_debug("%s: fe_id is %d, output channels = %d\n", __func__,
			fe_id,
			(unsigned int)(ucontrol->value.integer.value[0]));
	channel_mixer[fe_id].output_channel =
			(unsigned int)(ucontrol->value.integer.value[0]);

	return 1;
}

static const char *const ch_mixer[] = {"Disable", "Enable"};

/* If new backend is added, need update this array */
static const char *const be_name[] = {
"ZERO", "PRI_I2S_RX", "PRI_I2S_TX", "SLIM_0_RX",
"SLIM_0_TX", "HDMI_RX", "INT_BT_SCO_RX", "INT_BT_SCO_TX",
"INT_FM_RX", "INT_FM_TX", "AFE_PCM_RX", "AFE_PCM_TX",
"AUXPCM_RX", "AUXPCM_TX", "VOICE_PLAYBACK_TX", "VOICE2_PLAYBACK_TX",
"INCALL_RECORD_RX", "INCALL_RECORD_TX", "MI2S_RX", "MI2S_TX",
"SEC_I2S_RX", "SLIM_1_RX", "SLIM_1_TX", "SLIM_2_RX",
"SLIM_2_TX", "SLIM_3_RX", "SLIM_3_TX", "SLIM_4_RX",
"SLIM_4_TX", "SLIM_5_RX", "SLIM_5_TX", "SLIM_6_RX",
"SLIM_6_TX", "SLIM_7_RX", "SLIM_7_TX", "SLIM_8_RX",
"SLIM_8_TX", "EXTPROC_RX", "EXTPROC_TX", "EXPROC_EC_TX",
"QUAT_MI2S_RX", "QUAT_MI2S_TX", "SECOND_MI2S_RX", "SECOND_MI2S_TX",
"PRI_MI2S_RX", "PRI_MI2S_TX", "TERT_MI2S_RX", "TERT_MI2S_TX",
"AUDIO_I2S_RX", "SEC_AUXPCM_RX", "SEC_AUXPCM_TX", "SPDIF_RX",
"SECOND_MI2S_RX_SD1", "QUIN_MI2S_RX", "QUIN_MI2S_TX", "SENARY_MI2S_TX",
"PRI_TDM_RX_0", "PRI_TDM_TX_0", "PRI_TDM_RX_1", "PRI_TDM_TX_1",
"PRI_TDM_RX_2", "PRI_TDM_TX_2", "PRI_TDM_RX_3", "PRI_TDM_TX_3",
"PRI_TDM_RX_4", "PRI_TDM_TX_4", "PRI_TDM_RX_5", "PRI_TDM_TX_5",
"PRI_TDM_RX_6", "PRI_TDM_TX_6", "PRI_TDM_RX_7", "PRI_TDM_TX_7",
"SEC_TDM_RX_0", "SEC_TDM_TX_0", "SEC_TDM_RX_1", "SEC_TDM_TX_1",
"SEC_TDM_RX_2", "SEC_TDM_TX_2", "SEC_TDM_RX_3", "SEC_TDM_TX_3",
"SEC_TDM_RX_4", "SEC_TDM_TX_4", "SEC_TDM_RX_5", "SEC_TDM_TX_5",
"SEC_TDM_RX_6", "SEC_TDM_TX_6", "SEC_TDM_RX_7", "SEC_TDM_TX_7",
"TERT_TDM_RX_0", "TERT_TDM_TX_0", "TERT_TDM_RX_1", "TERT_TDM_TX_1",
"TERT_TDM_RX_2", "TERT_TDM_TX_2", "TERT_TDM_RX_3", "TERT_TDM_TX_3",
"TERT_TDM_RX_4", "TERT_TDM_TX_4", "TERT_TDM_RX_5", "TERT_TDM_TX_5",
"TERT_TDM_RX_6", "TERT_TDM_TX_6", "TERT_TDM_RX_7", "TERT_TDM_TX_7",
"QUAT_TDM_RX_0", "QUAT_TDM_TX_0", "QUAT_TDM_RX_1", "QUAT_TDM_TX_1",
"QUAT_TDM_RX_2", "QUAT_TDM_TX_2", "QUAT_TDM_RX_3", "QUAT_TDM_TX_3",
"QUAT_TDM_RX_4", "QUAT_TDM_TX_4", "QUAT_TDM_RX_5", "QUAT_TDM_TX_5",
"QUAT_TDM_RX_6", "QUAT_TDM_TX_6", "QUAT_TDM_RX_7", "QUAT_TDM_TX_7",
"QUIN_TDM_RX_0", "QUIN_TDM_TX_0", "QUIN_TDM_RX_1", "QUIN_TDM_TX_1",
"QUIN_TDM_RX_2", "QUIN_TDM_TX_2", "QUIN_TDM_RX_3", "QUIN_TDM_TX_3",
"QUIN_TDM_RX_4", "QUIN_TDM_TX_4", "QUIN_TDM_RX_5", "QUIN_TDM_TX_5",
"QUIN_TDM_RX_6", "QUIN_TDM_TX_6", "QUIN_TDM_RX_7", "QUIN_TDM_TX_7",
"INT_BT_A2DP_RX", "USB_RX", "USB_TX", "DISPLAY_PORT_RX",
"TERT_AUXPCM_RX", "TERT_AUXPCM_TX", "QUAT_AUXPCM_RX", "QUAT_AUXPCM_TX",
"QUIN_AUXPCM_RX", "QUIN_AUXPCM_TX",
"INT0_MI2S_RX", "INT0_MI2S_TX", "INT1_MI2S_RX", "INT1_MI2S_TX",
"INT2_MI2S_RX", "INT2_MI2S_TX", "INT3_MI2S_RX", "INT3_MI2S_TX",
"INT4_MI2S_RX", "INT4_MI2S_TX", "INT5_MI2S_RX", "INT5_MI2S_TX",
"INT6_MI2S_RX", "INT6_MI2S_TX", "PROXY_RX", "PROXY_TX"
};

static SOC_ENUM_SINGLE_DECL(mm1_channel_mux,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA1, ch_mixer);
static SOC_ENUM_SINGLE_DECL(mm2_channel_mux,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA2, ch_mixer);
static SOC_ENUM_SINGLE_DECL(mm3_channel_mux,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA3, ch_mixer);
static SOC_ENUM_SINGLE_DECL(mm4_channel_mux,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA4, ch_mixer);

static SOC_ENUM_DOUBLE_DECL(mm1_ch1_enum,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA1, 0, be_name);
static SOC_ENUM_DOUBLE_DECL(mm1_ch2_enum,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA1, 1, be_name);
static SOC_ENUM_DOUBLE_DECL(mm1_ch3_enum,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA1, 2, be_name);
static SOC_ENUM_DOUBLE_DECL(mm1_ch4_enum,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA1, 3, be_name);
static SOC_ENUM_DOUBLE_DECL(mm1_ch5_enum,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA1, 4, be_name);
static SOC_ENUM_DOUBLE_DECL(mm1_ch6_enum,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA1, 5, be_name);
static SOC_ENUM_DOUBLE_DECL(mm1_ch7_enum,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA1, 6, be_name);
static SOC_ENUM_DOUBLE_DECL(mm1_ch8_enum,
	SND_SOC_NOPM, MSM_FRONTEND_DAI_MULTIMEDIA1, 7, be_name);

static int msm_pcm_get_ctl_enum_info(struct snd_ctl_elem_info *uinfo,
		unsigned int channels,
		unsigned int items, const char *const names[])
{
	if (uinfo->value.enumerated.item >= items)
		uinfo->value.enumerated.item = items - 1;

	WARN(strlen(names[uinfo->value.enumerated.item]) >=
		sizeof(uinfo->value.enumerated.name),
		"ALSA: too long item name '%s'\n",
		names[uinfo->value.enumerated.item]);
	strlcpy(uinfo->value.enumerated.name,
		names[uinfo->value.enumerated.item],
		sizeof(uinfo->value.enumerated.name));
	return 0;
}

static int msm_pcm_channel_mixer_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;

	uinfo->value.enumerated.items = ARRAY_SIZE(ch_mixer);
	msm_pcm_get_ctl_enum_info(uinfo, 1, e->items, e->texts);

	return 0;
}
static int msm_pcm_channel_mixer_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u16 fe_id = 0;

	fe_id = ((struct soc_enum *)
			kcontrol->private_value)->shift_l;
	if (fe_id >= MSM_FRONTEND_DAI_MM_SIZE) {
		pr_err("%s: invalid FE %d\n", __func__, fe_id);
		return -EINVAL;
	}

	pr_debug("%s: FE %d %s\n", __func__,
		fe_id,
		channel_mixer[fe_id].enable ? "Enabled" : "Disabled");
	ucontrol->value.enumerated.item[0] = channel_mixer[fe_id].enable;
	return 0;
}

static int msm_pcm_channel_mixer_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u16 fe_id = 0;

	fe_id = ((struct soc_enum *)
			kcontrol->private_value)->shift_l;
	if (fe_id >= MSM_FRONTEND_DAI_MM_SIZE) {
		pr_err("%s: invalid FE %d\n", __func__, fe_id);
		return -EINVAL;
	}

	channel_mixer[fe_id].enable = ucontrol->value.enumerated.item[0];
	pr_debug("%s: %s FE %d\n", __func__,
		channel_mixer[fe_id].enable ? "Enable" : "Disable",
		fe_id);
	return 0;
}

static int msm_pcm_channel_input_be_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;

	uinfo->value.enumerated.items = ARRAY_SIZE(be_name);
	msm_pcm_get_ctl_enum_info(uinfo, 1, e->items, e->texts);

	return 0;
}

static int msm_pcm_channel_input_be_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u16 fe_id = 0, in_ch = 0;

	fe_id = e->shift_l;
	in_ch = e->shift_r;
	if (fe_id >= MSM_FRONTEND_DAI_MM_SIZE) {
		pr_err("%s: invalid FE %d\n", __func__, fe_id);
		return -EINVAL;
	}
	if (in_ch >= ADM_MAX_CHANNELS) {
		pr_err("%s: invalid input channel %d\n", __func__, in_ch);
		return -EINVAL;
	}

	channel_input[fe_id][in_ch] = ucontrol->value.enumerated.item[0];
	return 1;
}

static int msm_pcm_channel_input_be_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	u16 fe_id = 0, in_ch = 0;

	fe_id = e->shift_l;
	in_ch = e->shift_r;
	if (fe_id >= MSM_FRONTEND_DAI_MM_SIZE) {
		pr_err("%s: invalid FE %d\n", __func__, fe_id);
		return -EINVAL;
	}
	if (in_ch >= ADM_MAX_CHANNELS) {
		pr_err("%s: invalid input channel %d\n", __func__, in_ch);
		return -EINVAL;
	}

	ucontrol->value.enumerated.item[0] = channel_input[fe_id][in_ch];
	return 1;
}


static int msm_pcm_channel_weight_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = ADM_MAX_CHANNELS;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = WEIGHT_0_DB;

	return 0;
}

static int msm_pcm_channel_weight_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u16 fe_id = 0, out_ch = 0;
	int i, weight;

	fe_id = ((struct soc_multi_mixer_control *)
			kcontrol->private_value)->shift;
	out_ch = ((struct soc_multi_mixer_control *)
			kcontrol->private_value)->rshift;
	if (fe_id >= MSM_FRONTEND_DAI_MM_SIZE) {
		pr_err("%s: invalid FE %d\n", __func__, fe_id);
		return -EINVAL;
	}
	if (out_ch >= ADM_MAX_CHANNELS) {
		pr_err("%s: invalid input channel %d\n", __func__, out_ch);
		return -EINVAL;
	}

	pr_debug("%s: FE_ID: %d, channel weight %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld\n",
		__func__, fe_id,
		ucontrol->value.integer.value[0],
		ucontrol->value.integer.value[1],
		ucontrol->value.integer.value[2],
		ucontrol->value.integer.value[3],
		ucontrol->value.integer.value[4],
		ucontrol->value.integer.value[5],
		ucontrol->value.integer.value[6],
		ucontrol->value.integer.value[7]);

	for (i = 0; i < ADM_MAX_CHANNELS; ++i) {
		weight = ucontrol->value.integer.value[i];
		channel_mixer[fe_id].channel_weight[out_ch][i] = weight;
		pr_debug("%s: FE_ID %d, output %d input %d weight %d\n",
			__func__, fe_id, out_ch, i,
			channel_mixer[fe_id].channel_weight[out_ch][i]);
	}

	return 0;
}

static int msm_pcm_channel_weight_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	u16 fe_id = 0, out_ch = 0;
	int i;

	fe_id = ((struct soc_multi_mixer_control *)
			kcontrol->private_value)->shift;
	out_ch = ((struct soc_multi_mixer_control *)
			kcontrol->private_value)->rshift;
	if (fe_id >= MSM_FRONTEND_DAI_MM_SIZE) {
		pr_err("%s: invalid FE %d\n", __func__, fe_id);
		return -EINVAL;
	}
	if (out_ch >= ADM_MAX_CHANNELS) {
		pr_err("%s: invalid input channel %d\n", __func__, out_ch);
		return -EINVAL;
	}

	for (i = 0; i < ADM_MAX_CHANNELS; ++i)
		ucontrol->value.integer.value[i] =
			channel_mixer[fe_id].channel_weight[out_ch][i];

	pr_debug("%s: FE_ID: %d, weight  %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld",
		__func__, fe_id,
		ucontrol->value.integer.value[0],
		ucontrol->value.integer.value[1],
		ucontrol->value.integer.value[2],
		ucontrol->value.integer.value[3],
		ucontrol->value.integer.value[4],
		ucontrol->value.integer.value[5],
		ucontrol->value.integer.value[6],
		ucontrol->value.integer.value[7]);

	return 0;
}

static const struct snd_kcontrol_new channel_mixer_controls[] = {
	SOC_SINGLE_EXT("MultiMedia1 Channel Rule", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA1, 8, 0,
			msm_pcm_get_channel_rule_index,
			msm_pcm_put_channel_rule_index),
	SOC_SINGLE_EXT("MultiMedia2 Channel Rule", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA2, 8, 0,
			msm_pcm_get_channel_rule_index,
			msm_pcm_put_channel_rule_index),
	SOC_SINGLE_EXT("MultiMedia3 Channel Rule", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA3, 8, 0,
			msm_pcm_get_channel_rule_index,
			msm_pcm_put_channel_rule_index),
	SOC_SINGLE_EXT("MultiMedia4 Channel Rule", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA4, 8, 0,
			msm_pcm_get_channel_rule_index,
			msm_pcm_put_channel_rule_index),
	SOC_SINGLE_EXT("MultiMedia5 Channel Rule", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA5, 8, 0,
			msm_pcm_get_channel_rule_index,
			msm_pcm_put_channel_rule_index),
	SOC_SINGLE_EXT("MultiMedia6 Channel Rule", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA6, 8, 0,
			msm_pcm_get_channel_rule_index,
			msm_pcm_put_channel_rule_index),

	SOC_SINGLE_EXT("MultiMedia1 Channels", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA1, 8, 0,
			msm_pcm_get_out_chs,
			msm_pcm_put_out_chs),
	SOC_SINGLE_EXT("MultiMedia2 Channels", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA2, 8, 0,
			msm_pcm_get_out_chs,
			msm_pcm_put_out_chs),
	SOC_SINGLE_EXT("MultiMedia3 Channels", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA3, 8, 0,
			msm_pcm_get_out_chs,
			msm_pcm_put_out_chs),
	SOC_SINGLE_EXT("MultiMedia4 Channels", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA4, 8, 0,
			msm_pcm_get_out_chs,
			msm_pcm_put_out_chs),
	SOC_SINGLE_EXT("MultiMedia5 Channels", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA5, 8, 0,
			msm_pcm_get_out_chs,
			msm_pcm_put_out_chs),
	SOC_SINGLE_EXT("MultiMedia6 Channels", SND_SOC_NOPM,
			MSM_FRONTEND_DAI_MULTIMEDIA6, 8, 0,
			msm_pcm_get_out_chs,
			msm_pcm_put_out_chs),
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Channel Mixer",
	.info = msm_pcm_channel_mixer_info,
	.get = msm_pcm_channel_mixer_get,
	.put = msm_pcm_channel_mixer_put,
	.private_value = (unsigned long)&(mm1_channel_mux)
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia2 Channel Mixer",
	.info = msm_pcm_channel_mixer_info,
	.get = msm_pcm_channel_mixer_get,
	.put = msm_pcm_channel_mixer_put,
	.private_value = (unsigned long)&(mm2_channel_mux)
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia3 Channel Mixer",
	.info = msm_pcm_channel_mixer_info,
	.get = msm_pcm_channel_mixer_get,
	.put = msm_pcm_channel_mixer_put,
	.private_value = (unsigned long)&(mm3_channel_mux)
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia4 Channel Mixer",
	.info = msm_pcm_channel_mixer_info,
	.get = msm_pcm_channel_mixer_get,
	.put = msm_pcm_channel_mixer_put,
	.private_value = (unsigned long)&(mm4_channel_mux)
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Output Channel1",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{ .shift = MSM_FRONTEND_DAI_MULTIMEDIA1, .rshift = 0,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Output Channel2",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{ .shift = MSM_FRONTEND_DAI_MULTIMEDIA1, .rshift = 1, }
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Output Channel3",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{ .shift = MSM_FRONTEND_DAI_MULTIMEDIA1, .rshift = 2,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Output Channel4",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{ .shift = MSM_FRONTEND_DAI_MULTIMEDIA1, .rshift = 3,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Output Channel5",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{ .shift = MSM_FRONTEND_DAI_MULTIMEDIA1, .rshift = 4,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Output Channel6",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{ .shift = MSM_FRONTEND_DAI_MULTIMEDIA1, .rshift = 5,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Output Channel7",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{ .shift = MSM_FRONTEND_DAI_MULTIMEDIA1, .rshift = 6,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Output Channel8",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{ .shift = MSM_FRONTEND_DAI_MULTIMEDIA1, .rshift = 7,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia2 Output Channel1",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{.shift = MSM_FRONTEND_DAI_MULTIMEDIA2, .rshift = 0,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia2 Output Channel2",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{.shift = MSM_FRONTEND_DAI_MULTIMEDIA2, .rshift = 1,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia2 Output Channel3",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{.shift = MSM_FRONTEND_DAI_MULTIMEDIA2, .rshift = 2,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia3 Output Channel1",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{.shift = MSM_FRONTEND_DAI_MULTIMEDIA3, .rshift = 0,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia3 Output Channel2",
	.info = msm_pcm_channel_weight_info,
	.get = msm_pcm_channel_weight_get,
	.put = msm_pcm_channel_weight_put,
	.private_value = (unsigned long)&(struct soc_multi_mixer_control)
		{.shift = MSM_FRONTEND_DAI_MULTIMEDIA3, .rshift = 1,}
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Channel1",
	.info = msm_pcm_channel_input_be_info,
	.get = msm_pcm_channel_input_be_get,
	.put = msm_pcm_channel_input_be_put,
	.private_value = (unsigned long)&(mm1_ch1_enum)
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Channel2",
	.info = msm_pcm_channel_input_be_info,
	.get = msm_pcm_channel_input_be_get,
	.put = msm_pcm_channel_input_be_put,
	.private_value = (unsigned long)&(mm1_ch2_enum)
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Channel3",
	.info = msm_pcm_channel_input_be_info,
	.get = msm_pcm_channel_input_be_get,
	.put = msm_pcm_channel_input_be_put,
	.private_value = (unsigned long)&(mm1_ch3_enum)
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Channel4",
	.info = msm_pcm_channel_input_be_info,
	.get = msm_pcm_channel_input_be_get,
	.put = msm_pcm_channel_input_be_put,
	.private_value = (unsigned long)&(mm1_ch4_enum)
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Channel5",
	.info = msm_pcm_channel_input_be_info,
	.get = msm_pcm_channel_input_be_get,
	.put = msm_pcm_channel_input_be_put,
	.private_value = (unsigned long)&(mm1_ch5_enum)
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Channel6",
	.info = msm_pcm_channel_input_be_info,
	.get = msm_pcm_channel_input_be_get,
	.put = msm_pcm_channel_input_be_put,
	.private_value = (unsigned long)&(mm1_ch6_enum)
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Channel7",
	.info = msm_pcm_channel_input_be_info,
	.get = msm_pcm_channel_input_be_get,
	.put = msm_pcm_channel_input_be_put,
	.private_value = (unsigned long)&(mm1_ch7_enum)
	},
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.name = "MultiMedia1 Channel8",
	.info = msm_pcm_channel_input_be_info,
	.get = msm_pcm_channel_input_be_get,
	.put = msm_pcm_channel_input_be_put,
	.private_value = (unsigned long)&(mm1_ch8_enum)
	},
};
static int msm_ec_ref_ch_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = msm_ec_ref_ch;
	pr_debug("%s: msm_ec_ref_ch = %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_ec_ref_ch_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	msm_ec_ref_ch = ucontrol->value.integer.value[0];
	pr_debug("%s: msm_ec_ref_ch = %d\n", __func__, msm_ec_ref_ch);
	adm_num_ec_ref_rx_chans(msm_ec_ref_ch);
	return 0;
}

static const char *const ec_ref_ch_text[] = {"Zero", "One", "Two", "Three",
	"Four", "Five", "Six", "Seven", "Eight"};

static int msm_ec_ref_bit_format_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	switch (msm_ec_ref_bit_format) {
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 2;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		ucontrol->value.integer.value[0] = 1;
		break;
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}
	pr_debug("%s: msm_ec_ref_bit_format = %ld\n",
		 __func__, ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_ec_ref_bit_format_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	u16 bit_width = 0;

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		msm_ec_ref_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 1:
		msm_ec_ref_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	default:
		msm_ec_ref_bit_format = 0;
		break;
	}

	if (msm_ec_ref_bit_format == SNDRV_PCM_FORMAT_S16_LE)
		bit_width = 16;
	else if (msm_ec_ref_bit_format == SNDRV_PCM_FORMAT_S24_LE)
		bit_width = 24;

	pr_debug("%s: msm_ec_ref_bit_format = %d\n",
		 __func__, msm_ec_ref_bit_format);
	adm_ec_ref_rx_bit_width(bit_width);
	return 0;
}

static char const *ec_ref_bit_format_text[] = {"0", "S16_LE", "S24_LE"};

static int msm_ec_ref_rate_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = msm_ec_ref_sampling_rate;
	pr_debug("%s: msm_ec_ref_sampling_rate = %ld\n",
		 __func__, ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_ec_ref_rate_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 0:
		msm_ec_ref_sampling_rate = 0;
		break;
	case 1:
		msm_ec_ref_sampling_rate = 8000;
		break;
	case 2:
		msm_ec_ref_sampling_rate = 16000;
		break;
	case 3:
		msm_ec_ref_sampling_rate = 32000;
		break;
	case 4:
		msm_ec_ref_sampling_rate = 44100;
		break;
	case 5:
		msm_ec_ref_sampling_rate = 48000;
		break;
	case 6:
		msm_ec_ref_sampling_rate = 96000;
		break;
	case 7:
		msm_ec_ref_sampling_rate = 192000;
		break;
	case 8:
		msm_ec_ref_sampling_rate = 384000;
		break;
	default:
		msm_ec_ref_sampling_rate = 48000;
		break;
	}
	pr_debug("%s: msm_ec_ref_sampling_rate = %d\n",
		 __func__, msm_ec_ref_sampling_rate);
	adm_ec_ref_rx_sampling_rate(msm_ec_ref_sampling_rate);
	return 0;
}

static const char *const ec_ref_rate_text[] = {"0", "8000", "16000",
	"32000", "44100", "48000", "96000", "192000", "384000"};

static const struct soc_enum msm_route_ec_ref_params_enum[] = {
	SOC_ENUM_SINGLE_EXT(9, ec_ref_ch_text),
	SOC_ENUM_SINGLE_EXT(3, ec_ref_bit_format_text),
	SOC_ENUM_SINGLE_EXT(9, ec_ref_rate_text),
};

static const struct snd_kcontrol_new ec_ref_param_controls[] = {
	SOC_ENUM_EXT("EC Reference Channels", msm_route_ec_ref_params_enum[0],
		msm_ec_ref_ch_get, msm_ec_ref_ch_put),
	SOC_ENUM_EXT("EC Reference Bit Format", msm_route_ec_ref_params_enum[1],
		msm_ec_ref_bit_format_get, msm_ec_ref_bit_format_put),
	SOC_ENUM_EXT("EC Reference SampleRate", msm_route_ec_ref_params_enum[2],
		msm_ec_ref_rate_get, msm_ec_ref_rate_put),
};

static int msm_routing_ec_ref_rx_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: ec_ref_rx  = %d", __func__, msm_route_ec_ref_rx);
	mutex_lock(&routing_lock);
	ucontrol->value.integer.value[0] = msm_route_ec_ref_rx;
	mutex_unlock(&routing_lock);
	return 0;
}

static int msm_routing_ec_ref_rx_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int ec_ref_port_id;
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_dapm_update *update = NULL;


	mutex_lock(&routing_lock);
	switch (ucontrol->value.integer.value[0]) {
	case 0:
		msm_route_ec_ref_rx = 0;
		ec_ref_port_id = AFE_PORT_INVALID;
		break;
	case 1:
		msm_route_ec_ref_rx = 1;
		ec_ref_port_id = SLIMBUS_0_RX;
		break;
	case 2:
		msm_route_ec_ref_rx = 2;
		ec_ref_port_id = AFE_PORT_ID_PRIMARY_MI2S_RX;
		break;
	case 3:
		msm_route_ec_ref_rx = 3;
		ec_ref_port_id = AFE_PORT_ID_PRIMARY_MI2S_TX;
		break;
	case 4:
		msm_route_ec_ref_rx = 4;
		ec_ref_port_id = AFE_PORT_ID_SECONDARY_MI2S_TX;
		break;
	case 5:
		msm_route_ec_ref_rx = 5;
		ec_ref_port_id = AFE_PORT_ID_TERTIARY_MI2S_TX;
		break;
	case 6:
		msm_route_ec_ref_rx = 6;
		ec_ref_port_id = AFE_PORT_ID_QUATERNARY_MI2S_TX;
		break;
	case 7:
		msm_route_ec_ref_rx = 7;
		ec_ref_port_id = AFE_PORT_ID_SECONDARY_MI2S_RX;
		break;
	case 9:
		msm_route_ec_ref_rx = 9;
		ec_ref_port_id = SLIMBUS_5_RX;
		break;
	case 10:
		msm_route_ec_ref_rx = 10;
		ec_ref_port_id = SLIMBUS_1_TX;
		break;
	case 11:
		msm_route_ec_ref_rx = 11;
		ec_ref_port_id = AFE_PORT_ID_QUATERNARY_TDM_TX_1;
		break;
	case 12:
		msm_route_ec_ref_rx = 12;
		ec_ref_port_id = AFE_PORT_ID_QUATERNARY_TDM_RX;
		break;
	case 13:
		msm_route_ec_ref_rx = 13;
		ec_ref_port_id = AFE_PORT_ID_QUATERNARY_TDM_RX_1;
		break;
	case 14:
		msm_route_ec_ref_rx = 14;
		ec_ref_port_id = AFE_PORT_ID_QUATERNARY_TDM_RX_2;
		break;
	case 15:
		msm_route_ec_ref_rx = 15;
		ec_ref_port_id = SLIMBUS_6_RX;
		break;
	case 16:
		msm_route_ec_ref_rx = 16;
		ec_ref_port_id = AFE_PORT_ID_TERTIARY_MI2S_RX;
		break;
	case 17:
		msm_route_ec_ref_rx = 17;
		ec_ref_port_id = AFE_PORT_ID_QUATERNARY_MI2S_RX;
		break;
	case 18:
		msm_route_ec_ref_rx = 18;
		ec_ref_port_id = AFE_PORT_ID_TERTIARY_TDM_TX;
		break;
	case 19:
		msm_route_ec_ref_rx = 19;
		ec_ref_port_id = AFE_PORT_ID_USB_RX;
		break;
	case 20:
		msm_route_ec_ref_rx = 20;
		ec_ref_port_id = AFE_PORT_ID_INT0_MI2S_RX;
		break;
	case 21:
		msm_route_ec_ref_rx = 21;
		ec_ref_port_id = AFE_PORT_ID_INT4_MI2S_RX;
		break;
	case 22:
		msm_route_ec_ref_rx = 22;
		ec_ref_port_id = AFE_PORT_ID_INT3_MI2S_TX;
		break;
	case 23:
		msm_route_ec_ref_rx = 23;
		ec_ref_port_id = AFE_PORT_ID_HDMI_OVER_DP_RX;
		break;
	case 24:
		msm_route_ec_ref_rx = 24;
		ec_ref_port_id = AFE_PORT_ID_PRIMARY_TDM_RX_1;
		break;
	default:
		msm_route_ec_ref_rx = 0; /* NONE */
		pr_err("%s EC ref rx %ld not valid\n",
			__func__, ucontrol->value.integer.value[0]);
		ec_ref_port_id = AFE_PORT_INVALID;
		break;
	}
	msm_ec_ref_port_id = ec_ref_port_id;
	adm_ec_ref_rx_id(ec_ref_port_id);
	pr_debug("%s: msm_route_ec_ref_rx = %d\n",
	    __func__, msm_route_ec_ref_rx);
	mutex_unlock(&routing_lock);
	snd_soc_dapm_mux_update_power(widget->dapm, kcontrol,
					msm_route_ec_ref_rx, e, update);
	return 0;
}

static const char *const ec_ref_rx[] = { "None", "SLIM_RX", "I2S_RX",
	"PRI_MI2S_TX", "SEC_MI2S_TX",
	"TERT_MI2S_TX", "QUAT_MI2S_TX", "SEC_I2S_RX", "PROXY_RX",
	"SLIM_5_RX", "SLIM_1_TX", "QUAT_TDM_TX_1",
	"QUAT_TDM_RX_0", "QUAT_TDM_RX_1", "QUAT_TDM_RX_2", "SLIM_6_RX",
	"TERT_MI2S_RX", "QUAT_MI2S_RX", "TERT_TDM_TX_0", "USB_AUDIO_RX",
	"INT0_MI2S_RX", "INT4_MI2S_RX", "INT3_MI2S_TX", "DISPLAY_PORT",
	"PRI_TDM_RX_1"};

static const struct soc_enum msm_route_ec_ref_rx_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(ec_ref_rx), ec_ref_rx),
};

static const struct snd_kcontrol_new ext_ec_ref_mux_ul1 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL1 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul2 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL2 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul3 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL3 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul4 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL4 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul5 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL5 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul6 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL6 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul8 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL8 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul9 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL9 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul16 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL16 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul10 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL10 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul17 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL17 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul18 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL18 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul19 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL19 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul28 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL28 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static const struct snd_kcontrol_new ext_ec_ref_mux_ul29 =
	SOC_DAPM_ENUM_EXT("AUDIO_REF_EC_UL29 MUX Mux",
		msm_route_ec_ref_rx_enum[0],
		msm_routing_ec_ref_rx_get, msm_routing_ec_ref_rx_put);

static int msm_routing_ext_ec_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: ext_ec_ref_rx  = %x\n", __func__, msm_route_ext_ec_ref);

	mutex_lock(&routing_lock);
	ucontrol->value.integer.value[0] = msm_route_ext_ec_ref;
	mutex_unlock(&routing_lock);
	return 0;
}

static int msm_routing_ext_ec_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	int mux = ucontrol->value.enumerated.item[0];
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int ret = 1;
	bool state = true;
	uint16_t ext_ec_ref_port_id;
	struct snd_soc_dapm_update *update = NULL;

	if (mux >= e->items) {
		pr_err("%s: Invalid mux value %d\n", __func__, mux);
		return -EINVAL;
	}

	mutex_lock(&routing_lock);
	msm_route_ext_ec_ref = ucontrol->value.integer.value[0];

	switch (msm_route_ext_ec_ref) {
	case EXT_EC_REF_PRI_MI2S_TX:
		ext_ec_ref_port_id = AFE_PORT_ID_PRIMARY_MI2S_TX;
		break;
	case EXT_EC_REF_SEC_MI2S_TX:
		ext_ec_ref_port_id = AFE_PORT_ID_SECONDARY_MI2S_TX;
		break;
	case EXT_EC_REF_TERT_MI2S_TX:
		ext_ec_ref_port_id = AFE_PORT_ID_TERTIARY_MI2S_TX;
		break;
	case EXT_EC_REF_QUAT_MI2S_TX:
		ext_ec_ref_port_id = AFE_PORT_ID_QUATERNARY_MI2S_TX;
		break;
	case EXT_EC_REF_QUIN_MI2S_TX:
		ext_ec_ref_port_id = AFE_PORT_ID_QUINARY_MI2S_TX;
		break;
	case EXT_EC_REF_SLIM_1_TX:
		ext_ec_ref_port_id = SLIMBUS_1_TX;
		break;
	case EXT_EC_REF_NONE:
	default:
		ext_ec_ref_port_id = AFE_PORT_INVALID;
		state = false;
		break;
	}

	pr_debug("%s: val = %d ext_ec_ref_port_id = 0x%0x state = %d\n",
		 __func__, msm_route_ext_ec_ref, ext_ec_ref_port_id, state);

	if (!voc_set_ext_ec_ref_port_id(ext_ec_ref_port_id, state)) {
		mutex_unlock(&routing_lock);
		snd_soc_dapm_mux_update_power(widget->dapm, kcontrol, mux, e,
						update);
	} else {
		ret = -EINVAL;
		mutex_unlock(&routing_lock);
	}
	return ret;
}

static const char * const ext_ec_ref_rx[] = {"NONE", "PRI_MI2S_TX",
					"SEC_MI2S_TX", "TERT_MI2S_TX",
					"QUAT_MI2S_TX", "QUIN_MI2S_TX",
					"SLIM_1_TX"};

static const struct soc_enum msm_route_ext_ec_ref_rx_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(ext_ec_ref_rx), ext_ec_ref_rx),
};

static const struct snd_kcontrol_new voc_ext_ec_mux =
	SOC_DAPM_ENUM_EXT("VOC_EXT_EC MUX Mux", msm_route_ext_ec_ref_rx_enum[0],
			  msm_routing_ext_ec_get, msm_routing_ext_ec_put);


static const struct snd_kcontrol_new pri_i2s_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new sec_i2s_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new spdif_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SPDIF_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_2_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_2_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_5_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_5_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mi2s_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quaternary_mi2s_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quinary_mi2s_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new tertiary_mi2s_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new secondary_mi2s_rx2_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX_SD1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new secondary_mi2s_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new primary_mi2s_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new int0_mi2s_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new int4_mi2s_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new hdmi_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new display_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

	/* incall music delivery mixer */
static const struct snd_kcontrol_new incall_music_delivery_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_VOICE_PLAYBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_VOICE_PLAYBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_VOICE_PLAYBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_VOICE_PLAYBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new incall_music2_delivery_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_VOICE2_PLAYBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_VOICE2_PLAYBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_VOICE2_PLAYBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_VOICE2_PLAYBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_4_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_4_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_4_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_4_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_4_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_6_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new slimbus_7_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new usb_audio_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new int_bt_sco_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new int_bt_a2dp_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_A2DP_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new int_fm_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new afe_pcm_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new auxpcm_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new sec_auxpcm_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia17", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia18", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia19", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia28", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia29", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new tert_auxpcm_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quat_auxpcm_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quin_auxpcm_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};
static const struct snd_kcontrol_new pri_tdm_rx_0_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new pri_tdm_rx_1_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new pri_tdm_rx_2_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new pri_tdm_rx_3_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new pri_tdm_tx_0_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new pri_tdm_tx_1_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new pri_tdm_tx_2_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new pri_tdm_tx_3_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};



static const struct snd_kcontrol_new sec_tdm_rx_0_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new sec_tdm_rx_1_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new sec_tdm_rx_2_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new sec_tdm_rx_3_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new sec_tdm_tx_0_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),

};

static const struct snd_kcontrol_new tert_tdm_rx_0_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new tert_tdm_tx_0_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new tert_tdm_tx_1_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new tert_tdm_tx_2_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new tert_tdm_tx_3_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia30", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia31", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia32", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia33", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new tert_tdm_rx_1_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new tert_tdm_rx_2_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new tert_tdm_rx_3_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new tert_tdm_rx_4_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_RX_4,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quat_tdm_rx_0_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia20", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quat_tdm_tx_0_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quat_tdm_rx_1_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia20", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quat_tdm_rx_2_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia20", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quat_tdm_rx_3_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia20", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quin_tdm_rx_0_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia20", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quin_tdm_tx_0_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quin_tdm_rx_1_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia20", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quin_tdm_rx_2_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia20", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new quin_tdm_rx_3_mixer_controls[] = {
	SOC_DOUBLE_EXT("MultiMedia1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia4", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia5", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia6", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia7", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA7, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia8", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia9", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia10", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia11", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA11, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia12", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA12, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia13", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA13, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia14", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA14, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia15", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA15, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia16", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MultiMedia20", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_RX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul1_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_I2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT2_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_INT2_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_INT3_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SECONDARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SLIMBUS_0_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_AUXPCM_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_AUXPCM_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_AUXPCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_AUXPCM_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_AUXPCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUAT_AUXPCM_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_AUXPCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUIN_AUXPCM_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_INT_BT_SCO_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_INT_FM_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_AFE_PCM_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_DL", SND_SOC_NOPM,
		MSM_BACKEND_DAI_INCALL_RECORD_RX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_UL", SND_SOC_NOPM,
		MSM_BACKEND_DAI_INCALL_RECORD_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_4_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SLIMBUS_4_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_6_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SLIMBUS_6_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUINARY_MI2S_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_TX_0,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_TX_1,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_TX_2,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_TX_3,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_TX_0,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_TX_1,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_TX_2,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_TX_3,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_TX_0,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_TX_1,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_TX_2,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_TX_3,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUAT_TDM_TX_0,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUAT_TDM_TX_1,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUAT_TDM_TX_2,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUAT_TDM_TX_3,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUIN_TDM_TX_0,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUIN_TDM_TX_1,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUIN_TDM_TX_2,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_QUIN_TDM_TX_3,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_LOOPBACK_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_AFE_LOOPBACK_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SLIMBUS_7_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SLIMBUS_8_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_USB_TX,
		MSM_FRONTEND_DAI_MULTIMEDIA1, 1, 0, msm_routing_get_audio_mixer,
		msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul2_mixer_controls[] = {
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT2_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT2_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_6_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_1_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_LOOPBACK_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_LOOPBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA2, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul3_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT2_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT2_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_LOOPBACK_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_LOOPBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA3, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul4_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_DL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_UL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT2_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT2_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_LOOPBACK_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_LOOPBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA4, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul5_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT2_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT2_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_LOOPBACK_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_LOOPBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA5, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul6_mixer_controls[] = {
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT2_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT2_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_LOOPBACK_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_LOOPBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA6, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul8_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT2_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT2_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_DL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_UL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_6_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul16_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT2_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT2_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_DL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_UL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_6_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_LOOPBACK_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_LOOPBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA8, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA16, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul9_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_DL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_UL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_6_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_LOOPBACK_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_LOOPBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA9, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul10_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_DL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_UL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_6_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_LOOPBACK_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_LOOPBACK_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_AUX_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT2_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT2_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA10, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};
static const struct snd_kcontrol_new mmul17_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_DL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_UL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA17, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul18_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_DL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_UL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA18, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul19_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_DL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_UL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA19, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul20_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA20, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul28_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_DL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_UL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("TERT_MI2S_TX", MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_SINGLE_EXT("QUAT_MI2S_TX", MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA28, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul29_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_FM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_DL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_RX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("VOC_REC_UL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INCALL_RECORD_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_MULTIMEDIA29, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul30_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA30, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul31_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA31, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul32_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA32, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new mmul33_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_0,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_1,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_2,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_TDM_TX_3,
	MSM_FRONTEND_DAI_MULTIMEDIA33, 1, 0, msm_routing_get_audio_mixer,
	msm_routing_put_audio_mixer),
};

static const struct snd_kcontrol_new pri_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new sec_i2s_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new sec_mi2s_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new slimbus_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new slimbus_6_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new usb_audio_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new display_port_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new bt_sco_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new mi2s_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new pri_mi2s_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new int0_mi2s_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new int4_mi2s_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new tert_mi2s_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new quat_mi2s_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new quin_mi2s_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new afe_pcm_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new aux_pcm_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new sec_aux_pcm_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new tert_aux_pcm_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new quat_aux_pcm_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new quin_aux_pcm_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new hdmi_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new pri_tdm_rx_0_voice_mixer_controls[] = {
	SOC_SINGLE_EXT("Voip", MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("Voice Stub", MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_SINGLE_EXT("Voice2 Stub", MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_SINGLE_EXT("VoLTE Stub", MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("DTMF", MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("QCHAT", MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("VoiceMMode1", MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("VoiceMMode2", MSM_BACKEND_DAI_PRI_TDM_RX_0,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new pri_tdm_rx_1_voice_mixer_controls[] = {
	SOC_SINGLE_EXT("Voip", MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("Voice Stub", MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_SINGLE_EXT("Voice2 Stub", MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_SINGLE_EXT("VoLTE Stub", MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("DTMF", MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("QCHAT", MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("VoiceMMode1", MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("VoiceMMode2", MSM_BACKEND_DAI_PRI_TDM_RX_1,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new pri_tdm_rx_2_voice_mixer_controls[] = {
	SOC_SINGLE_EXT("Voip", MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("Voice Stub", MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_SINGLE_EXT("Voice2 Stub", MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_SINGLE_EXT("VoLTE Stub", MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("DTMF", MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("QCHAT", MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("VoiceMMode1", MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("VoiceMMode2", MSM_BACKEND_DAI_PRI_TDM_RX_2,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new pri_tdm_rx_3_voice_mixer_controls[] = {
	SOC_SINGLE_EXT("Voip", MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("Voice Stub", MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_SINGLE_EXT("Voice2 Stub", MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_SINGLE_EXT("VoLTE Stub", MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("DTMF", MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("QCHAT", MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("VoiceMMode1", MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_SINGLE_EXT("VoiceMMode2", MSM_BACKEND_DAI_PRI_TDM_RX_3,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new slimbus_7_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
MSM_BACKEND_DAI_SLIMBUS_7_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new slimbus_8_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_RX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("DTMF", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_RX,
	MSM_FRONTEND_DAI_DTMF_RX, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_RX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new quat_tdm_rx_2_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_RX_2,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new stub_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_EXTPROC_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_EXTPROC_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new slimbus_1_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new slimbus_3_rx_mixer_controls[] = {
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_3_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_3_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new proxy_rx_voice_mixer_controls[] = {
	SOC_DOUBLE_EXT("VoiceMMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PROXY_RX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("VoiceMMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PROXY_RX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new tx_voicemmode1_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_TX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("MI2S_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_TX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("INT_BT_SCO_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0,
	msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0,
	msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("TERT_AUX_PCM_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUAT_AUX_PCM_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUIN_AUX_PCM_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0,
	msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX, MSM_FRONTEND_DAI_VOICEMMODE1,
	1, 0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX, MSM_FRONTEND_DAI_VOICEMMODE1,
	1, 0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_TX,
	MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0, MSM_FRONTEND_DAI_VOICEMMODE1,
	1, 0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3, MSM_FRONTEND_DAI_VOICEMMODE1,
	1, 0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, MSM_FRONTEND_DAI_VOICEMMODE1, 1, 0,
	msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_TX, MSM_FRONTEND_DAI_VOICEMMODE1,
	1, 0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("PROXY_TX_MMode1", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PROXY_TX, MSM_FRONTEND_DAI_VOICEMMODE1,
	1, 0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new tx_voicemmode2_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_TX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("MI2S_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_TX,
	MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("INT_BT_SCO_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0,
	msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0,
	msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("TERT_AUX_PCM_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUAT_AUX_PCM_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUIN_AUX_PCM_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0,
	msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX, MSM_FRONTEND_DAI_VOICEMMODE2,
	1, 0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3, MSM_FRONTEND_DAI_VOICEMMODE2,
	1, 0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX, MSM_FRONTEND_DAI_VOICEMMODE2,
	1, 0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1,
	0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, MSM_FRONTEND_DAI_VOICEMMODE2, 1, 0,
	msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_TX, MSM_FRONTEND_DAI_VOICEMMODE2,
	1, 0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("PROXY_TX_MMode2", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PROXY_TX, MSM_FRONTEND_DAI_VOICEMMODE2,
	1, 0, msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new tx_voip_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("MI2S_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("TERT_AUX_PCM_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUAT_AUX_PCM_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUIN_AUX_PCM_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_TX,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3_Voip", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_TDM_TX_3,
	MSM_FRONTEND_DAI_VOIP, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new tx_voice_stub_mixer_controls[] = {
	SOC_DOUBLE_EXT("STUB_TX_HL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_EXTPROC_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_1_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("STUB_1_TX_HL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_EXTPROC_EC_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("TERT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("QUAT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("QUIN_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_3_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_3_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_TX,
	MSM_FRONTEND_DAI_VOICE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
};

static const struct snd_kcontrol_new tx_voice2_stub_mixer_controls[] = {
	SOC_DOUBLE_EXT("STUB_TX_HL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_EXTPROC_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_1_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("STUB_1_TX_HL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_EXTPROC_EC_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("TERT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("QUAT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("QUIN_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_3_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_3_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_TX,
	MSM_FRONTEND_DAI_VOICE2_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
};

static const struct snd_kcontrol_new tx_volte_stub_mixer_controls[] = {
	SOC_DOUBLE_EXT("STUB_TX_HL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_EXTPROC_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_1_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("STUB_1_TX_HL", SND_SOC_NOPM,
	MSM_BACKEND_DAI_EXTPROC_EC_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("TERT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("QUAT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("QUIN_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_3_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_3_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_TX,
	MSM_FRONTEND_DAI_VOLTE_STUB, 1, 0, msm_routing_get_voice_stub_mixer,
	msm_routing_put_voice_stub_mixer),
};

static const struct snd_kcontrol_new tx_qchat_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_I2S_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0,
	msm_routing_get_voice_mixer, msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("TERT_AUX_PCM_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUAT_AUX_PCM_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("QUIN_AUX_PCM_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("MI2S_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT3_MI2S_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_7_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_8_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
	SOC_DOUBLE_EXT("USB_AUDIO_TX_QCHAT", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_TX,
	MSM_FRONTEND_DAI_QCHAT, 1, 0, msm_routing_get_voice_mixer,
	msm_routing_put_voice_mixer),
};

static const struct snd_kcontrol_new int0_mi2s_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_BACKEND_DAI_QUINARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_BACKEND_DAI_INT3_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_BACKEND_DAI_INT_FM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_BACKEND_DAI_SLIMBUS_7_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT0_MI2S_RX,
	MSM_BACKEND_DAI_SLIMBUS_8_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new int4_mi2s_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_BACKEND_DAI_QUINARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INT3_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_BACKEND_DAI_INT3_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_BACKEND_DAI_INT_FM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_BACKEND_DAI_SLIMBUS_7_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT4_MI2S_RX,
	MSM_BACKEND_DAI_SLIMBUS_8_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new sbus_0_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_INT_FM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_1_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_SLIMBUS_1_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_SLIMBUS_7_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_SLIMBUS_8_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_QUIN_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_QUINARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_RX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_PRI_MI2S_RX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_RX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_SECONDARY_MI2S_RX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_RX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_TERTIARY_MI2S_RX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_RX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_RX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_0_RX,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new aux_pcm_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_BACKEND_DAI_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_1_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_BACKEND_DAI_SLIMBUS_1_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_BACKEND_DAI_QUAT_TDM_TX_0, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AUXPCM_RX,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new sec_auxpcm_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_BACKEND_DAI_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_AUXPCM_RX,
	MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new tert_auxpcm_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("TERT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_TERT_AUXPCM_RX,
	MSM_BACKEND_DAI_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new quat_auxpcm_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("QUAT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUAT_AUXPCM_RX,
	MSM_BACKEND_DAI_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new quin_auxpcm_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("QUIN_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_BACKEND_DAI_QUIN_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUIN_AUXPCM_RX,
	MSM_BACKEND_DAI_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new sbus_1_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_RX,
	MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_RX,
	MSM_BACKEND_DAI_AFE_PCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_RX,
	MSM_BACKEND_DAI_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_RX,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_RX,
	MSM_BACKEND_DAI_TERT_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_AUXPCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_1_RX,
	MSM_BACKEND_DAI_QUAT_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new sbus_3_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_RX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_3_RX,
	MSM_BACKEND_DAI_INT_BT_SCO_RX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_3_RX,
	MSM_BACKEND_DAI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_RX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_3_RX,
	MSM_BACKEND_DAI_AFE_PCM_RX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_RX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_3_RX,
	MSM_BACKEND_DAI_AUXPCM_RX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_RX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_3_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_RX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new sbus_6_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_INT_FM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_1_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_SLIMBUS_1_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_7_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_SLIMBUS_7_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_SLIMBUS_8_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SLIMBUS_6_RX,
	MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new bt_sco_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_1_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_BACKEND_DAI_SLIMBUS_1_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_INT_BT_SCO_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new afe_pcm_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_BACKEND_DAI_INT_FM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_1_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_AFE_PCM_RX,
	MSM_BACKEND_DAI_SLIMBUS_1_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};


static const struct snd_kcontrol_new hdmi_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_HDMI_RX,
	MSM_BACKEND_DAI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new display_port_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_DISPLAY_PORT_RX,
	MSM_BACKEND_DAI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new sec_i2s_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_SEC_I2S_RX,
	MSM_BACKEND_DAI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new mi2s_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("SLIM_1_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_BACKEND_DAI_SLIMBUS_1_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_MI2S_RX,
	MSM_BACKEND_DAI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new primary_mi2s_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_BACKEND_DAI_INT_FM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_BACKEND_DAI_QUINARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_BACKEND_DAI_SLIMBUS_8_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_PRI_MI2S_RX,
	MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new usb_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("USB_AUDIO_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_USB_RX,
	MSM_BACKEND_DAI_USB_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new quat_mi2s_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_BACKEND_DAI_INT_FM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_BACKEND_DAI_AUXPCM_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_BACKEND_DAI_QUINARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
	MSM_BACKEND_DAI_SLIMBUS_8_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new quin_mi2s_rx_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_BACKEND_DAI_TERTIARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_0_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_BACKEND_DAI_SLIMBUS_0_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_BACKEND_DAI_QUINARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SLIM_8_TX", SND_SOC_NOPM,
	MSM_BACKEND_DAI_QUINARY_MI2S_RX,
	MSM_BACKEND_DAI_SLIMBUS_8_TX, 1, 0, msm_routing_get_port_mixer,
	msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new pri_tdm_rx_0_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_INT_FM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_AFE_PCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_PRI_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_PRI_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_PRI_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_PRI_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_TERT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_TERT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_TERT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_TERT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new pri_tdm_rx_1_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_INT_FM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_AFE_PCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_PRI_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_PRI_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_PRI_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_PRI_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_QUIN_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_QUIN_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_QUIN_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_1,
		MSM_BACKEND_DAI_QUIN_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new pri_tdm_rx_2_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_INT_FM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_AFE_PCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_PRI_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_PRI_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_PRI_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_PRI_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_QUAT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_QUAT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_QUAT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_QUAT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_QUIN_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_QUIN_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_QUIN_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_2,
		MSM_BACKEND_DAI_QUIN_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new pri_tdm_rx_3_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_INT_FM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_AFE_PCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_PRI_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_PRI_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_PRI_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("PRI_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_PRI_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_QUAT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_QUAT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_QUAT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_QUAT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_QUIN_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_QUIN_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_QUIN_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_PRI_TDM_RX_3,
		MSM_BACKEND_DAI_QUIN_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new sec_tdm_rx_0_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_INT_FM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_AFE_PCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_SEC_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_SEC_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_SEC_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_SEC_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new sec_tdm_rx_1_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_INT_FM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_AFE_PCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_SEC_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_SEC_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_SEC_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_SEC_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_QUIN_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_QUIN_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_QUIN_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_1,
		MSM_BACKEND_DAI_QUIN_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new sec_tdm_rx_2_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_INT_FM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_AFE_PCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_SEC_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_SEC_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_SEC_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_SEC_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_QUAT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_QUAT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_QUAT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_QUAT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_QUIN_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_QUIN_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_QUIN_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_2,
		MSM_BACKEND_DAI_QUIN_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new sec_tdm_rx_3_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_INT_FM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_AFE_PCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_SEC_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_SEC_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_SEC_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_SEC_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_QUAT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_QUAT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_QUAT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_QUAT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_QUIN_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_QUIN_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_QUIN_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_SEC_TDM_RX_3,
		MSM_BACKEND_DAI_QUIN_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new tert_tdm_rx_0_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_INT_FM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_AFE_PCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_TERT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_TERT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_TERT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_TERT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_QUAT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_0,
		MSM_BACKEND_DAI_QUIN_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
};

static const struct snd_kcontrol_new tert_tdm_rx_1_port_mixer_controls[] = {
	SOC_DOUBLE_EXT("PRI_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_PRI_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_SECONDARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_MI2S_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_QUATERNARY_MI2S_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_FM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_INT_FM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("INTERNAL_BT_SCO_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_INT_BT_SCO_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AFE_PCM_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_AFE_PCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("SEC_AUX_PCM_UL_TX", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_SEC_AUXPCM_TX, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_TERT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_TERT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_TERT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("TERT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_TERT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_1", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_1, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_2", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_2, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUAT_TDM_TX_3", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_QUAT_TDM_TX_3, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_0", SND_SOC_NOPM,
		MSM_BACKEND_DAI_TERT_TDM_RX_1,
		MSM_BACKEND_DAI_QUIN_TDM_TX_0, 1, 0,
		msm_routing_get_port_mixer,
		msm_routing_put_port_mixer),
	SOC_DOUBLE_EXT("QUIN_TDM_TX_1", SND_SOC_NO