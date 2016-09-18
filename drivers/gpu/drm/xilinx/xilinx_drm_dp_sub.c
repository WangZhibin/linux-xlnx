/*
 * DisplayPort subsystem support for Xilinx DRM KMS
 *
 *  Copyright (C) 2015 Xilinx, Inc.
 *
 *  Author: Hyun Woo Kwon <hyunk@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fourcc.h>

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "xilinx_drm_dp_sub.h"
#include "xilinx_drm_drv.h"

/* Blender registers */
#define XILINX_DP_SUB_V_BLEND_BG_CLR_0				0x0
#define XILINX_DP_SUB_V_BLEND_BG_CLR_1				0x4
#define XILINX_DP_SUB_V_BLEND_BG_CLR_2				0x8
#define XILINX_DP_SUB_V_BLEND_SET_GLOBAL_ALPHA			0xc
#define XILINX_DP_SUB_V_BLEND_SET_GLOBAL_ALPHA_MASK		0x1fe
#define XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT			0x14
#define XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_RGB		0x0
#define XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_YCBCR444		0x1
#define XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_YCBCR422		0x2
#define XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_YONLY		0x3
#define XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_XVYCC		0x4
#define XILINX_DP_SUB_V_BLEND_LAYER_CONTROL			0x18
#define XILINX_DP_SUB_V_BLEND_LAYER_CONTROL_EN			BIT(0)
#define XILINX_DP_SUB_V_BLEND_LAYER_CONTROL_RGB			BIT(1)
#define XILINX_DP_SUB_V_BLEND_LAYER_CONTROL_BYPASS		BIT(8)
#define XILINX_DP_SUB_V_BLEND_RGB2YCBCR_COEFF0			0x20
#define XILINX_DP_SUB_V_BLEND_RGB2YCBCR_COEFF1			0x24
#define XILINX_DP_SUB_V_BLEND_RGB2YCBCR_COEFF2			0x28
#define XILINX_DP_SUB_V_BLEND_RGB2YCBCR_COEFF3			0x2c
#define XILINX_DP_SUB_V_BLEND_RGB2YCBCR_COEFF4			0x30
#define XILINX_DP_SUB_V_BLEND_RGB2YCBCR_COEFF5			0x34
#define XILINX_DP_SUB_V_BLEND_RGB2YCBCR_COEFF6			0x38
#define XILINX_DP_SUB_V_BLEND_RGB2YCBCR_COEFF7			0x3c
#define XILINX_DP_SUB_V_BLEND_RGB2YCBCR_COEFF8			0x40
#define XILINX_DP_SUB_V_BLEND_YCBCR2RGB_COEFF0			0x44
#define XILINX_DP_SUB_V_BLEND_YCBCR2RGB_COEFF1			0x48
#define XILINX_DP_SUB_V_BLEND_YCBCR2RGB_COEFF2			0x4c
#define XILINX_DP_SUB_V_BLEND_YCBCR2RGB_COEFF3			0x50
#define XILINX_DP_SUB_V_BLEND_YCBCR2RGB_COEFF4			0x54
#define XILINX_DP_SUB_V_BLEND_YCBCR2RGB_COEFF5			0x58
#define XILINX_DP_SUB_V_BLEND_YCBCR2RGB_COEFF6			0x5c
#define XILINX_DP_SUB_V_BLEND_YCBCR2RGB_COEFF7			0x60
#define XILINX_DP_SUB_V_BLEND_YCBCR2RGB_COEFF8			0x64
#define XILINX_DP_SUB_V_BLEND_LUMA_INCSC_OFFSET			0x68
#define XILINX_DP_SUB_V_BLEND_CR_INCSC_OFFSET			0x6c
#define XILINX_DP_SUB_V_BLEND_CB_INCSC_OFFSET			0x70
#define XILINX_DP_SUB_V_BLEND_LUMA_OUTCSC_OFFSET		0x74
#define XILINX_DP_SUB_V_BLEND_CR_OUTCSC_OFFSET			0x78
#define XILINX_DP_SUB_V_BLEND_CB_OUTCSC_OFFSET			0x7c
#define XILINX_DP_SUB_V_BLEND_CHROMA_KEY_ENABLE			0x1d0
#define XILINX_DP_SUB_V_BLEND_CHROMA_KEY_COMP1			0x1d4
#define XILINX_DP_SUB_V_BLEND_CHROMA_KEY_COMP2			0x1d8
#define XILINX_DP_SUB_V_BLEND_CHROMA_KEY_COMP3			0x1dc

/* AV buffer manager registers */
#define XILINX_DP_SUB_AV_BUF_FORMAT				0x0
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_SHIFT		0
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_MASK			(0x1f << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_UYVY			(0 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_VYUY			(1 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YVYU			(2 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YUYV			(3 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YU16			(4 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YU24			(5 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YU16CI		(6 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_MONO			(7 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YU16CI2		(8 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YUV444		(9 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_RGB888		(10 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_RGBA8880		(11 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_RGB888_10		(12 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YUV444_10		(13 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YV16CI2_10		(14 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YV16CI_10		(15 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YV16_10		(16 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YV24_10		(17 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YONLY_10		(18 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YV16_420		(19 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YV16CI_420		(20 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YV16CI2_420		(21 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YV16_420_10		(22 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YV16CI_420_10	(23 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YV16CI2_420_10	(24 << 0)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_SHIFT		8
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_MASK			(0xf << 8)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_RGBA8888		(0 << 8)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_ABGR8888		(1 << 8)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_RGB888		(2 << 8)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_BGR888		(3 << 8)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_RGBA5551		(4 << 8)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_RGBA4444		(5 << 8)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_RGB565		(6 << 8)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_8BPP			(7 << 8)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_4BPP			(8 << 8)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_2BPP			(9 << 8)
#define XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_1BPP			(10 << 8)
#define XILINX_DP_SUB_AV_BUF_NON_LIVE_LATENCY			0x8
#define XILINX_DP_SUB_AV_BUF_CHBUF				0x10
#define XILINX_DP_SUB_AV_BUF_CHBUF_EN				BIT(0)
#define XILINX_DP_SUB_AV_BUF_CHBUF_FLUSH			BIT(1)
#define XILINX_DP_SUB_AV_BUF_CHBUF_BURST_LEN_SHIFT		2
#define XILINX_DP_SUB_AV_BUF_CHBUF_BURST_LEN_MASK		(0xf << 2)
#define XILINX_DP_SUB_AV_BUF_CHBUF_BURST_LEN_MAX		0xf
#define XILINX_DP_SUB_AV_BUF_CHBUF_BURST_LEN_AUD_MAX		0x3
#define XILINX_DP_SUB_AV_BUF_STATUS				0x28
#define XILINX_DP_SUB_AV_BUF_STC_CTRL				0x2c
#define XILINX_DP_SUB_AV_BUF_STC_CTRL_EN			BIT(0)
#define XILINX_DP_SUB_AV_BUF_STC_CTRL_EVENT_SHIFT		1
#define XILINX_DP_SUB_AV_BUF_STC_CTRL_EVENT_EX_VSYNC		0
#define XILINX_DP_SUB_AV_BUF_STC_CTRL_EVENT_EX_VID		1
#define XILINX_DP_SUB_AV_BUF_STC_CTRL_EVENT_EX_AUD		2
#define XILINX_DP_SUB_AV_BUF_STC_CTRL_EVENT_INT_VSYNC		3
#define XILINX_DP_SUB_AV_BUF_STC_INIT_VALUE0			0x30
#define XILINX_DP_SUB_AV_BUF_STC_INIT_VALUE1			0x34
#define XILINX_DP_SUB_AV_BUF_STC_ADJ				0x38
#define XILINX_DP_SUB_AV_BUF_STC_VID_VSYNC_TS0			0x3c
#define XILINX_DP_SUB_AV_BUF_STC_VID_VSYNC_TS1			0x40
#define XILINX_DP_SUB_AV_BUF_STC_EXT_VSYNC_TS0			0x44
#define XILINX_DP_SUB_AV_BUF_STC_EXT_VSYNC_TS1			0x48
#define XILINX_DP_SUB_AV_BUF_STC_CUSTOM_EVENT_TS0		0x4c
#define XILINX_DP_SUB_AV_BUF_STC_CUSTOM_EVENT_TS1		0x50
#define XILINX_DP_SUB_AV_BUF_STC_CUSTOM_EVENT2_TS0		0x54
#define XILINX_DP_SUB_AV_BUF_STC_CUSTOM_EVENT2_TS1		0x58
#define XILINX_DP_SUB_AV_BUF_STC_SNAPSHOT0			0x60
#define XILINX_DP_SUB_AV_BUF_STC_SNAPSHOT1			0x64
#define XILINX_DP_SUB_AV_BUF_OUTPUT				0x70
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID1_SHIFT			0
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID1_MASK			(0x3 << 0)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID1_PL			(0 << 0)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID1_MEM			(1 << 0)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID1_PATTERN		(2 << 0)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID1_NONE			(3 << 0)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID2_SHIFT			2
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID2_MASK			(0x3 << 2)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID2_DISABLE		(0 << 2)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID2_MEM			(1 << 2)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID2_LIVE			(2 << 2)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_VID2_NONE			(3 << 2)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_AUD1_SHIFT			4
#define XILINX_DP_SUB_AV_BUF_OUTPUT_AUD1_MASK			(0x3 << 4)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_AUD1_PL			(0 << 4)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_AUD1_MEM			(1 << 4)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_AUD1_PATTERN		(2 << 4)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_AUD1_DISABLE		(3 << 4)
#define XILINX_DP_SUB_AV_BUF_OUTPUT_AUD2_EN			BIT(6)
#define XILINX_DP_SUB_AV_BUF_HCOUNT_VCOUNT_INT0			0x74
#define XILINX_DP_SUB_AV_BUF_HCOUNT_VCOUNT_INT1			0x78
#define XILINX_DP_SUB_AV_BUF_PATTERN_GEN_SELECT			0x100
#define XILINX_DP_SUB_AV_BUF_CLK_SRC				0x120
#define XILINX_DP_SUB_AV_BUF_CLK_SRC_VID_FROM_PS		BIT(0)
#define XILINX_DP_SUB_AV_BUF_CLK_SRC_AUD_FROM_PS		BIT(1)
#define XILINX_DP_SUB_AV_BUF_CLK_SRC_VID_INTERNAL_TIMING	BIT(2)
#define XILINX_DP_SUB_AV_BUF_SRST_REG				0x124
#define XILINX_DP_SUB_AV_BUF_AUDIO_CH_CONFIG			0x12c
#define XILINX_DP_SUB_AV_BUF_GFX_COMP0_SF			0x200
#define XILINX_DP_SUB_AV_BUF_GFX_COMP1_SF			0x204
#define XILINX_DP_SUB_AV_BUF_GFX_COMP2_SF			0x208
#define XILINX_DP_SUB_AV_BUF_VID_COMP0_SF			0x20c
#define XILINX_DP_SUB_AV_BUF_VID_COMP1_SF			0x210
#define XILINX_DP_SUB_AV_BUF_VID_COMP2_SF			0x214
#define XILINX_DP_SUB_AV_BUF_LIVE_VID_COMP0_SF			0x218
#define XILINX_DP_SUB_AV_BUF_LIVE_VID_COMP1_SF			0x21c
#define XILINX_DP_SUB_AV_BUF_LIVE_VID_COMP2_SF			0x220
#define XILINX_DP_SUB_AV_BUF_4BIT_SF				0x11111
#define XILINX_DP_SUB_AV_BUF_5BIT_SF				0x10842
#define XILINX_DP_SUB_AV_BUF_6BIT_SF				0x10410
#define XILINX_DP_SUB_AV_BUF_8BIT_SF				0x10101
#define XILINX_DP_SUB_AV_BUF_10BIT_SF				0x10040
#define XILINX_DP_SUB_AV_BUF_NULL_SF				0
#define XILINX_DP_SUB_AV_BUF_NUM_SF				3
#define XILINX_DP_SUB_AV_BUF_LIVE_CB_CR_SWAP			0x224
#define XILINX_DP_SUB_AV_BUF_PALETTE_MEMORY			0x400

/* Audio registers */
#define XILINX_DP_SUB_AUD_MIXER_VOLUME				0x0
#define XILINX_DP_SUB_AUD_MIXER_VOLUME_NO_SCALE			0x20002000
#define XILINX_DP_SUB_AUD_MIXER_META_DATA			0x4
#define XILINX_DP_SUB_AUD_CH_STATUS0				0x8
#define XILINX_DP_SUB_AUD_CH_STATUS1				0xc
#define XILINX_DP_SUB_AUD_CH_STATUS2				0x10
#define XILINX_DP_SUB_AUD_CH_STATUS3				0x14
#define XILINX_DP_SUB_AUD_CH_STATUS4				0x18
#define XILINX_DP_SUB_AUD_CH_STATUS5				0x1c
#define XILINX_DP_SUB_AUD_CH_A_DATA0				0x20
#define XILINX_DP_SUB_AUD_CH_A_DATA1				0x24
#define XILINX_DP_SUB_AUD_CH_A_DATA2				0x28
#define XILINX_DP_SUB_AUD_CH_A_DATA3				0x2c
#define XILINX_DP_SUB_AUD_CH_A_DATA4				0x30
#define XILINX_DP_SUB_AUD_CH_A_DATA5				0x34
#define XILINX_DP_SUB_AUD_CH_B_DATA0				0x38
#define XILINX_DP_SUB_AUD_CH_B_DATA1				0x3c
#define XILINX_DP_SUB_AUD_CH_B_DATA2				0x40
#define XILINX_DP_SUB_AUD_CH_B_DATA3				0x44
#define XILINX_DP_SUB_AUD_CH_B_DATA4				0x48
#define XILINX_DP_SUB_AUD_CH_B_DATA5				0x4c

#define XILINX_DP_SUB_AV_BUF_NUM_VID_GFX_BUFFERS		4
#define XILINX_DP_SUB_AV_BUF_NUM_BUFFERS			6

/**
 * enum xilinx_drm_dp_sub_layer_type - Layer type
 * @XILINX_DRM_DP_SUB_LAYER_VID: video layer
 * @XILINX_DRM_DP_SUB_LAYER_GFX: graphics layer
 */
enum xilinx_drm_dp_sub_layer_type {
	XILINX_DRM_DP_SUB_LAYER_VID,
	XILINX_DRM_DP_SUB_LAYER_GFX
};

/**
 * struct xilinx_drm_dp_sub_layer - DP subsystem layer
 * @id: layer ID
 * @offset: layer offset in the register space
 * @avail: flag if layer is available
 * @primary: flag for primary plane
 * @enabled: flag if the layer is enabled
 * @fmt: format descriptor
 * @w: width
 * @h: height
 * @other: other layer
 */
struct xilinx_drm_dp_sub_layer {
	enum xilinx_drm_dp_sub_layer_type id;
	u32 offset;
	bool avail;
	bool primary;
	bool enabled;
	const struct xilinx_drm_dp_sub_fmt *fmt;
	uint32_t w;
	uint32_t h;
	struct xilinx_drm_dp_sub_layer *other;
};

/**
 * struct xilinx_drm_dp_sub_blend - DP subsystem blender
 * @base: pre-calculated base address
 */
struct xilinx_drm_dp_sub_blend {
	void __iomem *base;
};

/**
 * struct xilinx_drm_dp_sub_av_buf - DP subsystem av buffer manager
 * @base: pre-calculated base address
 */
struct xilinx_drm_dp_sub_av_buf {
	void __iomem *base;
};

/**
 * struct xilinx_drm_dp_sub_aud - DP subsystem audio
 * @base: pre-calculated base address
 */
struct xilinx_drm_dp_sub_aud {
	void __iomem *base;
};

/**
 * struct xilinx_drm_dp_sub - DP subsystem
 * @dev: device structure
 * @blend: blender device
 * @av_buf: av buffer manager device
 * @aud: audio device
 * @layers: layers
 * @list: entry in the global DP subsystem list
 * @vblank_fn: vblank handler
 * @vblank_data: vblank data to be used in vblank_fn
 * @lock: lock for access to struct xilinx_drm_dp_sub
 */
struct xilinx_drm_dp_sub {
	struct device *dev;
	struct xilinx_drm_dp_sub_blend blend;
	struct xilinx_drm_dp_sub_av_buf av_buf;
	struct xilinx_drm_dp_sub_aud aud;
	struct xilinx_drm_dp_sub_layer layers[XILINX_DRM_DP_SUB_NUM_LAYERS];
	struct list_head list;
	void (*vblank_fn)(void *);
	void *vblank_data;
	spinlock_t lock;
};

/**
 * struct xilinx_drm_dp_sub_fmt - DP subsystem format mapping
 * @drm_fmt: drm format
 * @dp_sub_fmt: DP subsystem format
 * @rgb: flag for RGB formats
 * @sf: scaling factors for upto 3 color components
 */
struct xilinx_drm_dp_sub_fmt {
	uint32_t drm_fmt;
	u32 dp_sub_fmt;
	bool rgb;
	u32 sf[3];
};

static LIST_HEAD(xilinx_drm_dp_sub_list);
static DEFINE_MUTEX(xilinx_drm_dp_sub_lock);

/* Blender functions */

/**
 * xilinx_drm_dp_sub_blend_layer_enable - Enable a layer
 * @blend: blend object
 * @layer: layer to enable
 * @bypass: bypass flag when the other layer is disabled
 *
 * Enable a layer @layer.
 */
static void
xilinx_drm_dp_sub_blend_layer_enable(struct xilinx_drm_dp_sub_blend *blend,
				     struct xilinx_drm_dp_sub_layer *layer,
				     bool bypass)
{
	u32 reg;

	reg = layer->fmt->rgb ? XILINX_DP_SUB_V_BLEND_LAYER_CONTROL_RGB : 0;
	reg |= bypass ? XILINX_DP_SUB_V_BLEND_LAYER_CONTROL_BYPASS :
	       XILINX_DP_SUB_V_BLEND_LAYER_CONTROL_EN;

	xilinx_drm_writel(blend->base,
			  XILINX_DP_SUB_V_BLEND_LAYER_CONTROL + layer->offset,
			  reg);
}

/**
 * xilinx_drm_dp_sub_blend_layer_disable - Disable a layer
 * @blend: blend object
 * @layer: layer to disable
 *
 * Disable a layer @layer.
 */
static void
xilinx_drm_dp_sub_blend_layer_disable(struct xilinx_drm_dp_sub_blend *blend,
				      struct xilinx_drm_dp_sub_layer *layer)
{
	xilinx_drm_writel(blend->base,
			  XILINX_DP_SUB_V_BLEND_LAYER_CONTROL + layer->offset,
			  0);
}

/**
 * xilinx_drm_dp_sub_blend_set_bg_color - Set the background color
 * @blend: blend object
 * @c0: color component 0
 * @c1: color component 1
 * @c2: color component 2
 *
 * Set the background color.
 */
static void
xilinx_drm_dp_sub_blend_set_bg_color(struct xilinx_drm_dp_sub_blend *blend,
				     u32 c0, u32 c1, u32 c2)
{
	xilinx_drm_writel(blend->base, XILINX_DP_SUB_V_BLEND_BG_CLR_0, c0);
	xilinx_drm_writel(blend->base, XILINX_DP_SUB_V_BLEND_BG_CLR_1, c1);
	xilinx_drm_writel(blend->base, XILINX_DP_SUB_V_BLEND_BG_CLR_2, c2);
}

/**
 * xilinx_drm_dp_sub_blend_set_alpha - Set the alpha for blending
 * @blend: blend object
 * @enable: flag to enable or disable alpha blending
 * @alpha: alpha value to be used
 *
 * Set the alpha for blending. @enable can be used to enable or disable
 * blending.
 */
static void
xilinx_drm_dp_sub_blend_set_alpha(struct xilinx_drm_dp_sub_blend *blend,
				  bool enable, u32 alpha)
{
	xilinx_drm_writel(blend->base, XILINX_DP_SUB_V_BLEND_SET_GLOBAL_ALPHA,
			  alpha << 1 | enable);
}

static const struct xilinx_drm_dp_sub_fmt blend_output_fmts[] = {
	{
		.drm_fmt	= DRM_FORMAT_RGB888,
		.dp_sub_fmt	= XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_RGB,
		.rgb		= true,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_8BIT_SF
	}, {
		.drm_fmt	= DRM_FORMAT_YUV444,
		.dp_sub_fmt	= XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_YCBCR444,
		.rgb		= false,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_8BIT_SF
	}, {
		.drm_fmt	= DRM_FORMAT_YUV422,
		.dp_sub_fmt	= XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_YCBCR422,
		.rgb		= false,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_4BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_NULL_SF
	}
};

/**
 * xilinx_drm_dp_sub_blend_set_output_fmt - Set the output format
 * @blend: blend object
 * @fmt: output format
 *
 * Set the output format to @fmt.
 */
static void
xilinx_drm_dp_sub_blend_set_output_fmt(struct xilinx_drm_dp_sub_blend *blend,
				       u32 fmt)
{
	xilinx_drm_writel(blend->base, XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT,
			  fmt);
}

/* AV buffer manager functions */

/* Default video format = BGR888 */
#define AV_BUF_DEFAULT_VID_FMT_ID	5

static const struct xilinx_drm_dp_sub_fmt av_buf_vid_fmts[] = {
	{
		.drm_fmt	= DRM_FORMAT_YVYU,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_UYVY,
		.rgb		= false,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_NULL_SF
	}, {
		.drm_fmt	= DRM_FORMAT_YUYV,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_VYUY,
		.rgb		= false,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_NULL_SF
	}, {
		.drm_fmt	= DRM_FORMAT_UYVY,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YVYU,
		.rgb		= false,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_NULL_SF
	}, {
		.drm_fmt	= DRM_FORMAT_VYUY,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_YUYV,
		.rgb		= false,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_NULL_SF
	}, {
		.drm_fmt	= DRM_FORMAT_BGR888,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_RGB888,
		.rgb		= true,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_8BIT_SF
	}, {
		.drm_fmt	= DRM_FORMAT_XBGR8888,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_RGBA8880,
		.rgb		= true,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_8BIT_SF
	}
};

/* Default graphics format = RGB565 */
#define AV_BUF_DEFAULT_GFX_FMT_ID	6

static const struct xilinx_drm_dp_sub_fmt av_buf_gfx_fmts[] = {
	{
		.drm_fmt	= DRM_FORMAT_ABGR8888,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_RGBA8888,
		.rgb		= true,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_8BIT_SF
	}, {
		.drm_fmt	= DRM_FORMAT_RGBA8888,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_ABGR8888,
		.rgb		= true,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_8BIT_SF
	}, {
		.drm_fmt	= DRM_FORMAT_BGR888,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_RGB888,
		.rgb		= true,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_8BIT_SF
	}, {
		.drm_fmt	= DRM_FORMAT_RGB888,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_BGR888,
		.rgb		= true,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_8BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_8BIT_SF
	}, {
		.drm_fmt	= DRM_FORMAT_ABGR1555,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_RGBA5551,
		.rgb		= true,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_5BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_5BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_5BIT_SF
	}, {
		.drm_fmt	= DRM_FORMAT_ABGR4444,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_RGBA4444,
		.rgb		= true,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_4BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_4BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_4BIT_SF
	}, {
		.drm_fmt	= DRM_FORMAT_RGB565,
		.dp_sub_fmt	= XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_RGB565,
		.rgb		= true,
		.sf[0]		= XILINX_DP_SUB_AV_BUF_5BIT_SF,
		.sf[1]		= XILINX_DP_SUB_AV_BUF_6BIT_SF,
		.sf[2]		= XILINX_DP_SUB_AV_BUF_5BIT_SF
	}
};

/**
 * xilinx_drm_dp_sub_av_buf_set_fmt - Set the input formats
 * @av_buf: av buffer manager
 * @fmt: formats
 *
 * Set the av buffer manager format to @fmt. @fmt should have valid values
 * for both video and graphics layer.
 */
static void
xilinx_drm_dp_sub_av_buf_set_fmt(struct xilinx_drm_dp_sub_av_buf *av_buf,
				 u32 fmt)
{
	xilinx_drm_writel(av_buf->base, XILINX_DP_SUB_AV_BUF_FORMAT, fmt);
}

/**
 * xilinx_drm_dp_sub_av_buf_get_fmt - Get the input formats
 * @av_buf: av buffer manager
 *
 * Get the input formats (which include video and graphics) of
 * av buffer manager.
 *
 * Return: value of XILINX_DP_SUB_AV_BUF_FORMAT register.
 */
static u32
xilinx_drm_dp_sub_av_buf_get_fmt(struct xilinx_drm_dp_sub_av_buf *av_buf)
{
	return xilinx_drm_readl(av_buf->base, XILINX_DP_SUB_AV_BUF_FORMAT);
}

/**
 * xilinx_drm_dp_sub_av_buf_set_vid_clock_src - Set the video clock source
 * @av_buf: av buffer manager
 * @from_ps: flag if the video clock is from ps
 *
 * Set the video clock source based on @from_ps. It can come from either PS or
 * PL.
 */
static void xilinx_drm_dp_sub_av_buf_set_vid_clock_src(
		struct xilinx_drm_dp_sub_av_buf *av_buf, bool from_ps)
{
	u32 reg;

	reg = xilinx_drm_readl(av_buf->base, XILINX_DP_SUB_AV_BUF_CLK_SRC);
	if (from_ps)
		reg |= XILINX_DP_SUB_AV_BUF_CLK_SRC_VID_FROM_PS;
	else
		reg &= ~XILINX_DP_SUB_AV_BUF_CLK_SRC_VID_FROM_PS;
	xilinx_drm_writel(av_buf->base, XILINX_DP_SUB_AV_BUF_CLK_SRC, reg);
}

/**
 * xilinx_drm_dp_sub_av_buf_set_vid_timing_src - Set the video timing source
 * @av_buf: av buffer manager
 * @internal: flag if the video timing is generated internally
 *
 * Set the video timing source based on @internal. It can come externally or
 * be generated internally.
 */
static void xilinx_drm_dp_sub_av_buf_set_vid_timing_src(
		struct xilinx_drm_dp_sub_av_buf *av_buf, bool internal)
{
	u32 reg;

	reg = xilinx_drm_readl(av_buf->base, XILINX_DP_SUB_AV_BUF_CLK_SRC);
	if (internal)
		reg |= XILINX_DP_SUB_AV_BUF_CLK_SRC_VID_INTERNAL_TIMING;
	else
		reg &= ~XILINX_DP_SUB_AV_BUF_CLK_SRC_VID_INTERNAL_TIMING;
	xilinx_drm_writel(av_buf->base, XILINX_DP_SUB_AV_BUF_CLK_SRC, reg);
}

/**
 * xilinx_drm_dp_sub_av_buf_set_aud_clock_src - Set the audio clock source
 * @av_buf: av buffer manager
 * @from_ps: flag if the video clock is from ps
 *
 * Set the audio clock source based on @from_ps. It can come from either PS or
 * PL.
 */
static void xilinx_drm_dp_sub_av_buf_set_aud_clock_src(
		struct xilinx_drm_dp_sub_av_buf *av_buf, bool from_ps)
{
	u32 reg;

	reg = xilinx_drm_readl(av_buf->base, XILINX_DP_SUB_AV_BUF_CLK_SRC);
	if (from_ps)
		reg |= XILINX_DP_SUB_AV_BUF_CLK_SRC_AUD_FROM_PS;
	else
		reg &= ~XILINX_DP_SUB_AV_BUF_CLK_SRC_AUD_FROM_PS;
	xilinx_drm_writel(av_buf->base, XILINX_DP_SUB_AV_BUF_CLK_SRC, reg);
}

/**
 * xilinx_drm_dp_sub_av_buf_enable_buf - Enable buffers
 * @av_buf: av buffer manager
 *
 * Enable all (video and audio) buffers.
 */
static void
xilinx_drm_dp_sub_av_buf_enable_buf(struct xilinx_drm_dp_sub_av_buf *av_buf)
{
	u32 reg, i;

	reg = XILINX_DP_SUB_AV_BUF_CHBUF_EN;
	reg |= XILINX_DP_SUB_AV_BUF_CHBUF_BURST_LEN_MAX <<
	       XILINX_DP_SUB_AV_BUF_CHBUF_BURST_LEN_SHIFT;

	for (i = 0; i < XILINX_DP_SUB_AV_BUF_NUM_VID_GFX_BUFFERS; i++)
		xilinx_drm_writel(av_buf->base,
				  XILINX_DP_SUB_AV_BUF_CHBUF + i * 4, reg);

	reg = XILINX_DP_SUB_AV_BUF_CHBUF_EN;
	reg |= XILINX_DP_SUB_AV_BUF_CHBUF_BURST_LEN_AUD_MAX <<
	       XILINX_DP_SUB_AV_BUF_CHBUF_BURST_LEN_SHIFT;

	for (; i < XILINX_DP_SUB_AV_BUF_NUM_BUFFERS; i++)
		xilinx_drm_writel(av_buf->base,
				  XILINX_DP_SUB_AV_BUF_CHBUF + i * 4, reg);
}

/**
 * xilinx_drm_dp_sub_av_buf_disable_buf - Disable buffers
 * @av_buf: av buffer manager
 *
 * Disable all (video and audio) buffers.
 */
static void
xilinx_drm_dp_sub_av_buf_disable_buf(struct xilinx_drm_dp_sub_av_buf *av_buf)
{
	u32 reg, i;

	reg = XILINX_DP_SUB_AV_BUF_CHBUF_FLUSH & ~XILINX_DP_SUB_AV_BUF_CHBUF_EN;
	for (i = 0; i < XILINX_DP_SUB_AV_BUF_NUM_BUFFERS; i++)
		xilinx_drm_writel(av_buf->base,
				  XILINX_DP_SUB_AV_BUF_CHBUF + i * 4, reg);
}

/**
 * xilinx_drm_dp_sub_av_buf_enable_aud - Enable audio
 * @av_buf: av buffer manager
 *
 * Enable all audio buffers.
 */
static void
xilinx_drm_dp_sub_av_buf_enable_aud(struct xilinx_drm_dp_sub_av_buf *av_buf)
{
	u32 reg;

	reg = xilinx_drm_readl(av_buf->base, XILINX_DP_SUB_AV_BUF_OUTPUT);
	reg &= ~XILINX_DP_SUB_AV_BUF_OUTPUT_AUD1_MASK;
	reg |= XILINX_DP_SUB_AV_BUF_OUTPUT_AUD1_MEM;
	reg |= XILINX_DP_SUB_AV_BUF_OUTPUT_AUD2_EN;
	xilinx_drm_writel(av_buf->base, XILINX_DP_SUB_AV_BUF_OUTPUT, reg);
}

/**
 * xilinx_drm_dp_sub_av_buf_disable_aud - Disable audio
 * @av_buf: av buffer manager
 *
 * Disable all audio buffers.
 */
static void
xilinx_drm_dp_sub_av_buf_disable_aud(struct xilinx_drm_dp_sub_av_buf *av_buf)
{
	u32 reg;

	reg = xilinx_drm_readl(av_buf->base, XILINX_DP_SUB_AV_BUF_OUTPUT);
	reg &= ~XILINX_DP_SUB_AV_BUF_OUTPUT_AUD1_MASK;
	reg |= XILINX_DP_SUB_AV_BUF_OUTPUT_AUD1_DISABLE;
	reg &= ~XILINX_DP_SUB_AV_BUF_OUTPUT_AUD2_EN;
	xilinx_drm_writel(av_buf->base, XILINX_DP_SUB_AV_BUF_OUTPUT, reg);
}

/**
 * xilinx_drm_dp_sub_av_buf_enable_vid - Enable the video layer buffer
 * @av_buf: av buffer manager
 * @layer: layer to enable
 *
 * Enable the video/graphics buffer for @layer.
 */
static void
xilinx_drm_dp_sub_av_buf_enable_vid(struct xilinx_drm_dp_sub_av_buf *av_buf,
				    struct xilinx_drm_dp_sub_layer *layer)
{
	u32 reg;

	reg = xilinx_drm_readl(av_buf->base, XILINX_DP_SUB_AV_BUF_OUTPUT);
	if (layer->id == XILINX_DRM_DP_SUB_LAYER_VID) {
		reg &= ~XILINX_DP_SUB_AV_BUF_OUTPUT_VID1_MASK;
		reg |= XILINX_DP_SUB_AV_BUF_OUTPUT_VID1_MEM;
	} else {
		reg &= ~XILINX_DP_SUB_AV_BUF_OUTPUT_VID2_MASK;
		reg |= XILINX_DP_SUB_AV_BUF_OUTPUT_VID2_MEM;
	}
	xilinx_drm_writel(av_buf->base, XILINX_DP_SUB_AV_BUF_OUTPUT, reg);
}

/**
 * xilinx_drm_dp_sub_av_buf_disable_vid - Disable the video layer buffer
 * @av_buf: av buffer manager
 * @layer: layer to disable
 *
 * Disable the video/graphics buffer for @layer.
 */
static void
xilinx_drm_dp_sub_av_buf_disable_vid(struct xilinx_drm_dp_sub_av_buf *av_buf,
				     struct xilinx_drm_dp_sub_layer *layer)
{
	u32 reg;

	reg = xilinx_drm_readl(av_buf->base, XILINX_DP_SUB_AV_BUF_OUTPUT);

	if (layer->id == XILINX_DRM_DP_SUB_LAYER_VID) {
		reg &= ~XILINX_DP_SUB_AV_BUF_OUTPUT_VID1_MASK;
		reg |= XILINX_DP_SUB_AV_BUF_OUTPUT_VID1_NONE;
	} else {
		reg &= ~XILINX_DP_SUB_AV_BUF_OUTPUT_VID2_MASK;
		reg |= XILINX_DP_SUB_AV_BUF_OUTPUT_VID2_DISABLE;
	}

	xilinx_drm_writel(av_buf->base, XILINX_DP_SUB_AV_BUF_OUTPUT, reg);
}

/**
 * xilinx_drm_dp_sub_av_buf_init_fmts - Initialize the layer formats
 * @av_buf: av buffer manager
 * @vid_fmt: video format descriptor
 * @gfx_fmt: graphics format descriptor
 *
 * Initialize formats of both video and graphics layers.
 */
static void
xilinx_drm_dp_sub_av_buf_init_fmts(struct xilinx_drm_dp_sub_av_buf *av_buf,
				   const struct xilinx_drm_dp_sub_fmt *vid_fmt,
				   const struct xilinx_drm_dp_sub_fmt *gfx_fmt)
{
	u32 reg;

	reg = vid_fmt->dp_sub_fmt;
	reg |= gfx_fmt->dp_sub_fmt;
	xilinx_drm_writel(av_buf->base, XILINX_DP_SUB_AV_BUF_FORMAT, reg);
}

/**
 * xilinx_drm_dp_sub_av_buf_init_sf - Initialize scaling factors
 * @av_buf: av buffer manager
 * @vid_fmt: video format descriptor
 * @gfx_fmt: graphics format descriptor
 *
 * Initialize scaling factors for both video and graphics layers.
 */
static void
xilinx_drm_dp_sub_av_buf_init_sf(struct xilinx_drm_dp_sub_av_buf *av_buf,
				 const struct xilinx_drm_dp_sub_fmt *vid_fmt,
				 const struct xilinx_drm_dp_sub_fmt *gfx_fmt)
{
	unsigned int i;
	int offset;

	offset = XILINX_DP_SUB_AV_BUF_GFX_COMP0_SF;
	for (i = 0; i < XILINX_DP_SUB_AV_BUF_NUM_SF; i++)
		xilinx_drm_writel(av_buf->base, offset, gfx_fmt->sf[i]);

	offset = XILINX_DP_SUB_AV_BUF_VID_COMP0_SF;
	for (i = 0; i < XILINX_DP_SUB_AV_BUF_NUM_SF; i++)
		xilinx_drm_writel(av_buf->base, offset, vid_fmt->sf[i]);
}

/* Audio functions */

/**
 * xilinx_drm_dp_sub_aud_init - Initialize the audio
 * @aud: audio
 *
 * Initialize the audio with default mixer volume.
 */
static void xilinx_drm_dp_sub_aud_init(struct xilinx_drm_dp_sub_aud *aud)
{
	xilinx_drm_set(aud->base, XILINX_DP_SUB_AUD_MIXER_VOLUME,
		       XILINX_DP_SUB_AUD_MIXER_VOLUME_NO_SCALE);
}

/* DP subsystem layer functions */

/**
 * xilinx_drm_dp_sub_layer_check_size - Verify width and height for the layer
 * @dp_sub: DP subsystem
 * @layer: layer
 * @width: width
 * @height: height
 *
 * The DP subsystem has the limitation that both layers should have
 * identical size. This function stores width and height of @layer, and verifies
 * if the size (width and height) is valid.
 *
 * Return: 0 on success, or -EINVAL if width or/and height is invalid.
 */
int xilinx_drm_dp_sub_layer_check_size(struct xilinx_drm_dp_sub *dp_sub,
				       struct xilinx_drm_dp_sub_layer *layer,
				       uint32_t width, uint32_t height)
{
	struct xilinx_drm_dp_sub_layer *other = layer->other;
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);

	if (other->enabled && (other->w != width || other->h != height)) {
		dev_err(dp_sub->dev, "Layer width:height must be %d:%d\n",
			other->w, other->h);
		spin_unlock_irqrestore(&dp_sub->lock, flags);
		return -EINVAL;
	}

	layer->w = width;
	layer->h = height;

	spin_unlock_irqrestore(&dp_sub->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_layer_check_size);

/**
 * xilinx_drm_dp_sub_map_fmt - Find the DP subsystem format for given drm format
 * @fmts: format table to look up
 * @size: size of the table @fmts
 * @drm_fmt: DRM format to search
 *
 * Search a DP subsystem format corresponding to the given DRM format @drm_fmt,
 * and return the format descriptor which contains the DP subsystem format
 * value.
 *
 * Return: a DP subsystem format descriptor on success, or NULL.
 */
static const struct xilinx_drm_dp_sub_fmt *
xilinx_drm_dp_sub_map_fmt(const struct xilinx_drm_dp_sub_fmt fmts[],
			  unsigned int size, uint32_t drm_fmt)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		if (fmts[i].drm_fmt == drm_fmt)
			return &fmts[i];

	return NULL;
}

/**
 * xilinx_drm_dp_sub_set_fmt - Set the format of the layer
 * @dp_sub: DP subsystem
 * @layer: layer to set the format
 * @drm_fmt: DRM format to set
 *
 * Set the format of the given layer to @drm_fmt.
 *
 * Return: 0 on success. -EINVAL if @drm_fmt is not supported by the layer.
 */
int xilinx_drm_dp_sub_layer_set_fmt(struct xilinx_drm_dp_sub *dp_sub,
				    struct xilinx_drm_dp_sub_layer *layer,
				    uint32_t drm_fmt)
{
	const struct xilinx_drm_dp_sub_fmt *table;
	const struct xilinx_drm_dp_sub_fmt *fmt;
	u32 size, fmts, mask;
	bool vid;
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);

	if (layer->id == XILINX_DRM_DP_SUB_LAYER_VID) {
		table = av_buf_vid_fmts;
		size = ARRAY_SIZE(av_buf_vid_fmts);
		mask = ~XILINX_DP_SUB_AV_BUF_FORMAT_NL_VID_MASK;
		vid = true;
	} else {
		table = av_buf_gfx_fmts;
		size = ARRAY_SIZE(av_buf_gfx_fmts);
		mask = ~XILINX_DP_SUB_AV_BUF_FORMAT_NL_GFX_MASK;
		vid = false;
	}

	fmt = xilinx_drm_dp_sub_map_fmt(table, size, drm_fmt);
	if (!fmt) {
		spin_unlock_irqrestore(&dp_sub->lock, flags);
		return -EINVAL;
	}

	fmts = xilinx_drm_dp_sub_av_buf_get_fmt(&dp_sub->av_buf);
	fmts &= mask;
	fmts |= fmt->dp_sub_fmt;
	xilinx_drm_dp_sub_av_buf_set_fmt(&dp_sub->av_buf, fmts);

	layer->fmt = fmt;

	spin_unlock_irqrestore(&dp_sub->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_layer_set_fmt);

/**
 * xilinx_drm_dp_sub_get_fmt - Get the format of the layer
 * @dp_sub: DP subsystem
 * @layer: layer to set the format
 *
 * Get the format of the given layer.
 *
 * Return: DRM format of the layer
 */
uint32_t xilinx_drm_dp_sub_layer_get_fmt(struct xilinx_drm_dp_sub *dp_sub,
					 struct xilinx_drm_dp_sub_layer *layer)
{
	return layer->fmt->drm_fmt;
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_layer_get_fmt);

/**
 * xilinx_drm_dp_sub_layer_enable - Enable the layer
 * @dp_sub: DP subsystem
 * @layer: layer to esable
 *
 * Enable the layer @layer.
 */
void xilinx_drm_dp_sub_layer_enable(struct xilinx_drm_dp_sub *dp_sub,
				    struct xilinx_drm_dp_sub_layer *layer)
{
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);
	xilinx_drm_dp_sub_av_buf_enable_vid(&dp_sub->av_buf, layer);
	xilinx_drm_dp_sub_blend_layer_enable(&dp_sub->blend, layer,
					     !layer->other->enabled);
	if (layer->other->enabled)
		xilinx_drm_dp_sub_blend_layer_enable(&dp_sub->blend,
						     layer->other, false);
	layer->enabled = true;
	spin_unlock_irqrestore(&dp_sub->lock, flags);
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_layer_enable);

/**
 * xilinx_drm_dp_sub_layer_enable - Disable the layer
 * @dp_sub: DP subsystem
 * @layer: layer to disable
 *
 * Disable the layer @layer.
 */
void xilinx_drm_dp_sub_layer_disable(struct xilinx_drm_dp_sub *dp_sub,
				     struct xilinx_drm_dp_sub_layer *layer)
{
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);
	xilinx_drm_dp_sub_av_buf_disable_vid(&dp_sub->av_buf, layer);
	xilinx_drm_dp_sub_blend_layer_disable(&dp_sub->blend, layer);
	if (layer->other->enabled)
		xilinx_drm_dp_sub_blend_layer_enable(&dp_sub->blend,
						     layer->other, true);
	layer->enabled = false;
	spin_unlock_irqrestore(&dp_sub->lock, flags);
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_layer_disable);

/**
 * xilinx_drm_dp_sub_layer_get - Get the DP subsystem layer
 * @dp_sub: DP subsystem
 * @primary: flag to indicate the primary plane
 *
 * Check if there's any available layer based on the flag @primary, and return
 * the found layer.
 *
 * Return: a DP subsystem layer on success, or -ENODEV error pointer.
 */
struct xilinx_drm_dp_sub_layer *
xilinx_drm_dp_sub_layer_get(struct xilinx_drm_dp_sub *dp_sub, bool primary)
{
	struct xilinx_drm_dp_sub_layer *layer = NULL;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&dp_sub->lock, flags);

	for (i = 0; i < XILINX_DRM_DP_SUB_NUM_LAYERS; i++) {
		if (dp_sub->layers[i].primary == primary) {
			layer = &dp_sub->layers[i];
			break;
		}
	}

	if (!layer || !layer->avail) {
		spin_unlock_irqrestore(&dp_sub->lock, flags);
		return ERR_PTR(-ENODEV);
	}

	spin_unlock_irqrestore(&dp_sub->lock, flags);

	return layer;

}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_layer_get);

/**
 * xilinx_drm_dp_sub_layer_get - Put the DP subsystem layer
 * @dp_sub: DP subsystem
 * @layer: DP subsystem layer
 *
 * Return the DP subsystem layer @layer when it's no longer used.
 */
void xilinx_drm_dp_sub_layer_put(struct xilinx_drm_dp_sub *dp_sub,
				 struct xilinx_drm_dp_sub_layer *layer)
{
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);
	layer->avail = true;
	spin_unlock_irqrestore(&dp_sub->lock, flags);
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_layer_put);

/* DP subsystem functions */

/**
 * xilinx_drm_dp_sub_set_output_fmt - Set the output format
 * @dp_sub: DP subsystem
 * @drm_fmt: DRM format to set
 *
 * Set the output format of the DP subsystem. The flag @primary indicates that
 * which layer to configure.
 *
 * Return: 0 on success, or -EINVAL if @drm_fmt is not supported for output.
 */
int xilinx_drm_dp_sub_set_output_fmt(struct xilinx_drm_dp_sub *dp_sub,
				     uint32_t drm_fmt)
{
	const struct xilinx_drm_dp_sub_fmt *fmt;
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);

	fmt = xilinx_drm_dp_sub_map_fmt(blend_output_fmts,
					ARRAY_SIZE(blend_output_fmts), drm_fmt);
	if (!fmt) {
		spin_unlock_irqrestore(&dp_sub->lock, flags);
		return -EINVAL;
	}

	xilinx_drm_dp_sub_blend_set_output_fmt(&dp_sub->blend, fmt->dp_sub_fmt);

	spin_unlock_irqrestore(&dp_sub->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_set_output_fmt);

/**
 * xilinx_drm_dp_sub_set_bg_color - Set the background color
 * @dp_sub: DP subsystem
 * @c0: color component 0
 * @c1: color component 1
 * @c2: color component 2
 *
 * Set the background color with given color components (@c0, @c1, @c2).
 */
void xilinx_drm_dp_sub_set_bg_color(struct xilinx_drm_dp_sub *dp_sub,
				    u32 c0, u32 c1, u32 c2)
{
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);
	xilinx_drm_dp_sub_blend_set_bg_color(&dp_sub->blend, c0, c1, c2);
	spin_unlock_irqrestore(&dp_sub->lock, flags);
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_set_bg_color);

/**
 * xilinx_drm_dp_sub_set_alpha - Set the alpha value
 * @dp_sub: DP subsystem
 * @alpha: alpha value to set
 *
 * Set the alpha value for blending.
 */
void xilinx_drm_dp_sub_set_alpha(struct xilinx_drm_dp_sub *dp_sub, u32 alpha)
{
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);
	xilinx_drm_dp_sub_blend_set_alpha(&dp_sub->blend, true, alpha);
	spin_unlock_irqrestore(&dp_sub->lock, flags);
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_set_alpha);

/**
 * xilinx_drm_dp_sub_handle_vblank - Vblank handling wrapper
 * @dp_sub: DP subsystem
 *
 * Trigger the registered vblank handler. This function is supposed to be
 * called in the actual vblank handler.
 */
void xilinx_drm_dp_sub_handle_vblank(struct xilinx_drm_dp_sub *dp_sub)
{
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);
	if (dp_sub->vblank_fn)
		dp_sub->vblank_fn(dp_sub->vblank_data);
	spin_unlock_irqrestore(&dp_sub->lock, flags);
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_handle_vblank);

/**
 * xilinx_drm_dp_sub_enable_vblank - Enable the vblank handling
 * @dp_sub: DP subsystem
 * @vblank_fn: callback to be called on vblank event
 * @vblank_data: data to be used in @vblank_fn
 *
 * This function register the vblank handler, and the handler will be triggered
 * on vblank event after.
 */
void xilinx_drm_dp_sub_enable_vblank(struct xilinx_drm_dp_sub *dp_sub,
				     void (*vblank_fn)(void *),
				     void *vblank_data)
{
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);
	dp_sub->vblank_fn = vblank_fn;
	dp_sub->vblank_data = vblank_data;
	spin_unlock_irqrestore(&dp_sub->lock, flags);
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_enable_vblank);

/**
 * xilinx_drm_dp_sub_disable_vblank - Disable the vblank handling
 * @dp_sub: DP subsystem
 *
 * Disable the vblank handler. The vblank handler and data are unregistered.
 */
void xilinx_drm_dp_sub_disable_vblank(struct xilinx_drm_dp_sub *dp_sub)
{
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);
	dp_sub->vblank_fn = NULL;
	dp_sub->vblank_data = NULL;
	spin_unlock_irqrestore(&dp_sub->lock, flags);
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_disable_vblank);

/**
 * xilinx_drm_dp_sub_enable - Enable the DP subsystem
 * @dp_sub: DP subsystem
 *
 * Enable the DP subsystem.
 */
void xilinx_drm_dp_sub_enable(struct xilinx_drm_dp_sub *dp_sub)
{
	const struct xilinx_drm_dp_sub_fmt *vid_fmt;
	const struct xilinx_drm_dp_sub_fmt *gfx_fmt;
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);
	vid_fmt = dp_sub->layers[XILINX_DRM_DP_SUB_LAYER_VID].fmt;
	gfx_fmt = dp_sub->layers[XILINX_DRM_DP_SUB_LAYER_GFX].fmt;
	xilinx_drm_dp_sub_av_buf_init_fmts(&dp_sub->av_buf, vid_fmt, gfx_fmt);
	xilinx_drm_dp_sub_av_buf_init_sf(&dp_sub->av_buf, vid_fmt, gfx_fmt);
	xilinx_drm_dp_sub_av_buf_set_vid_clock_src(&dp_sub->av_buf, true);
	xilinx_drm_dp_sub_av_buf_set_vid_timing_src(&dp_sub->av_buf, true);
	xilinx_drm_dp_sub_av_buf_set_aud_clock_src(&dp_sub->av_buf, true);
	xilinx_drm_dp_sub_av_buf_enable_buf(&dp_sub->av_buf);
	xilinx_drm_dp_sub_av_buf_enable_aud(&dp_sub->av_buf);
	xilinx_drm_dp_sub_aud_init(&dp_sub->aud);
	spin_unlock_irqrestore(&dp_sub->lock, flags);
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_enable);

/**
 * xilinx_drm_dp_sub_enable - Disable the DP subsystem
 * @dp_sub: DP subsystem
 *
 * Disable the DP subsystem.
 */
void xilinx_drm_dp_sub_disable(struct xilinx_drm_dp_sub *dp_sub)
{
	unsigned long flags;

	spin_lock_irqsave(&dp_sub->lock, flags);
	xilinx_drm_dp_sub_av_buf_disable_aud(&dp_sub->av_buf);
	xilinx_drm_dp_sub_av_buf_disable_buf(&dp_sub->av_buf);
	spin_unlock_irqrestore(&dp_sub->lock, flags);
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_disable);

/* DP subsystem initialization functions */

/**
 * xilinx_drm_dp_sub_of_get - Get the DP subsystem instance
 * @np: parent device node
 *
 * This function searches and returns a DP subsystem structure for
 * the parent device node, @np. The DP subsystem node should be a child node of
 * @np, with 'xlnx,dp-sub' property pointing to the DP device node. An instance
 * can be shared by multiple users.
 *
 * Return: corresponding DP subsystem structure if found. NULL if
 * the device node doesn't have 'xlnx,dp-sub' property, or -EPROBE_DEFER error
 * pointer if the the DP subsystem isn't found.
 */
struct xilinx_drm_dp_sub *xilinx_drm_dp_sub_of_get(struct device_node *np)
{
	struct device_node *xilinx_drm_dp_sub_node;
	struct xilinx_drm_dp_sub *found = NULL;
	struct xilinx_drm_dp_sub *dp_sub;

	if (!of_find_property(np, "xlnx,dp-sub", NULL))
		return NULL;

	xilinx_drm_dp_sub_node = of_parse_phandle(np, "xlnx,dp-sub", 0);
	if (xilinx_drm_dp_sub_node == NULL)
		return ERR_PTR(-EINVAL);

	mutex_lock(&xilinx_drm_dp_sub_lock);
	list_for_each_entry(dp_sub, &xilinx_drm_dp_sub_list, list) {
		if (dp_sub->dev->of_node == xilinx_drm_dp_sub_node) {
			found = dp_sub;
			break;
		}
	}
	mutex_unlock(&xilinx_drm_dp_sub_lock);

	of_node_put(xilinx_drm_dp_sub_node);

	if (!found)
		return ERR_PTR(-EPROBE_DEFER);

	return found;
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_of_get);

/**
 * xilinx_drm_dp_sub_put - Put the DP subsystem instance
 * @dp_sub: DP subsystem
 *
 * Put the DP subsystem instance @dp_sub.
 */
void xilinx_drm_dp_sub_put(struct xilinx_drm_dp_sub *dp_sub)
{
	/* no-op */
}
EXPORT_SYMBOL_GPL(xilinx_drm_dp_sub_put);

/**
 * xilinx_drm_dp_register_device - Register the DP subsystem to the global list
 * @dp_sub: DP subsystem
 *
 * Register the DP subsystem instance to the global list
 */
static void xilinx_drm_dp_sub_register_device(struct xilinx_drm_dp_sub *dp_sub)
{
	mutex_lock(&xilinx_drm_dp_sub_lock);
	list_add_tail(&dp_sub->list, &xilinx_drm_dp_sub_list);
	mutex_unlock(&xilinx_drm_dp_sub_lock);
}

/**
 * xilinx_drm_dp_register_device - Unregister the DP subsystem instance
 * @dp_sub: DP subsystem
 *
 * Unregister the DP subsystem instance from the global list
 */
static void
xilinx_drm_dp_sub_unregister_device(struct xilinx_drm_dp_sub *dp_sub)
{
	mutex_lock(&xilinx_drm_dp_sub_lock);
	list_del(&dp_sub->list);
	mutex_unlock(&xilinx_drm_dp_sub_lock);
}

/**
 * xilinx_drm_dp_sub_parse_of - Parse the DP subsystem device tree node
 * @dp_sub: DP subsystem
 *
 * Parse the DP subsystem device tree node.
 *
 * Return: 0 on success, or the corresponding error code.
 */
static int xilinx_drm_dp_sub_parse_of(struct xilinx_drm_dp_sub *dp_sub)
{
	struct device_node *node = dp_sub->dev->of_node;
	const char *string;
	u32 fmt;
	bool ret;

	ret = of_property_read_string(node, "xlnx,output-fmt", &string);
	if (ret < 0) {
		dev_err(dp_sub->dev, "No colormetry in DT\n");
		return ret;
	}

	if (strcmp(string, "rgb") == 0) {
		fmt = XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_RGB;
	} else if (strcmp(string, "ycrcb444") == 0) {
		fmt = XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_YCBCR444;
	} else if (strcmp(string, "ycrcb422") == 0) {
		fmt = XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_YCBCR422;
	} else if (strcmp(string, "yonly") == 0) {
		fmt = XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT_YONLY;
	} else {
		dev_err(dp_sub->dev, "Invalid output format in DT\n");
		return -EINVAL;
	}

	xilinx_drm_writel(dp_sub->blend.base,
			  XILINX_DP_SUB_V_BLEND_OUTPUT_VID_FMT, fmt);

	ret = of_property_read_bool(node, "xlnx,vid-primary");
	if (ret)
		dp_sub->layers[XILINX_DRM_DP_SUB_LAYER_VID].primary = true;
	else
		dp_sub->layers[XILINX_DRM_DP_SUB_LAYER_GFX].primary = true;

	return 0;
}

static int xilinx_drm_dp_sub_probe(struct platform_device *pdev)
{
	struct xilinx_drm_dp_sub *dp_sub;
	struct resource *res;
	int ret;

	dp_sub = devm_kzalloc(&pdev->dev, sizeof(*dp_sub), GFP_KERNEL);
	if (!dp_sub)
		return -ENOMEM;

	dp_sub->dev = &pdev->dev;

	spin_lock_init(&dp_sub->lock);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "blend");
	dp_sub->blend.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dp_sub->blend.base))
		return PTR_ERR(dp_sub->blend.base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "av_buf");
	dp_sub->av_buf.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dp_sub->av_buf.base))
		return PTR_ERR(dp_sub->av_buf.base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "aud");
	dp_sub->aud.base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dp_sub->aud.base))
		return PTR_ERR(dp_sub->aud.base);

	dp_sub->layers[0].id = XILINX_DRM_DP_SUB_LAYER_VID;
	dp_sub->layers[0].offset = 0;
	dp_sub->layers[0].avail = true;
	dp_sub->layers[0].fmt = &av_buf_vid_fmts[AV_BUF_DEFAULT_VID_FMT_ID];
	dp_sub->layers[0].other = &dp_sub->layers[1];

	dp_sub->layers[1].id = XILINX_DRM_DP_SUB_LAYER_GFX;
	dp_sub->layers[1].offset = 4;
	dp_sub->layers[1].avail = true;
	dp_sub->layers[1].fmt = &av_buf_gfx_fmts[AV_BUF_DEFAULT_GFX_FMT_ID];
	dp_sub->layers[1].other = &dp_sub->layers[0];

	ret = xilinx_drm_dp_sub_parse_of(dp_sub);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, dp_sub);

	xilinx_drm_dp_sub_register_device(dp_sub);

	dev_info(dp_sub->dev, "Xilinx DisplayPort Subsystem is probed\n");

	return 0;
}

static int xilinx_drm_dp_sub_remove(struct platform_device *pdev)
{
	struct xilinx_drm_dp_sub *dp_sub = platform_get_drvdata(pdev);

	xilinx_drm_dp_sub_unregister_device(dp_sub);

	return 0;
}

static const struct of_device_id xilinx_drm_dp_sub_of_id_table[] = {
	{ .compatible = "xlnx,dp-sub" },
	{ }
};
MODULE_DEVICE_TABLE(of, xilinx_drm_dp_sub_of_id_table);

static struct platform_driver xilinx_drm_dp_sub_driver = {
	.driver = {
		.name		= "xilinx-drm-dp-sub",
		.of_match_table = xilinx_drm_dp_sub_of_id_table,
	},
	.probe	= xilinx_drm_dp_sub_probe,
	.remove	= xilinx_drm_dp_sub_remove,
};

module_platform_driver(xilinx_drm_dp_sub_driver);

MODULE_DESCRIPTION("Xilinx DisplayPort Subsystem Driver");
MODULE_LICENSE("GPL v2");
