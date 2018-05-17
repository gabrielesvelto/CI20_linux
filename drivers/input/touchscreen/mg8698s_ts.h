#ifndef __LINUX_MG8698S_TS_H__
#define __LINUX_MG8698S_TS_H__

#define COORD_INTERPRET(MSB_BYTE, LSB_BYTE) \
                (MSB_BYTE << 8 | LSB_BYTE)

#define CFG_MAX_X	1024
#define CFG_MAX_Y	768
/* -- dirver configure -- */
#define CFG_MAX_TOUCH_POINTS	5


#define PRESS_MAX	0xFF
#define FT_PRESS	0x7F

#define MG8698S_NAME	"mg8698s_tsc"

#define FT_MAX_ID	0x0F
#define FT_TOUCH_STEP	6
#define FT_TOUCH_X_H_POS		3
#define FT_TOUCH_X_L_POS		4
#define FT_TOUCH_Y_H_POS		5
#define FT_TOUCH_Y_L_POS		6
#define FT_TOUCH_EVENT_POS		3
#define FT_TOUCH_ID_POS			5
#define FT_TOUCH_WEIGHT			7

#define POINT_READ_BUF	30

enum commds
{
        MG_CALIBRATION = 0,
        MG_TOUCH_ENABLE,
        MG_TOUCH_DISABLE,
        MG_DEVICE_ID,
        MG_DEVICE_VERSION,
        MG_SLEEP,
        MG_IDLE,
        MG_WAKEUP,
        MG_CALIBRATION_SUCCESS,
        MG_JUMP_BOOTLOADER_OK,
        MG_JUMP_BOOTLOADER,
        MG_ACTION123_OK,
        MG_ACTION2,
        MG_ENDING,
        MG_GET_CHECK_SUM,
        MG_NONE,
};

#define COMMAND_COUSE           15
#define COMMAND_BYTES           5

enum mg_capac_report {

        MG_MODE = 0x0,
        TOUCH_KEY_CODE,
        ACTUAL_TOUCH_POINTS,

        MG_CONTACT_ID1,
        MG_STATUS1,
        MG_POS_X1_LOW,
        MG_POS_X1_HI,
        MG_POS_Y1_LOW,
        MG_POS_Y1_HI,

        MG_CONTACT_ID2,
        MG_STATUS2,
        MG_POS_X2_LOW,
        MG_POS_X2_HI,
        MG_POS_Y2_LOW,
        MG_POS_Y2_HI,

        MG_CONTACT_IDS3,
        MG_POS_X3_LOW,
        MG_POS_X3_HI,
        MG_POS_Y3_LOW,
        MG_POS_Y3_HI,

        MG_CONTACT_IDS4,
        MG_POS_X4_LOW,
        MG_POS_X4_HI,
        MG_POS_Y4_LOW,
        MG_POS_Y4_HI,

        MG_CONTACT_IDS5,
        MG_POS_X5_LOW,
        MG_POS_X5_HI,
        MG_POS_Y5_LOW,
        MG_POS_Y5_HI,
};

enum mg_mode {
    MG_MODE_TOUCH = 0x01,
    MG_MODE_KEY = 0x02,
};

#endif
