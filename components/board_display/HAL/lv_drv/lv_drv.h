#ifndef _LV_DRV_H_
#define _LV_DRV_H_


#include "esp_lvgl_port.h"
#include "config.h"
#include "esp_lcd_dsi/esp_lcd_dsi.h"
#include "esp_log.h"
#include "I2C/I2c_drv.h"
#include "../PCA9536/pca9536.h"

/* LCD display color bytes endianess */
#define BSP_LCD_BIGENDIAN           (0)
/**************************************************************************************************
 *
 * LCD interface
 *
 * ESP-BOX is shipped with 2.4inch ST7789 display controller.
 * It features 16-bit colors, 320x240 resolution and capacitive touch controller.
 *
 * LVGL is used as graphics library. LVGL is NOT thread safe, therefore the user must take LVGL mutex
 * by calling bsp_display_lock() before calling and LVGL API (lv_...) and then give the mutex with
 * bsp_display_unlock().
 *
 * Display's backlight must be enabled explicitly by calling bsp_display_backlight_on()
 **************************************************************************************************/
#define BSP_LCD_PIXEL_CLOCK_MHZ     (80)

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)

#define BSP_LCD_DRAW_BUFF_SIZE     (MIPI_DSI_LCD_H_RES * 50) // Frame buffer size in pixels
#define BSP_LCD_DRAW_BUFF_DOUBLE   (0)



/**
 * @brief BSP display configuration structure
 *
 */
typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;  /*!< LVGL port configuration */
    uint32_t        buffer_size;    /*!< Size of the buffer for the screen in pixels */
    bool            double_buffer;  /*!< True, if should be allocated two buffers */
    struct {
        unsigned int buff_dma: 1;    /*!< Allocated LVGL buffer will be DMA capable */
        unsigned int buff_spiram: 1; /*!< Allocated LVGL buffer will be in PSRAM */
        unsigned int sw_rotate: 1;   /*!< Use software rotation (slower), The feature is unavailable under avoid-tear mode */
    } flags;
} bsp_display_cfg_t;

lv_display_t *lvgl_display_port_init(const bsp_display_cfg_t *cfg);
lv_display_t *lvgl_port_init_with_display_init(const bsp_display_cfg_t *cfg);
#endif  // _LV_DRV_H_
#endif  // _LV_DRV_H_
