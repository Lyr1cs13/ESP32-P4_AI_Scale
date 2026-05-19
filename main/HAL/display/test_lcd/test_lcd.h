
#ifndef __TEST_LCD_H_
#define __TEST_LCD_H_
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "../../esp_lcd_dsi/esp_lcd_dsi.h"



// Refresh Rate = 48000000/(10+120+120+1024)/(1+20+10+600) = 60Hz
// #define TEST_MIPI_DSI_DPI_CLK_MHZ  60
// #define TEST_MIPI_DSI_LCD_H_RES    568
// #define TEST_MIPI_DSI_LCD_V_RES    1232
// #define TEST_MIPI_DSI_LCD_HSYNC    50
// #define TEST_MIPI_DSI_LCD_HBP      150
// #define TEST_MIPI_DSI_LCD_HFP      50
// #define TEST_MIPI_DSI_LCD_VSYNC    40
// #define TEST_MIPI_DSI_LCD_VBP      120
// #define TEST_MIPI_DSI_LCD_VFP      80

// #define TEST_MIPI_DSI_DPI_CLK_MHZ  25
#define TEST_MIPI_DSI_LCD_H_RES    480
#define TEST_MIPI_DSI_LCD_V_RES    640
// #define TEST_MIPI_DSI_LCD_HSYNC    4    // HSync脉宽
// #define TEST_MIPI_DSI_LCD_HBP      480+20   // HBP
// #define TEST_MIPI_DSI_LCD_HFP      10   // HFP
// #define TEST_MIPI_DSI_LCD_VSYNC    4    // VSync脉宽
// #define TEST_MIPI_DSI_LCD_VBP      14   // VBP
// #define TEST_MIPI_DSI_LCD_VFP      8    // VFP
// 3.97 inch st7701
#define TEST_MIPI_DSI_DPI_CLK_MHZ  30
// #define TEST_MIPI_DSI_LCD_H_RES    480
// #define TEST_MIPI_DSI_LCD_V_RES    800
// #define TEST_MIPI_DSI_LCD_H_RES    480
// #define TEST_MIPI_DSI_LCD_V_RES    640

// 时序参数 (来自全志配置的精确计算值)
#define TEST_MIPI_DSI_LCD_HSYNC    2
#define TEST_MIPI_DSI_LCD_HBP      40 
#define TEST_MIPI_DSI_LCD_HFP      36
#define TEST_MIPI_DSI_LCD_VSYNC    10
#define TEST_MIPI_DSI_LCD_VBP      52
#define TEST_MIPI_DSI_LCD_VFP      48


#define TEST_MIPI_DSI_LANE_NUM          2  // 2 data lanes
#define TEST_MIPI_DSI_LANE_BITRATE_MBPS 444 // 

#define TEST_PIN_NUM_LCD_RST            -1 // LCD背光控制引脚
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your Board Design //////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The "VDD_MIPI_DPHY" should be supplied with 2.5V, it can source from the internal LDO regulator or from external LDO chip



#endif // __TEST_LCD_H_