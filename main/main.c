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
#include "nutrition_app.h"
#include "ai_scale_perception.h"

static const char *TAG = "dsi-example";

#if CONFIG_EXAMPLE_MONITOR_REFRESH_BY_GPIO
#define EXAMPLE_PIN_NUM_REFRESH_MONITOR         20  // Monitor the Refresh Rate by toggling the GPIO
#endif

static void perception_foods_updated(const char *const names[],
                                     const float weights_g[],
                                     size_t count,
                                     void *user_ctx)
{
    (void)user_ctx;
    ESP_LOGI(TAG, "received perception update: %u food class(es)", (unsigned)count);
    for (size_t i = 0; i < count; ++i) {
        ESP_LOGI(TAG, "  -> nutrition UI: %s %.2f g", names[i], weights_g[i]);
    }
    if (!nutrition_update_ingredients_from_names(names, weights_g, count)) {
        ESP_LOGW(TAG, "ignored perception food update");
    }
}

#if CONFIG_AI_SCALE_RUN_MODE_PERCEPTION_ONLY
static void perception_foods_logged(const char *const names[],
                                    const float weights_g[],
                                    size_t count,
                                    void *user_ctx)
{
    (void)user_ctx;
    ESP_LOGI(TAG, "perception result: %u food class(es)", (unsigned)count);
    for (size_t i = 0; i < count; ++i) {
        ESP_LOGI(TAG, "  %s %.1f g", names[i], weights_g[i]);
    }
}
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

static lv_display_t *start_display_and_nutrition_ui(void)
{
    bsp_display_cfg_t cfg = {
    .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
    .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
    .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
    .flags = {
        .buff_dma = true,
        .buff_spiram = false,
        .sw_rotate = false,
    }};

    lv_display_t *disp = lvgl_port_init_with_display_init(&cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "display init failed");
        return NULL;
    }

    if (lvgl_port_lock(0)) {
        nutrition_lvgl_ui(disp);
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "failed to lock LVGL while creating nutrition UI");
    }
    return disp;
}

static esp_err_t start_perception_pipeline(ai_scale_foods_updated_cb_t callback)
{
    esp_err_t perception_ret = ai_scale_perception_prepare_camera();
    if (perception_ret != ESP_OK) {
        ESP_LOGE(TAG, "perception camera pipeline not ready: %s", esp_err_to_name(perception_ret));
        return perception_ret;
    }

    ai_scale_perception_config_t perception_cfg = {
        .foods_updated_cb = callback,
        .user_ctx = NULL,
        .enable_weight_sensor = true,
        .enable_camera_yolo = true,
    };
    return ai_scale_perception_start(&perception_cfg);
}

static void start_perception_pipeline_or_warn(ai_scale_foods_updated_cb_t callback, bool keep_ui_alive)
{
    esp_err_t ret = start_perception_pipeline(callback);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "perception pipeline started");
        return;
    }

    ESP_LOGE(TAG, "perception pipeline disabled: %s", esp_err_to_name(ret));
    ESP_LOGW(TAG, "check camera module, MIPI-CSI cable direction, sensor type, and SCCB/I2C pins");
    if (keep_ui_alive) {
        ESP_LOGW(TAG, "fallback mode: touch UI, serial food input, and NutriCook remain available");
    }
}

void app_main(void)
{
#if CONFIG_EXAMPLE_MONITOR_REFRESH_BY_GPIO
    example_bsp_init_refresh_monitor_io();
#endif

#if CONFIG_AI_SCALE_RUN_MODE_PERCEPTION_ONLY
    ESP_LOGI(TAG, "run mode: perception only");
    ESP_LOGI(TAG, "pass criteria: camera ready, HX711 ready, YOLO waits for trigger, then logs Stored item/perception result");
    start_perception_pipeline_or_warn(perception_foods_logged, false);
#elif CONFIG_AI_SCALE_RUN_MODE_DISPLAY_NUTRITION_ONLY
    ESP_LOGI(TAG, "run mode: display and nutrition only");
    ESP_LOGI(TAG, "pass criteria: three-page UI works, serial food input refreshes page 1, page 3 logs NutriCook inference time");
    start_display_and_nutrition_ui();
#else
    ESP_LOGI(TAG, "run mode: full AI scale");
    ESP_LOGI(TAG, "pass criteria: perception result refreshes page 1, cooking selection triggers page 3 nutrition result");
    start_display_and_nutrition_ui();
    start_perception_pipeline_or_warn(perception_foods_updated, true);
#endif
     
}
