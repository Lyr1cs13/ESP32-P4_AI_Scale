// /*
//  * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
//  *
//  * SPDX-License-Identifier: Apache-2.0
//  */

// #include "soc/soc_caps.h"

// #if SOC_MIPI_DSI_SUPPORTED
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "driver/gpio.h"
// #include "esp_check.h"
// #include "esp_lcd_panel_commands.h"
// #include "esp_lcd_panel_interface.h"
// #include "esp_lcd_panel_io.h"
// #include "esp_lcd_mipi_dsi.h"
// #include "esp_lcd_panel_vendor.h"
// #include "esp_log.h"
// #include "rm69a10.h"

// #define RM69A10_PAD_CONTROL (0xB2)
// #define RM69A10_DSI_2_LANE (0x10)
// #define RM69A10_DSI_4_LANE (0x00)
// // #define CONFIG_LCD_PIXEL_FORMAT_RGB565 0
// #define RM69A10_CMD_SHLR_BIT (1ULL << 0)
// #define RM69A10_CMD_UPDN_BIT (1ULL << 1)
// #define RM69A10_MDCTL_VALUE_DEFAULT (0x01)
// #define Amoled_4_0_inch
// typedef struct
// {
//     esp_lcd_panel_io_handle_t io;
//     int reset_gpio_num;
//     uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
//     const RM69A10_lcd_init_cmd_t *init_cmds;
//     uint16_t init_cmds_size;
//     uint8_t lane_num;
//     struct
//     {
//         unsigned int reset_level : 1;
//     } flags;
//     // To save the original functions of MIPI DPI panel
//     esp_err_t (*del)(esp_lcd_panel_t *panel);
//     esp_err_t (*init)(esp_lcd_panel_t *panel);
// } RM69A10_panel_t;

// static const char *TAG = "RM69A10";

// static esp_err_t panel_RM69A10_send_init_cmds(RM69A10_panel_t *RM69A10);

// static esp_err_t panel_RM69A10_del(esp_lcd_panel_t *panel);
// static esp_err_t panel_RM69A10_init(esp_lcd_panel_t *panel);
// static esp_err_t panel_RM69A10_reset(esp_lcd_panel_t *panel);
// static esp_err_t panel_RM69A10_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
// static esp_err_t panel_RM69A10_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);

// esp_err_t esp_lcd_new_panel_RM69A10(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
//                                     esp_lcd_panel_handle_t *ret_panel)
// {
//     ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid arguments");
//     RM69A10_vendor_config_t *vendor_config = (RM69A10_vendor_config_t *)panel_dev_config->vendor_config;
//     ESP_RETURN_ON_FALSE(vendor_config && vendor_config->mipi_config.dpi_config && vendor_config->mipi_config.dsi_bus, ESP_ERR_INVALID_ARG, TAG,
//                         "invalid vendor config");

//     esp_err_t ret = ESP_OK;
//     RM69A10_panel_t *RM69A10 = (RM69A10_panel_t *)calloc(1, sizeof(RM69A10_panel_t));
//     ESP_RETURN_ON_FALSE(RM69A10, ESP_ERR_NO_MEM, TAG, "no mem for RM69A10 panel");

//     if (panel_dev_config->reset_gpio_num >= 0)
//     {
//         gpio_config_t io_conf = {
//             .mode = GPIO_MODE_OUTPUT,
//             .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
//         };
//         ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
//     }

//     RM69A10->io = io;
//     RM69A10->init_cmds = vendor_config->init_cmds;
//     RM69A10->init_cmds_size = vendor_config->init_cmds_size;
//     RM69A10->lane_num = vendor_config->mipi_config.lane_num;
//     RM69A10->reset_gpio_num = panel_dev_config->reset_gpio_num;
//     RM69A10->flags.reset_level = panel_dev_config->flags.reset_active_high;
//     RM69A10->madctl_val = RM69A10_MDCTL_VALUE_DEFAULT;

//     // Create MIPI DPI panel
//     ESP_GOTO_ON_ERROR(esp_lcd_new_panel_dpi(vendor_config->mipi_config.dsi_bus, vendor_config->mipi_config.dpi_config, ret_panel), err, TAG,
//                       "create MIPI DPI panel failed");
//     ESP_LOGD(TAG, "new MIPI DPI panel @%p", *ret_panel);

//     // Save the original functions of MIPI DPI panel
//     RM69A10->del = (*ret_panel)->del;
//     RM69A10->init = (*ret_panel)->init;
//     // Overwrite the functions of MIPI DPI panel
//     (*ret_panel)->del = panel_RM69A10_del;
//     (*ret_panel)->init = panel_RM69A10_init;
//     (*ret_panel)->reset = panel_RM69A10_reset;
//     (*ret_panel)->mirror = panel_RM69A10_mirror;
//     (*ret_panel)->invert_color = panel_RM69A10_invert_color;
//     (*ret_panel)->user_data = RM69A10;
//     ESP_LOGD(TAG, "new RM69A10 panel @%p", RM69A10);

//     return ESP_OK;

// err:
//     if (RM69A10)
//     {
//         if (panel_dev_config->reset_gpio_num >= 0)
//         {
//             gpio_reset_pin(panel_dev_config->reset_gpio_num);
//         }
//         free(RM69A10);
//     }
//     return ret;
// }

// static const RM69A10_lcd_init_cmd_t vendor_specific_init_default[] = {
// //  {cmd, { data }, data_size, delay_ms}
// // {0x80, (uint8_t []){0x8B}, 1, 0},
// // {0x81, (uint8_t []){0x78}, 1, 0},
// // {0x82, (uint8_t []){0x84}, 1, 0},
// // {0x83, (uint8_t []){0x88}, 1, 0},
// // {0x84, (uint8_t []){0xA8}, 1, 0},
// // {0x85, (uint8_t []){0xE3}, 1, 0},
// // {0x86, (uint8_t []){0x88}, 1, 0},
// // {0x11, (uint8_t []){0x00}, 0, 120},
// #ifdef Amoled_4_0_inch
//     //     {0xFE, (uint8_t[]){0xFD}, 1, 0},
//     //     {0x80, (uint8_t[]){0xFC}, 1, 0},
//     //     {0xFE, (uint8_t[]){0x00}, 1, 0},
//     //     {0x2A, (uint8_t[]){0x00, 0x00, 0x02, 0x37}, 4, 0},
//     //     {0x2B, (uint8_t[]){0x00, 0x00, 0x04, 0xCF}, 4, 0},
//     //     {0x31, (uint8_t[]){0x00, 0x03, 0x02, 0x34}, 4, 0},
//     //     {0x30, (uint8_t[]){0x00, 0x00, 0x04, 0xCF}, 4, 0},
//     //     {0x12, (uint8_t[]){0x00}, 1, 0},
//     //     {0x35, (uint8_t[]){0x00}, 1, 0},
//     // #if CONFIG_LCD_PIXEL_FORMAT_RGB565
//     //     {0x3A, (uint8_t[]){0x75}, 1, 0}, // interface pixel format 16bit/pixel
//     // #elif CONFIG_LCD_PIXEL_FORMAT_RGB888
//     //     {0x3A, (uint8_t[]){0x77}, 1, 0}, // interface pixel format 24bit/pixel

//     // #endif
//     //     {0x51, (uint8_t[]){0xFE}, 1, 0},
//     //     // {0x51, (uint8_t[]){0xEF}, 1, 0}, // 设置屏幕亮度为0
//     //     {0x11, (uint8_t[]){0x00}, 1, 120},
//     //     {0x29, (uint8_t[]){0x00}, 1, 0},
//     //============ Gamma END===========

//     {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
//     {0xC0, (uint8_t[]){0x63, 0x00}, 2, 0},
//     {0xC1, (uint8_t[]){0x11, 0x02}, 2, 0},
//     {0xC2, (uint8_t[]){0x37, 0x08}, 2, 0},
//     {0xCC, (uint8_t[]){0x30}, 1, 0},
//     {0xB0, (uint8_t[]){0x40, 0x03, 0x0B, 0x13, 0x19, 0x0C, 0x0D, 0x0B, 0x09, 0x20, 0x08, 0x15, 0x12, 0x0E, 0x1A, 0x14}, 16, 0},
//     {0xB1, (uint8_t[]){0x40, 0x02, 0xC9, 0x10, 0x15, 0x0A, 0x0A, 0x09, 0x09, 0x24, 0x08, 0x15, 0x11, 0x1B, 0x19, 0x14}, 16, 0},
//     {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
//     {0xB0, (uint8_t[]){0x4D}, 1, 0},
//     {0xB1, (uint8_t[]){0x6F}, 1, 0},
//     {0xB2, (uint8_t[]){0x07}, 1, 0},
//     {0xB3, (uint8_t[]){0x80}, 1, 0},
//     {0xB5, (uint8_t[]){0x47}, 1, 0},
//     {0xB7, (uint8_t[]){0x85}, 1, 0},
//     {0xB8, (uint8_t[]){0x21}, 1, 0},
//     {0xB9, (uint8_t[]){0x10}, 1, 0},
//     {0xC1, (uint8_t[]){0x78}, 1, 0},
//     {0xC2, (uint8_t[]){0x78}, 1, 0},
//     {0xD0, (uint8_t[]){0x88}, 1, 120},
//     {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
//     {0xE1, (uint8_t[]){0x08, 0x00, 0x0A, 0x00, 0x09, 0x00, 0x0B, 0x00, 0x00, 0x44, 0x44}, 11, 0},
//     {0xE2, (uint8_t[]){0x33, 0x33, 0x44, 0x44, 0x2D, 0x00, 0x2F, 0x00, 0x2E, 0x00, 0x30, 0x00, 0x00}, 13, 0},
//     {0xE3, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
//     {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
//     {0xE5, (uint8_t[]){0x0B, 0x40, 0xA0, 0xA0, 0x0D, 0x40, 0xA0, 0xA0, 0x0F, 0x40, 0xA0, 0xA0, 0x11, 0x40, 0xA0, 0xA0}, 16, 0},
//     {0xE6, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
//     {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
//     {0xE8, (uint8_t[]){0x0C, 0x40, 0xA0, 0xA0, 0x0E, 0x40, 0xA0, 0xA0, 0x10, 0x40, 0xA0, 0xA0, 0x12, 0x40, 0xA0, 0xA0}, 16, 0},
//     {0xEB, (uint8_t[]){0x02, 0x00, 0x4E, 0x4E, 0xEE, 0x44, 0x00}, 7, 0},
//     {0xEC, (uint8_t[]){0x00, 0x00}, 2, 0},
//     {0xED, (uint8_t[]){0xFF, 0xF1, 0x04, 0x56, 0x72, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF3, 0x27, 0x65, 0x40, 0x1F, 0xFF}, 16, 0},
//     {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
//     {0x11, (uint8_t[]){0x00}, 1, 120}, // delay_ms(120)
//     {0x3A, (uint8_t[]){0x77}, 1, 0},
//     {0x36, (uint8_t[]){0x00}, 1, 0},
//     {0x29, (uint8_t[]){0x00}, 1, 20}

// #else
//     {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
//     {0xEF, (uint8_t[]){0x08}, 1, 0},
//     {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
//     {0xC0, (uint8_t[]){0x4F, 0x00}, 2, 0},
//     {0xC1, (uint8_t[]){0x10, 0x0C}, 2, 0},
//     {0xC2, (uint8_t[]){0x07, 0x14}, 2, 0},
//     {0xCC, (uint8_t[]){0x10}, 1, 0},
//     {0xB0, (uint8_t[]){0x00, 0x0B, 0x13, 0x0D, 0x10, 0x07, 0x02, 0x08, 0x07, 0x1F, 0x04, 0x11, 0x0F, 0x28, 0x2F, 0x1F}, 16, 0},
//     {0xB1, (uint8_t[]){0x00, 0x0C, 0x13, 0x0C, 0x10, 0x05, 0x02, 0x08, 0x08, 0x1E, 0x05, 0x13, 0x11, 0x27, 0x30, 0x1F}, 16, 0},

//     {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
//     {0xB0, (uint8_t[]){0x4D}, 1, 0},
//     {0xB1, (uint8_t[]){0x55}, 1, 0},
//     {0xB2, (uint8_t[]){0x87}, 1, 0},
//     {0xB3, (uint8_t[]){0x80}, 1, 0},
//     {0xB5, (uint8_t[]){0x45}, 1, 0},
//     {0xB7, (uint8_t[]){0x85}, 1, 0},
//     {0xB8, (uint8_t[]){0x20}, 1, 0},
//     {0xC0, (uint8_t[]){0x09}, 1, 0},
//     {0xC1, (uint8_t[]){0x78}, 1, 0},
//     {0xC2, (uint8_t[]){0x78}, 1, 0},
//     {0xD0, (uint8_t[]){0x88}, 1, 100}, // Delay 100ms

//     {0xE0, (uint8_t[]){0x00, 0x00, 0x02}, 3, 0},
//     {0xE1, (uint8_t[]){0x04, 0xB0, 0x06, 0xB0, 0x05, 0xB0, 0x07, 0xB0, 0x00, 0x44, 0x44}, 11, 0},
//     {0xE2, (uint8_t[]){0x20, 0x20, 0x44, 0x44, 0x96, 0xA0, 0x00, 0x00, 0x96, 0xA0, 0x00, 0x00}, 12, 0},
//     {0xE3, (uint8_t[]){0x00, 0x00, 0x22, 0x22}, 4, 0},
//     {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
//     {0xE5, (uint8_t[]){0x0C, 0x90, 0xB0, 0xA0, 0x0E, 0x92, 0xB0, 0xA0, 0x08, 0x8C, 0xB0, 0xA0, 0x0A, 0x8E, 0xB0, 0xA0}, 16, 0},
//     {0xE6, (uint8_t[]){0x00, 0x00, 0x22, 0x22}, 4, 0},
//     {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
//     {0xE8, (uint8_t[]){0x0D, 0x91, 0xB0, 0xA0, 0x0F, 0x93, 0xB0, 0xA0, 0x09, 0x8D, 0xB0, 0xA0, 0x0B, 0x8F, 0xB0, 0xA0}, 16, 0},
//     {0xE9, (uint8_t[]){0x36, 0x00}, 2, 0},
//     {0xEB, (uint8_t[]){0x00, 0x00, 0xE4, 0xE4, 0x44, 0x88, 0x40}, 7, 0},
//     {0xED, (uint8_t[]){0xF1, 0xB2, 0xAC, 0x0F, 0x67, 0x45, 0xFF, 0xFF, 0xFF, 0xFF, 0x54, 0x76, 0xF0, 0xCA, 0x2B, 0x1F}, 16, 0},
//     {0xEF, (uint8_t[]){0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F}, 6, 0},

//     {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
//     {0xE8, (uint8_t[]){0x00, 0x0E}, 2, 0},
//     {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},

//     {0x11, (uint8_t[]){0x00}, 1, 120}, // Sleep out, delay 120ms

//     {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
//     {0xE8, (uint8_t[]){0x00, 0x0C}, 2, 10}, // Delay 10ms
//     {0xE8, (uint8_t[]){0x00, 0x00}, 2, 0},

//     {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
//     {0x36, (uint8_t[]){0x10}, 1, 0},

//     {0x29, (uint8_t[]){0x00}, 1, 20}, // Display on, delay 20ms
// #endif
// };

// static esp_err_t panel_RM69A10_send_init_cmds(RM69A10_panel_t *RM69A10)
// {
//     esp_lcd_panel_io_handle_t io = RM69A10->io;
//     const RM69A10_lcd_init_cmd_t *init_cmds = NULL;
//     uint16_t init_cmds_size = 0;
//     // uint8_t lane_command = RM69A10_DSI_2_LANE;
//     // bool is_cmd_overwritten = false;

//     // switch (RM69A10->lane_num) {
//     // case 0:
//     // case 2:
//     //     lane_command = RM69A10_DSI_2_LANE;
//     //     break;
//     // case 4:
//     //     lane_command = RM69A10_DSI_4_LANE;
//     //     break;
//     // default:
//         // ESP_LOGI(TAG, "Invalid lane number %d", RM69A10->lane_num);
//     //     return ESP_ERR_INVALID_ARG;
//     // }
//     // ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, RM69A10_PAD_CONTROL, (uint8_t[]) {
//     //     lane_command,
//     // }, 1), TAG, "send command failed");

// #if 1
//     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
//     vTaskDelay(pdMS_TO_TICKS(150));


//     // 尝试发送 Sleep Out (0x11) 命令，因为有些屏幕在休眠状态下不响应读命令
// esp_err_t sleep_out_ret = esp_lcd_panel_io_tx_param(io, 0x11, NULL, 0);
// if (sleep_out_ret == ESP_OK) {
//     ESP_LOGI(TAG, "Sent Sleep Out (0x11) command successfully.");
//     vTaskDelay(pdMS_TO_TICKS(120)); // 等待屏幕退出休眠
// } else {
//     ESP_LOGE(TAG, "Failed to send Sleep Out (0x11) command: %s", esp_err_to_name(sleep_out_ret));
// }
//     // 准备接收缓冲区
//     uint8_t id_data[4] = {0}; // 准备4个字节的缓冲区，以防万一

//     // 执行读命令 0x04 (read_display_id)
//     // MIPI DSI读操作通常是异步的，ESP-IDF的实现会阻塞等待结果
//     esp_err_t ret = esp_lcd_panel_io_rx_param(io, 0x04, id_data, 3); // 期望读取3个字节

//     if (ret == ESP_OK) {
//         ESP_LOGI(TAG, "Successfully read display ID!");
//         ESP_LOGI(TAG, "ID[0]: 0x%02X", id_data[0]);
//         ESP_LOGI(TAG, "ID[1]: 0x%02X", id_data[1]);
//         ESP_LOGI(TAG, "ID[2]: 0x%02X", id_data[2]);
//         // 根据ST7701数据手册，ID通常是 0x77, 0x01, 加上其他版本号
//         // 如果读到的不是全0或全FF，那就说明通信链路是通的！
//     } else {
//         ESP_LOGE(TAG, "Failed to read display ID. Error: %s", esp_err_to_name(ret));
//         ESP_LOGE(TAG, "This likely indicates a hardware connection issue (power, reset, or MIPI DSI lines P/N reversed) or a fundamental configuration error.");
//     }

//     uint8_t power_mode_data[2] = {0}; // 准备2个字节以防万一

// // 执行读命令 0x0A (get_power_mode)
//  ret = esp_lcd_panel_io_rx_param(io, 0x0A, power_mode_data, 1); // 期望读取1个字节

// if (ret == ESP_OK) {
//     ESP_LOGI(TAG, "Successfully read Power Mode!");
//     ESP_LOGI(TAG, "Power Mode: 0x%02X", power_mode_data[0]);
//     // 在Sleep Out之后，这个值通常不是0。如果读到非FF的值，比如0x9C, 0x8C等，就说明通信成功了。
// } else {
//     ESP_LOGE(TAG, "Failed to read Power Mode. Error: %s", esp_err_to_name(ret));
// }
// #endif
//     // // vendor specific initialization, it can be different between manufacturers
//     // // should consult the LCD supplier for initialization sequence code
//     // if (RM69A10->init_cmds) {
//     //     init_cmds = RM69A10->init_cmds;
//     //     init_cmds_size = RM69A10->init_cmds_size;
//     // } else {
//     //     init_cmds = vendor_specific_init_default;
//     //     init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(RM69A10_lcd_init_cmd_t);
//     // }

//     init_cmds = vendor_specific_init_default;
//     init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(RM69A10_lcd_init_cmd_t);
//     for (int i = 0; i < init_cmds_size; i++)
//     {
//         // Check if the command has been used or conflicts with the internal
//         // if (init_cmds[i].data_bytes > 0) {
//         //     switch (init_cmds[i].cmd) {
//         //     case LCD_CMD_MADCTL:
//         //         is_cmd_overwritten = true;
//         //         RM69A10->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
//         //         break;
//         //     default:
//         //         is_cmd_overwritten = false;
//         //         break;
//         //     }

//         //     if (is_cmd_overwritten) {
//         //         is_cmd_overwritten = false;
//         //         ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence",
//         //                  init_cmds[i].cmd);
//         //     }
//         // }

//         // Send command
//         ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
//         vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
//     }

//     ESP_LOGI(TAG, "send init commands success");

//     return ESP_OK;
// }

// static esp_err_t panel_RM69A10_del(esp_lcd_panel_t *panel)
// {
//     RM69A10_panel_t *RM69A10 = (RM69A10_panel_t *)panel->user_data;

//     if (RM69A10->reset_gpio_num >= 0)
//     {
//         gpio_reset_pin(RM69A10->reset_gpio_num);
//     }
//     // Delete MIPI DPI panel
//     RM69A10->del(panel);
//     ESP_LOGD(TAG, "del RM69A10 panel @%p", RM69A10);
//     free(RM69A10);

//     return ESP_OK;
// }

// static esp_err_t panel_RM69A10_init(esp_lcd_panel_t *panel)
// {
//     RM69A10_panel_t *RM69A10 = (RM69A10_panel_t *)panel->user_data;

//     ESP_RETURN_ON_ERROR(panel_RM69A10_send_init_cmds(RM69A10), TAG, "send init commands failed");
//     ESP_RETURN_ON_ERROR(RM69A10->init(panel), TAG, "init MIPI DPI panel failed");

//     return ESP_OK;
// }

// static esp_err_t panel_RM69A10_reset(esp_lcd_panel_t *panel)
// {
//     RM69A10_panel_t *RM69A10 = (RM69A10_panel_t *)panel->user_data;
//     esp_lcd_panel_io_handle_t io = RM69A10->io;

//     // Perform hardware reset
//     if (RM69A10->reset_gpio_num >= 0)
//     {
//         gpio_set_level(RM69A10->reset_gpio_num, RM69A10->flags.reset_level);
//         vTaskDelay(pdMS_TO_TICKS(20));
//         gpio_set_level(RM69A10->reset_gpio_num, !RM69A10->flags.reset_level);
//         vTaskDelay(pdMS_TO_TICKS(120));
//     }
//     else if (io)
//     { // Perform software reset
//         ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
//         vTaskDelay(pdMS_TO_TICKS(20));
//     }

//     return ESP_OK;
// }

// static esp_err_t panel_RM69A10_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
// {
//     RM69A10_panel_t *RM69A10 = (RM69A10_panel_t *)panel->user_data;
//     esp_lcd_panel_io_handle_t io = RM69A10->io;
//     uint8_t madctl_val = RM69A10->madctl_val;

//     ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_STATE, TAG, "invalid panel IO");

//     // Control mirror through LCD command
//     if (mirror_x)
//     {
//         madctl_val |= RM69A10_CMD_SHLR_BIT;
//     }
//     else
//     {
//         madctl_val &= ~RM69A10_CMD_SHLR_BIT;
//     }
//     if (mirror_y)
//     {
//         madctl_val |= RM69A10_CMD_UPDN_BIT;
//     }
//     else
//     {
//         madctl_val &= ~RM69A10_CMD_UPDN_BIT;
//     }

//     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){madctl_val}, 1), TAG, "send command failed");
//     RM69A10->madctl_val = madctl_val;

//     return ESP_OK;
// }

// static esp_err_t panel_RM69A10_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
// {
//     RM69A10_panel_t *RM69A10 = (RM69A10_panel_t *)panel->user_data;
//     esp_lcd_panel_io_handle_t io = RM69A10->io;
//     uint8_t command = 0;

//     ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_STATE, TAG, "invalid panel IO");

//     if (invert_color_data)
//     {
//         command = LCD_CMD_INVON;
//     }
//     else
//     {
//         command = LCD_CMD_INVOFF;
//     }
//     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");

//     return ESP_OK;
// }
// #endif
