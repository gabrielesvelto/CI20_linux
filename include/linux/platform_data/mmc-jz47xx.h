#ifndef __LINUX_PLATFORM_DATA_MMC_JZ47XX_H
#define __LINUX_PLATFORM_DATA_MMC_JZ47XX_H

struct jz47xx_mmc_platform_data {
	int gpio_power;
	int gpio_card_detect;
	int gpio_read_only;
	unsigned card_detect_active_low:1;
	unsigned read_only_active_low:1;
	unsigned power_active_low:1;

	unsigned int bus_width;
	unsigned int max_freq;
};

#endif

