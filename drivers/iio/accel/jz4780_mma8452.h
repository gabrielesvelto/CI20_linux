#ifndef  __MMA8452_H__
#define  __MMA8452_H__

#include <linux/gpio.h>
#include <linux/regulator/consumer.h>

#define SENSOR_TYPE_ACCELEROMETER	1

#define	SENSORIO	0x4c

#define	SENSOR_IOCTL_SET_DELAY		_IOW(SENSORIO, 0x11, short)
#define	SENSOR_IOCTL_SET_ACTIVE		_IOW(SENSORIO, 0x13, short)
#define	SENSOR_IOCTL_GET_DATA		_IOR(SENSORIO, 0x15, short)

#define	SENSOR_IOCTL_GET_DELAY		_IOR(SENSORIO, 0x12, short)
#define	SENSOR_IOCTL_GET_ACTIVE		_IOR(SENSORIO, 0x14, short)

#define	SENSOR_IOCTL_GET_DATA_TYPE		_IOR(SENSORIO, 0x16, short)
#define	SENSOR_IOCTL_GET_DATA_MAXRANGE		_IOR(SENSORIO, 0x17, short)
#define	SENSOR_IOCTL_GET_DATA_NAME		_IOR(SENSORIO, 0x18, short)
#define	SENSOR_IOCTL_GET_DATA_POWER		_IOR(SENSORIO, 0x19, short)
#define	SENSOR_IOCTL_GET_DATA_RESOLUTION	_IOR(SENSORIO, 0x20, short)
#define	SENSOR_IOCTL_GET_DATA_VERSION		_IOR(SENSORIO, 0x21, short)
#define	SENSOR_IOCTL_WAKE			_IOR(SENSORIO, 0x99, short)

#define MMA8452_ODR1			0x38	/* 1.56Hz output data rate */
#define MMA8452_ODR6			0x30	/* 6.25Hz output data rate */
#define MMA8452_ODR12			0x28	/* 12.5Hz output data rate */
#define MMA8452_ODR50			0x20	/* 50Hz output data rate */
#define MMA8452_ODR100			0x18	/* 100Hz output data rate */
#define MMA8452_ODR200			0x10	/* 200Hz output data rate */
#define MMA8452_ODR400			0x08	/* 400Hz output data rate */
#define MMA8452_ODR800			0x00	/* 800Hz output data rate */

__attribute__((weak)) struct gsensor_platform_data {
	int gpio_int;

	int poll_interval;
	int min_interval;
	int max_interval;

	u8 g_range;

	u8 axis_map_x;
	u8 axis_map_y;
	u8 axis_map_z;

	u8 negate_x;
	u8 negate_y;
	u8 negate_z;

	int (*init)(void);
	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);
};

enum {
	GSENSOR_2G = 0,
	GSENSOR_4G,
	GSENSOR_8G,
	GSENSOR_16G,
};

struct linux_sensor_t {
	/* name of this sensors */
	char	 name[100];
	/* vendor of the hardware part */
	char	 vendor[100];

	int type;
	/*
	 * version of the hardware part + driver. The value of this field is
	 * left to the implementation and doesn't have to be monotonicaly
	 * increasing.
	 */
	int			 version;
	/* maximaum range of this sensor's value in SI units */
	int		maxRange;
	/* smallest difference between two values reported by this sensor */
	int		resolution;
	/* rough estimate of this sensor's power consumption in mA */
	int		power;
	/* reserved fields, must be zero */
	void*		reserved[9];
};

/* register enum for mma8452 registers */
enum {
	MMA8452_STATUS = 0x00,
	MMA8452_OUT_X_MSB,
	MMA8452_OUT_X_LSB,
	MMA8452_OUT_Y_MSB,
	MMA8452_OUT_Y_LSB,
	MMA8452_OUT_Z_MSB,
	MMA8452_OUT_Z_LSB,

	MMA8452_SYSMOD = 0x0B,
	MMA8452_INT_SOURCE,
	MMA8452_WHO_AM_I,
	MMA8452_XYZ_DATA_CFG,
	MMA8452_HP_FILTER_CUTOFF,

	MMA8452_PL_STATUS,
	MMA8452_PL_CFG,
	MMA8452_PL_COUNT,
	MMA8452_PL_BF_ZCOMP,
	MMA8452_PL_P_L_THS_REG,

	MMA8452_FF_MT_CFG,
	MMA8452_FF_MT_SRC,
	MMA8452_FF_MT_THS,
	MMA8452_FF_MT_COUNT,

	MMA8452_TRANSIENT_CFG = 0x1D,
	MMA8452_TRANSIENT_SRC,
	MMA8452_TRANSIENT_THS,
	MMA8452_TRANSIENT_COUNT,

	MMA8452_PULSE_CFG,
	MMA8452_PULSE_SRC,
	MMA842_PULSE_THSX,
	MMA8452_PULSE_THSY,
	MMA8452_PULSE_THSZ,
	MMA8452_PULSE_TMLT,
	MMA8452_PULSE_LTCY,
	MMA8452_PULSE_WIND,

	MMA8452_ASLP_COUNT,
	MMA8452_CTRL_REG1,
	MMA8452_CTRL_REG2,
	MMA8452_CTRL_REG3,
	MMA8452_CTRL_REG4,
	MMA8452_CTRL_REG5,

	MMA8452_OFF_X,
	MMA8452_OFF_Y,
	MMA8452_OFF_Z,

	MMA8452_REG_END,
};
#endif
