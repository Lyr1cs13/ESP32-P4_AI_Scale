#include "lv_drv.h"
#include "../touch/touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch_cst816s.h"
#include "../esp_lcd_dsi/esp_lcd_dsi.h"
static const char *TAG = "hal/lv_drv";

static lv_indev_t *disp_indev = NULL;


lv_display_t *lvgl_display_port_init(const bsp_display_cfg_t *cfg)
{
    assert(cfg != NULL);
    esp_lcd_handles_t lcd_panels;
    // ESP_ERROR_CHECK(bsp_display_new_with_handles(NULL, &lcd_panels));
    esp_lcd_dsi_init(&lcd_panels);
    /* Add LCD screen */
    ESP_LOGI(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_panels.io,
        .panel_handle = lcd_panels.panel,
        .control_handle = lcd_panels.control,
        .buffer_size = cfg->buffer_size,
        .double_buffer = cfg->double_buffer,
        .hres = MIPI_DSI_LCD_H_RES,
        .vres = MIPI_DSI_LCD_V_RES,
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,  //tag
            .mirror_y = false, // tag
        },
#if LVGL_VERSION_MAJOR >= 9
#if CONFIG_DSI_LCD_COLOR_FORMAT_888
        .color_format = LV_COLOR_FORMAT_RGB888,
#else
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
#endif
        .flags = {
            .buff_dma = cfg->flags.buff_dma,
            .buff_spiram = cfg->flags.buff_spiram,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = (BSP_LCD_BIGENDIAN ? true : false),
#endif
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR
            .sw_rotate = false,                /* Avoid tearing is not supported for SW rotation */
#else
            .sw_rotate = cfg->flags.sw_rotate, /* Only SW rotation is supported for 90° and 270° */
#endif
#if CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH
            .full_refresh = true,
#elif CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE
            .direct_mode = true,
#endif
        }
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {
        .flags = {
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR
            .avoid_tearing = true,
#else
            .avoid_tearing = false,
#endif
        }
    };

    return lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
}


esp_err_t bsp_touch_new(const bsp_touch_config_t *config, esp_lcd_touch_handle_t *ret_touch)
{
    /* Initilize I2C */
    ESP_ERROR_CHECK(i2c_drv_init());

    /* Initialize touch */
    const esp_lcd_touch_config_t tp_cfg = {
#if CONFIG_BSP_LCD_TYPE_480_640_2_8_INCH || CONFIG_BSP_LCD_TYPE_480_800_4_INCH
        .x_max = BSP_LCD_V_RES,
        .y_max = BSP_LCD_H_RES,
#else
        .x_max = MIPI_DSI_LCD_H_RES,
        .y_max = MIPI_DSI_LCD_V_RES,
#endif
        .rst_gpio_num = BSP_LCD_TOUCH_RST, // Shared with LCD reset
        .int_gpio_num = BSP_LCD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
#if CONFIG_BSP_LCD_TYPE_800_1280_10_1_INCH || CONFIG_BSP_LCD_TYPE_800_1280_10_1_INCH_A || CONFIG_BSP_LCD_TYPE_720_1280_7_INCH_A
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1,
#elif CONFIG_BSP_LCD_TYPE_800_1280_8_INCH_A
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
#elif CONFIG_BSP_LCD_TYPE_480_640_2_8_INCH
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 1,
#elif CONFIG_BSP_LCD_TYPE_480_800_4_INCH
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 0,
#else
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1,
#endif
        },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
#if CONFIG_TOUCH_GT911    
    esp_lcd_panel_io_i2c_config_t tp_io_config = {
#if CONFIG_BSP_LCD_TYPE_800_1280_10_1_INCH || CONFIG_BSP_LCD_TYPE_800_1280_10_1_INCH_A || CONFIG_BSP_LCD_TYPE_800_1280_8_INCH_A || CONFIG_BSP_LCD_TYPE_720_1280_7_INCH_A || CONFIG_BSP_LCD_TYPE_720_1280_5_INCH_A
    .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
#else
    .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP,
#endif
    .control_phase_bytes = 1,
    .dc_bit_offset = 0,
    .lcd_cmd_bits = 16,
    .flags = {
        .disable_control_phase = 1,
        }
    };
    tp_io_config.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;
#else

    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();

#endif

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_drv_get_handle(), &tp_io_config, &tp_io_handle), TAG, "");
    return esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, ret_touch);
    // return esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, ret_touch);
}




static lv_indev_t *lvgl_touch_port_init(lv_display_t *disp)
{
    esp_lcd_touch_handle_t tp;
    ESP_ERROR_CHECK(bsp_touch_new(NULL, &tp));
    assert(tp);    

    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
    };

    return lvgl_port_add_touch(&touch_cfg);
}




lv_display_t *lvgl_port_init_with_display_init(const bsp_display_cfg_t *cfg)
{
    lv_display_t *disp;

    assert(cfg != NULL);
    ESP_ERROR_CHECK(lvgl_port_init(&cfg->lvgl_port_cfg));
    // i2c_drv_init();
   // ESP_ERROR_CHECK(pca9536_init());

    HAL_NULL_CHECK(disp = lvgl_display_port_init(cfg), NULL);
    
    HAL_NULL_CHECK(disp_indev = lvgl_touch_port_init(disp), NULL);

    return disp;
}




