/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __RM69A10_H__
#define __RM69A10_H__
#include "../../esp_lcd_dsi/esp_lcd_dsi.h"


#ifdef __cplusplus
extern "C"
{
#endif


#define RM69A10_PANEL_BUS_DSI_2CH_CONFIG()            \
    {                                                 \
        .bus_id = 0,                                  \
        .num_data_lanes = 2,                          \
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,  \
        .lane_bit_rate_mbps = 1000,                   \
    }

/**
 * @brief MIPI DBI panel IO configuration structure
 *
 */
#define RM69A10_PANEL_IO_DBI_CONFIG() \
    {                                 \
        .virtual_channel = 0,         \
        .lcd_cmd_bits    = 8,         \
        .lcd_param_bits  = 8,         \
    }

/**
 * @brief MIPI DPI configuration structure
 *
 * @note  refresh_rate = (dpi_clock_freq_mhz * 1000000) / (h_res + hsync_pulse_width + hsync_back_porch + hsync_front_porch)
 *                                                      / (v_res + vsync_pulse_width + vsync_back_porch + vsync_front_porch)
 *
 */
#define RM69A10_568_1232_PANEL_60HZ_CONFIG(px_format) \
    {                                                 \
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,  \
        .dpi_clock_freq_mhz = 60,                     \
        .virtual_channel = 0,                         \
        .in_color_format = px_format,                 \
        .num_fbs = 1,                                 \
        .video_timing = {                             \
            .h_size = 568,                           \
            .v_size = 1232,                            \
            .hsync_back_porch = 150,                  \
            .hsync_pulse_width = 50,                  \
            .hsync_front_porch = 50,                  \
            .vsync_back_porch = 120,                  \
            .vsync_pulse_width = 40,                  \
            .vsync_front_porch = 80,                  \
        },                                            \
        .flags.use_dma2d = true,                      \
    }


// #define RM69A10_SCREEN_WIDTH 568
// #define RM69A10_SCREEN_HEIGHT 1232
// #define RM69A10_SCREEN_MIPI_DSI_DPI_CLK_MHZ 60
// #define RM69A10_SCREEN_MIPI_DSI_HSYNC 50
// #define RM69A10_SCREEN_MIPI_DSI_HBP 150
// #define RM69A10_SCREEN_MIPI_DSI_HFP 50
// #define RM69A10_SCREEN_MIPI_DSI_VSYNC 40
// #define RM69A10_SCREEN_MIPI_DSI_VBP 120
// #define RM69A10_SCREEN_MIPI_DSI_VFP 80
// #define RM69A10_SCREEN_DATA_LANE_NUM 2
// #define RM69A10_SCREEN_LANE_BIT_RATE_MBPS 1000


static const dsi_lcd_init_cmd_t inch4_init_cmd[] = {
        {0xFE, (uint8_t[]){0xFD}, 1, 0},
        {0x80, (uint8_t[]){0xFC}, 1, 0},
        {0xFE, (uint8_t[]){0x00}, 1, 0},
        {0x2A, (uint8_t[]){0x00, 0x00, 0x02, 0x37}, 4, 0},
        {0x2B, (uint8_t[]){0x00, 0x00, 0x04, 0xCF}, 4, 0},
        {0x31, (uint8_t[]){0x00, 0x03, 0x02, 0x34}, 4, 0},
        {0x30, (uint8_t[]){0x00, 0x00, 0x04, 0xCF}, 4, 0},
        {0x12, (uint8_t[]){0x00}, 1, 0},
        {0x35, (uint8_t[]){0x00}, 1, 0},
        {0x3A, (uint8_t[]){0x75}, 1, 10}, // interface pixel format 16bit/pixel
    // #if 1
    // #elif CONFIG_LCD_PIXEL_FORMAT_RGB888
        // {0x3A, (uint8_t[]){0x77}, 1, 10}, // interface pixel format 24bit/pixel

    // #endif
        {0x51, (uint8_t[]){0xFE}, 1, 0},
        // {0x51, (uint8_t[]){0xFF}, 1, 10}, // 设置屏幕亮度为0

        {0x11, (uint8_t[]){0x00}, 1, 130},
        {0x29, (uint8_t[]){0x00}, 1, 50},
};
#ifdef __cplusplus
}
#endif

#endif
