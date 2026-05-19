/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "demos/lv_demos.h"
#include "HAL/lv_drv/lv_drv.h"

static const char *TAG = "dsi-example";

#if CONFIG_EXAMPLE_MONITOR_REFRESH_BY_GPIO
#define EXAMPLE_PIN_NUM_REFRESH_MONITOR         20  // Monitor the Refresh Rate by toggling the GPIO
#endif


#if CONFIG_EXAMPLE_MONITOR_REFRESH_BY_GPIO
static bool example_monitor_refresh_rate(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    static int io_level = 0;
    // please note, the real refresh rate should be 2*frequency of this GPIO toggling
    gpio_set_level(EXAMPLE_PIN_NUM_REFRESH_MONITOR, io_level);
    io_level = !io_level;
    return false;
}
#endif


#if CONFIG_EXAMPLE_MONITOR_REFRESH_BY_GPIO
static void example_bsp_init_refresh_monitor_io(void)
{
    gpio_config_t monitor_io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_REFRESH_MONITOR,
    };
    ESP_ERROR_CHECK(gpio_config(&monitor_io_conf));
}
#endif

void i2c_scanner(void)
{
    ESP_LOGI(TAG, "初始化 I2C 总线 (New API)...");


    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .i2c_port = BSP_I2C_NUM,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, 

    };

    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    ESP_LOGW(TAG, ">> 开始扫描设备...");
    
    int devices_found = 0;

    for (uint8_t addr = 0x01; addr < 0x7F; addr++) {

        esp_err_t ret = i2c_master_probe(bus_handle, addr, 50);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found device at: 0x%02x", addr);
            devices_found++;
        }
    }

    if (devices_found == 0) {
        ESP_LOGE(TAG, "未发现任何设备! 请检查接线、电源和复位引脚。");
    } else {
        ESP_LOGW(TAG, ">> 扫描结束，共发现 %d 个设备", devices_found);
    }
    i2c_del_master_bus(bus_handle);
}


void app_main(void)
{
#if CONFIG_EXAMPLE_MONITOR_REFRESH_BY_GPIO
    example_bsp_init_refresh_monitor_io();
#endif

    bsp_display_cfg_t cfg = {
    .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
    .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
    .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
    .flags = {
        .buff_dma = true,
        .buff_spiram = false,
        .sw_rotate = false,
    }};
    // static esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
   // i2c_scanner();
    lv_display_t *disp = lvgl_port_init_with_display_init(&cfg);
    extern void nutrition_lvgl_ui(lv_display_t *disp);
    nutrition_lvgl_ui(disp);
    
}
