/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "soc/soc_caps.h"

#if SOC_MIPI_DSI_SUPPORTED
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"


#define MIPI_DSI_PHY_PWR_LDO_CHAN       3  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV  2500 //2500

// This is the level to turn on/off the LCD backlight, please refer to your LCD module 
#define LCD_BK_LIGHT_ON_LEVEL           1
#define LCD_BK_LIGHT_OFF_LEVEL          !LCD_BK_LIGHT_ON_LEVEL
#define PIN_NUM_BK_LIGHT                -1



#if CONFIG_DSI_LCD_COLOR_FORMAT_565
#define USER_COLOR_DEFINE LCD_COLOR_FMT_RGB565
#else
#define USER_COLOR_DEFINE LCD_COLOR_FMT_RGB888
#endif


#if CONFIG_INCH_2_8_LCD_ST7701S_480x640

#define MIPI_DSI_LCD_H_RES    480
#define MIPI_DSI_LCD_V_RES    640

#define esp_lcd_dsi_bus_config  ST7701_PANEL_BUS_DSI_1CH_CONFIG()
#define dsi_init_cmd            WLK280_init_cmd
#define dsi_panel(x)               ST7701_640_480_PANEL_25HZ_CONFIG(x)
#endif

#if CONFIG_INCH_2_4_LCD_ST7701S_480x640

#define MIPI_DSI_LCD_H_RES    480
#define MIPI_DSI_LCD_V_RES    640

#define esp_lcd_dsi_bus_config  ST7701_PANEL_BUS_DSI_1CH_CONFIG()
#define dsi_init_cmd            WLK240_init_cmd
#define dsi_panel(x)               ST7701_640_480_PANEL_25HZ_CONFIG(x)
#endif

#if CONFIG_INCH_4_0_LCD_RM69A10_568_1232

#define MIPI_DSI_LCD_H_RES    568
#define MIPI_DSI_LCD_V_RES    1232

#define esp_lcd_dsi_bus_config  RM69A10_PANEL_BUS_DSI_2CH_CONFIG()
#define dsi_init_cmd            inch4_init_cmd
#define dsi_panel(x)            RM69A10_568_1232_PANEL_60HZ_CONFIG(x)
#endif


#if CONFIG_INCH_10_0_LCD_JD9365_800_1280

#define MIPI_DSI_LCD_H_RES    800
#define MIPI_DSI_LCD_V_RES    1280

#define esp_lcd_dsi_bus_config  JD9365_PANEL_BUS_DSI_2CH_CONFIG()
#define dsi_init_cmd            inch_10_init_cmd
#define dsi_panel(x)            jd9365_800_1280_PANEL_30HZ_CONFIG(x)
#endif



#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * @brief MIPI power function
     * 
     */



    void enable_dsi_phy_power(void);

    typedef struct {
    esp_lcd_dsi_bus_handle_t    mipi_dsi_bus;  /*!< MIPI DSI bus handle */
    esp_lcd_panel_io_handle_t   io;            /*!< ESP LCD IO handle */
    esp_lcd_panel_handle_t      panel;         /*!< ESP LCD panel (color) handle */
    esp_lcd_panel_handle_t      control;       /*!< ESP LCD panel (control) handle */
    } esp_lcd_handles_t;


    /**
     * @brief LCD panel initialization commands.
     *
     */
    typedef struct
    {
        int cmd;               /*<! The specific LCD command */
        const void *data;      /*<! Buffer that holds the command specific data */
        size_t data_bytes;     /*<! Size of `data` in memory, in bytes */
        unsigned int delay_ms; /*<! Delay in milliseconds after this command */
    } dsi_lcd_init_cmd_t;

    /**
     * @brief LCD panel vendor configuration.
     *
     * @note  This structure needs to be passed to the `vendor_config` field in `esp_lcd_panel_dev_config_t`.
     *
     */
    typedef struct
    {
        const dsi_lcd_init_cmd_t *init_cmds; /*!< Pointer to initialization commands array. Set to NULL if using default commands.
                                                  *   The array should be declared as `static const` and positioned outside the function.
                                                  *   Please refer to `vendor_specific_init_default` in source file.
                                                  */
        uint16_t init_cmds_size;                 /*<! Number of commands in above array */
        struct
        {
            esp_lcd_dsi_bus_handle_t dsi_bus;             /*!< MIPI-DSI bus configuration */
            const esp_lcd_dpi_panel_config_t *dpi_config; /*!< MIPI-DPI panel configuration */
            uint8_t lane_num;                             /*!< Number of MIPI-DSI lanes, defaults to 2 if set to 0 */
        } mipi_config;
    } dsi_vendor_config_t;

    /**
     * @brief Create LCD panel for model DSI
     *
     * @note  Vendor specific initialization can be different between manufacturers, should consult the LCD supplier for initialization sequence code.
     *
     * @param[in]  io LCD panel IO handle
     * @param[in]  panel_dev_config General panel device configuration
     * @param[out] ret_panel Returned LCD panel handle
     * @return
     *      - ESP_ERR_INVALID_ARG   if parameter is invalid
     *      - ESP_OK                on success
     *      - Otherwise             on fail
     */
    esp_err_t esp_lcd_new_panel_dsi(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                                        esp_lcd_panel_handle_t *ret_panel);


    void bsp_init_lcd_backlight(void);

    void bsp_set_lcd_backlight(uint32_t level);

    void esp_lcd_dsi_init(esp_lcd_handles_t *ret_handles);

/**
 * @brief MIPI DSI bus configuration structure
 *
 */
#define DSI_PANEL_BUS_DSI_2CH_CONFIG()               \
    {                                                \
        .bus_id = 0,                                 \
        .num_data_lanes = 2,                         \
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT, \
        .lane_bit_rate_mbps = 500,                   \
    }

/**
 * @brief MIPI DBI panel IO configuration structure
 *
 */
#define DSI_PANEL_IO_DBI_CONFIG()     \
    {                                 \
        .virtual_channel = 0,         \
        .lcd_cmd_bits = 8,            \
        .lcd_param_bits = 8,          \
    }

/**
 * @brief MIPI DPI configuration structure
 *
 * @note  refresh_rate = (dpi_clock_freq_mhz * 1000000) / (h_res + hsync_pulse_width + hsync_back_porch + hsync_front_porch)
 *                                                      / (v_res + vsync_pulse_width + vsync_back_porch + vsync_front_porch)
 *
 */
#define DSI_1232_568_PANEL_60HZ_CONFIG(px_format)     \
    {                                                 \
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,  \
        .dpi_clock_freq_mhz = 60,                     \
        .virtual_channel = 0,                         \
        .in_color_format = px_format,                 \
        .num_fbs = 1,                                 \
        .video_timing = {                             \
            .h_size = 1232,                           \
            .v_size = 568,                            \
            .hsync_back_porch = 150,                  \
            .hsync_pulse_width = 50,                  \
            .hsync_front_porch = 50,                  \
            .vsync_back_porch = 120,                  \
            .vsync_pulse_width = 40,                  \
            .vsync_front_porch = 80,                  \
        },                                            \
        .flags.use_dma2d = true,                      \
    }

#ifdef __cplusplus
}
#endif

#endif
