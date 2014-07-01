/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DWC_HDMI_H__
#define __DWC_HDMI_H__

#define HDMI_EDID_LEN		512


struct hdmi_vmode {
	bool mdvi;
	bool mhsyncpolarity;
	bool mvsyncpolarity;
	bool minterlaced;
	bool mdataenablepolarity;

	unsigned int mpixelclock;
	unsigned int mpixelrepetitioninput;
	unsigned int mpixelrepetitionoutput;
};

struct hdmi_data_info {
	unsigned int enc_in_format;
	unsigned int enc_out_format;
	unsigned int enc_color_depth;
	unsigned int colorimetry;
	unsigned int pix_repet_factor;
	unsigned int hdcp_enable;
	struct hdmi_vmode video_mode;
};

enum hdmi_datamap {
	RGB444_8B = 0x01,
	RGB444_10B = 0x03,
	RGB444_12B = 0x05,
	RGB444_16B = 0x07,
	YCbCr444_8B = 0x09,
	YCbCr444_10B = 0x0B,
	YCbCr444_12B = 0x0D,
	YCbCr444_16B = 0x0F,
	YCbCr422_8B = 0x16,
	YCbCr422_10B = 0x14,
	YCbCr422_12B = 0x12,
};

enum dwc_hdmi_devtype {
	IMX6Q_HDMI,
	IMX6DL_HDMI,
	DWC_HDMI,
};

struct dwc_hdmi {
	struct drm_connector connector;
	struct dwc_drm_connector *dwc_drm_connector;
	struct drm_encoder encoder;
	struct dwc_drm_encoder *dwc_drm_encoder;

	struct drm_device *drm;

	enum dwc_hdmi_devtype dev_type;
	struct device *dev;
	struct clk *isfr_clk;
	struct clk *iahb_clk;

	struct hdmi_data_info hdmi_data;
	int vic;

	u8 edid[HDMI_EDID_LEN];
	bool cable_plugin;

	bool phy_enabled;
	struct drm_display_mode previous_mode;

	struct regmap *regmap;
	struct i2c_adapter *ddc;
	void __iomem *regs;
	int reg_shift;

	unsigned long pixel_clk_rate;
	unsigned int sample_rate;
	int ratio;

};

void dwc_hdmi_register(struct dwc_hdmi *hdmi, struct drm_device *dev);

#endif /* __DWC_HDMI_H__ */
