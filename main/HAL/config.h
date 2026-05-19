#ifndef _CONFIG_H_
#define _CONFIG_H_
#include "esp_check.h"
#include "esp_log.h"
#include "esp_err.h"
#include <assert.h>

#define HAL_NULL_CHECK(x, ret)           assert(x)

/* I2C */
#define BSP_I2C_NUM             I2C_NUM_0
#define BSP_I2C_SCL           (GPIO_NUM_8)
#define BSP_I2C_SDA           (GPIO_NUM_7)
#define CONFIG_BSP_I2C_CLK_SPEED_HZ 400000
// /* Audio */
// #define BSP_I2S_SCLK          (GPIO_NUM_12)
// #define BSP_I2S_MCLK          (GPIO_NUM_13)
// #define BSP_I2S_LCLK          (GPIO_NUM_10)
// #define BSP_I2S_DOUT          (GPIO_NUM_9)
// #define BSP_I2S_DSIN          (GPIO_NUM_11)
// #define BSP_POWER_AMP_IO      (GPIO_NUM_53)

#define BSP_LCD_BACKLIGHT     (GPIO_NUM_NC)
#define BSP_LCD_RST           (GPIO_NUM_NC)
#define BSP_LCD_TOUCH_RST     (GPIO_NUM_NC)
#define BSP_LCD_TOUCH_INT     (GPIO_NUM_NC)

// /* uSD card */
// #define BSP_SD_D0             (GPIO_NUM_39)
// #define BSP_SD_D1             (GPIO_NUM_40)
// #define BSP_SD_D2             (GPIO_NUM_41)
// #define BSP_SD_D3             (GPIO_NUM_42)
// #define BSP_SD_CMD            (GPIO_NUM_44)
// #define BSP_SD_CLK            (GPIO_NUM_43)

#endif //_CONFIG_H_