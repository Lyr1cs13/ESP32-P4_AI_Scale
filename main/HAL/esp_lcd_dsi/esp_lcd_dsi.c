/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "soc/soc_caps.h"

#if SOC_MIPI_DSI_SUPPORTED
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lcd_dsi.h"
#include "config.h"


#include "../display/RM69A10/rm69a10.h"
#include "../display/ST7701S/st7701s.h"
#include "../display/jd9365.h"

static const char *TAG = "hal/esp_lcd_dsi";



#define DSI_PAD_CONTROL (0xB2)
#define DSI_2_LANE (0x10)
#define DSI_4_LANE (0x00)

#define DSI_CMD_SHLR_BIT (1ULL << 0)
#define DSI_CMD_UPDN_BIT (1ULL << 1)
#define DSI_MDCTL_VALUE_DEFAULT (0x01)


#define Amoled_4_0_inch
typedef struct
{
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    const dsi_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    uint8_t lane_num;
    struct
    {
        unsigned int reset_level : 1;
    } flags;
    // To save the original functions of MIPI DPI panel
    esp_err_t (*del)(esp_lcd_panel_t *panel);
    esp_err_t (*init)(esp_lcd_panel_t *panel);
} DSI_panel_t;


static esp_err_t panel_DSI_send_init_cmds(DSI_panel_t *DSI);
static esp_err_t panel_DSI_del(esp_lcd_panel_t *panel);
static esp_err_t panel_DSI_init(esp_lcd_panel_t *panel);
static esp_err_t panel_DSI_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_DSI_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_DSI_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t check_display_connect(esp_lcd_panel_io_handle_t panel);

esp_err_t esp_lcd_new_panel_dsi(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid arguments");
    dsi_vendor_config_t *vendor_config = (dsi_vendor_config_t *)panel_dev_config->vendor_config;
    ESP_RETURN_ON_FALSE(vendor_config && vendor_config->mipi_config.dpi_config && vendor_config->mipi_config.dsi_bus, ESP_ERR_INVALID_ARG, TAG,
                        "invalid vendor config");

    esp_err_t ret = ESP_OK;
    DSI_panel_t *DSI = (DSI_panel_t *)calloc(1, sizeof(DSI_panel_t));
    ESP_RETURN_ON_FALSE(DSI, ESP_ERR_NO_MEM, TAG, "no mem for DSI panel");

    if (panel_dev_config->reset_gpio_num >= 0)
    {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    DSI->io = io;
    DSI->init_cmds = vendor_config->init_cmds;
    DSI->init_cmds_size = vendor_config->init_cmds_size;
    DSI->lane_num = vendor_config->mipi_config.lane_num;
    DSI->reset_gpio_num = panel_dev_config->reset_gpio_num;
    DSI->flags.reset_level = panel_dev_config->flags.reset_active_high;
    DSI->madctl_val = DSI_MDCTL_VALUE_DEFAULT;

    // Create MIPI DPI panel
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_dpi(vendor_config->mipi_config.dsi_bus, vendor_config->mipi_config.dpi_config, ret_panel), err, TAG,
                      "create MIPI DPI panel failed");
    ESP_LOGD(TAG, "new MIPI DPI panel @%p", *ret_panel);

    // Save the original functions of MIPI DPI panel
    DSI->del = (*ret_panel)->del;
    DSI->init = (*ret_panel)->init;
    // Overwrite the functions of MIPI DPI panel
    (*ret_panel)->del = panel_DSI_del;
    (*ret_panel)->init = panel_DSI_init;
    (*ret_panel)->reset = panel_DSI_reset;
    (*ret_panel)->mirror = panel_DSI_mirror;
    (*ret_panel)->invert_color = panel_DSI_invert_color;
    (*ret_panel)->user_data = DSI;
    ESP_LOGD(TAG, "new DSI panel @%p", DSI);

    return ESP_OK;

err:
    if (DSI)
    {
        if (panel_dev_config->reset_gpio_num >= 0)
        {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(DSI);
    }
    return ret;
}

static const dsi_lcd_init_cmd_t vendor_specific_init_default[] = {

#ifdef Amoled_4_0_inch
    //     {0xFE, (uint8_t[]){0xFD}, 1, 0},
    //     {0x80, (uint8_t[]){0xFC}, 1, 0},
    //     {0xFE, (uint8_t[]){0x00}, 1, 0},
    //     {0x2A, (uint8_t[]){0x00, 0x00, 0x02, 0x37}, 4, 0},
    //     {0x2B, (uint8_t[]){0x00, 0x00, 0x04, 0xCF}, 4, 0},
    //     {0x31, (uint8_t[]){0x00, 0x03, 0x02, 0x34}, 4, 0},
    //     {0x30, (uint8_t[]){0x00, 0x00, 0x04, 0xCF}, 4, 0},
    //     {0x12, (uint8_t[]){0x00}, 1, 0},
    //     {0x35, (uint8_t[]){0x00}, 1, 0},
    // #if CONFIG_LCD_PIXEL_FORMAT_RGB565
    //     {0x3A, (uint8_t[]){0x75}, 1, 0}, // interface pixel format 16bit/pixel
    // #elif CONFIG_LCD_PIXEL_FORMAT_RGB888
    //     {0x3A, (uint8_t[]){0x77}, 1, 0}, // interface pixel format 24bit/pixel

    // #endif
    //     {0x51, (uint8_t[]){0xFE}, 1, 0},
    //     // {0x51, (uint8_t[]){0xEF}, 1, 0}, // 设置屏幕亮度为0
    //     {0x11, (uint8_t[]){0x00}, 1, 120},
    //     {0x29, (uint8_t[]){0x00}, 1, 0},
    //============ Gamma END===========


#else

#endif
};

static esp_err_t panel_DSI_send_init_cmds(DSI_panel_t *DSI)
{
    esp_lcd_panel_io_handle_t io = DSI->io;
    const dsi_lcd_init_cmd_t *init_cmds = NULL;
    uint16_t init_cmds_size = 0;
    // uint8_t lane_command = DSI_DSI_2_LANE;
    // bool is_cmd_overwritten = false;

    // switch (DSI->lane_num) {
    // case 0:
    // case 2:
    //     lane_command = DSI_DSI_2_LANE;
    //     break;
    // case 4:
    //     lane_command = DSI_DSI_4_LANE;
    //     break;
    // default:
        // ESP_LOGI(TAG, "Invalid lane number %d", DSI->lane_num);
    //     return ESP_ERR_INVALID_ARG;
    // }
    // ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, DSI_PAD_CONTROL, (uint8_t[]) {
    //     lane_command,
    // }, 1), TAG, "send command failed");

    // // vendor specific initialization, it can be different between manufacturers
    // // should consult the LCD supplier for initialization sequence code


    // // Optional: Check if the display is connected by reading its ID
#if CONFIG_DISPLAY_READ_ID
    check_display_connect(io);
#endif
    // Send vendor specific init commands
    if (DSI->init_cmds) {
        init_cmds = DSI->init_cmds;
        init_cmds_size = DSI->init_cmds_size;
        ESP_LOGI(TAG, "using user defined init commands (size: %d)", init_cmds_size);
    } else {
        ESP_LOGI(TAG, "using default init commands");
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(dsi_lcd_init_cmd_t);
    }

    // init_cmds = vendor_specific_init_default;
    // init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(dsi_lcd_init_cmd_t);

    for (int i = 0; i < init_cmds_size; i++)
    {
        // Check if the command has been used or conflicts with the internal
        // if (init_cmds[i].data_bytes > 0) {
        //     switch (init_cmds[i].cmd) {
        //     case LCD_CMD_MADCTL:
        //         is_cmd_overwritten = true;
        //         DSI->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
        //         break;
        //     default:
        //         is_cmd_overwritten = false;
        //         break;
        //     }

        //     if (is_cmd_overwritten) {
        //         is_cmd_overwritten = false;
        //         ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence",
        //                  init_cmds[i].cmd);
        //     }
        // }

        // Send command
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }

    ESP_LOGI(TAG, "send init commands success");

    return ESP_OK;
}

static esp_err_t panel_DSI_del(esp_lcd_panel_t *panel)
{
    DSI_panel_t *DSI = (DSI_panel_t *)panel->user_data;

    if (DSI->reset_gpio_num >= 0)
    {
        gpio_reset_pin(DSI->reset_gpio_num);
    }
    // Delete MIPI DPI panel
    DSI->del(panel);
    ESP_LOGD(TAG, "del DSI panel @%p", DSI);
    free(DSI);

    return ESP_OK;
}

static esp_err_t panel_DSI_init(esp_lcd_panel_t *panel)
{
    DSI_panel_t *DSI = (DSI_panel_t *)panel->user_data;

    ESP_RETURN_ON_ERROR(panel_DSI_send_init_cmds(DSI), TAG, "send init commands failed");
    ESP_RETURN_ON_ERROR(DSI->init(panel), TAG, "init MIPI DPI panel failed");

    return ESP_OK;
}

static esp_err_t panel_DSI_reset(esp_lcd_panel_t *panel)
{
    DSI_panel_t *DSI = (DSI_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = DSI->io;

    // Perform hardware reset
    if (DSI->reset_gpio_num >= 0)
    {
        gpio_set_level(DSI->reset_gpio_num, DSI->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(DSI->reset_gpio_num, !DSI->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(20));//120
    }
    else if (io)
    { // Perform software reset
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ESP_OK;
}

static esp_err_t panel_DSI_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    DSI_panel_t *DSI = (DSI_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = DSI->io;
    uint8_t madctl_val = DSI->madctl_val;

    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_STATE, TAG, "invalid panel IO");

    // Control mirror through LCD command
    if (mirror_x)
    {
        madctl_val |= DSI_CMD_SHLR_BIT;
    }
    else
    {
        madctl_val &= ~DSI_CMD_SHLR_BIT;
    }
    if (mirror_y)
    {
        madctl_val |= DSI_CMD_UPDN_BIT;
    }
    else
    {
        madctl_val &= ~DSI_CMD_UPDN_BIT;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){madctl_val}, 1), TAG, "send command failed");
    DSI->madctl_val = madctl_val;

    return ESP_OK;
}

static esp_err_t panel_DSI_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    DSI_panel_t *DSI = (DSI_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = DSI->io;
    uint8_t command = 0;

    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_STATE, TAG, "invalid panel IO");

    if (invert_color_data)
    {
        command = LCD_CMD_INVON;
    }
    else
    {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");

    return ESP_OK;
}



static esp_err_t check_display_connect(esp_lcd_panel_io_handle_t panel)
{


    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    //尝试发送 Sleep Out (0x11) 命令，因为有些屏幕在休眠状态下不响应读命令
    esp_err_t sleep_out_ret = esp_lcd_panel_io_tx_param(panel, 0x11, NULL, 0);
    if (sleep_out_ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Sent Sleep Out (0x11) command successfully.");
        vTaskDelay(pdMS_TO_TICKS(120)); // 等待屏幕退出休眠
        // ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel, 0x29, NULL, 0), TAG, "send command failed");
        // vTaskDelay(pdMS_TO_TICKS(20)); // 等待屏幕退出休眠
    }
    else
    {
        ESP_LOGE(TAG, "Failed to send Sleep Out (0x11) command: %s", esp_err_to_name(sleep_out_ret));
    }


    // // 准备接收缓冲区
    uint8_t id_data[4] = {0}; // 准备4个字节的缓冲区，以防万一

    // // 执行读命令 0x04 (read_display_id)
    // // MIPI DSI读操作通常是异步的，ESP-IDF的实现会阻塞等待结果
    esp_err_t ret = esp_lcd_panel_io_rx_param(panel, 0xa1, id_data, 4); // 期望读取3个字节

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Successfully read display ID!");
        ESP_LOGI(TAG, "ID[0]: 0x%02X", id_data[0]);
        ESP_LOGI(TAG, "ID[1]: 0x%02X", id_data[1]);
        ESP_LOGI(TAG, "ID[2]: 0x%02X", id_data[2]);
        // 根据ST7701数据手册，ID通常是 0x77, 0x01, 加上其他版本号
        // 如果读到的不是全0或全FF，那就说明通信链路是通的！
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read display ID. Error: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "This likely indicates a hardware connection issue (power, reset, or MIPI DSI lines P/N reversed) or a fundamental configuration error.");
    }

    // uint8_t power_mode_data[2] = {0}; // 准备2个字节以防万一

    // // 执行读命令 0x0A (get_power_mode)
    // ret = esp_lcd_panel_io_rx_param(panel, 0x51, power_mode_data, 1); // 期望读取1个字节

    // if (ret == ESP_OK)
    // {
    //     ESP_LOGI(TAG, "Successfully read Power Mode!");
    //     ESP_LOGI(TAG, "Power Mode: 0x%02X", power_mode_data[0]);
    //     // 在Sleep Out之后，这个值通常不是0。如果读到非FF的值，比如0x9C, 0x8C等，就说明通信成功了。

    //     return ret;
    // }
    // else
    // {
    //     ESP_LOGE(TAG, "Failed to read Power Mode. Error: %s", esp_err_to_name(ret));
    //     return ESP_FAIL;       
    // }

    return ESP_FAIL;
}




void enable_dsi_phy_power(void)
{
    // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
#ifdef MIPI_DSI_PHY_PWR_LDO_CHAN
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif
}


void bsp_init_lcd_backlight(void)
{
#if PIN_NUM_BK_LIGHT >= 0
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif
}

void bsp_set_lcd_backlight(uint32_t level)
{
#if PIN_NUM_BK_LIGHT >= 0
    gpio_set_level(PIN_NUM_BK_LIGHT, level);
#endif
}


void esp_lcd_dsi_init(esp_lcd_handles_t *ret_handles)
{
    esp_err_t ret = ESP_OK;

    enable_dsi_phy_power();
    // Initialize backlight control pin
    //bsp_init_lcd_backlight();
    // Turn on backlight
    //bsp_set_lcd_backlight(1);


    // create MIPI DSI bus first, it will initialize the DSI PHY as well
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_panel_handle_t mipi_dpi_panel = NULL;

    esp_lcd_dsi_bus_config_t bus_config = esp_lcd_dsi_bus_config;
    // esp_lcd_dsi_bus_config_t bus_config = DSI_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    
    esp_lcd_panel_io_handle_t mipi_dbi_io;
    // 3. install MIPI DSI DBI IO driver
    esp_lcd_dbi_io_config_t dbi_config  =  DSI_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));
    // esp_lcd_dpi_panel_config_t _dpi_config  = dpi_config;
    esp_lcd_dpi_panel_config_t dpi_config  = dsi_panel(USER_COLOR_DEFINE);
        // esp_lcd_dpi_panel_config_t dpi_config  = ST7701_480_800_PANEL_30HZ_CONFIG(LCD_COLOR_FMT_RGB888);
    // esp_lcd_dpi_panel_config_t dpi_config  = RM69A10_1232_568_PANEL_60HZ_CONFIG(LCD_COLOR_FMT_RGB888);
    ESP_LOGI(TAG, "Install MIPI DSI LCD control IO");
    // 5. LCD init commands
    dsi_vendor_config_t vendor_config = {
        .init_cmds = dsi_init_cmd, //inch_397_init_cmd WLK280_init_cmd
        .init_cmds_size = sizeof(dsi_init_cmd) / sizeof(dsi_lcd_init_cmd_t),
        // .init_cmds = NULL,
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };

    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#if CONFIG_DSI_LCD_COLOR_FORMAT_565
        .bits_per_pixel = 16,
#else
        .bits_per_pixel = 24,
#endif
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_dsi(mipi_dbi_io, &lcd_dev_config, &mipi_dpi_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(mipi_dpi_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(mipi_dpi_panel));
    ESP_LOGI(TAG, "DSI LCD panel initialized successfully");

    // esp_lcd_dpi_panel_set_pattern(mipi_dpi_panel, MIPI_DSI_PATTERN_BAR_VERTICAL);
    ret_handles->io = mipi_dbi_io;
    ret_handles->mipi_dsi_bus = mipi_dsi_bus;
    ret_handles->panel = mipi_dpi_panel;
    ret_handles->control = NULL;
    // esp_lcd_dpi_panel_set_pattern(mipi_dpi_panel, MIPI_DSI_PATTERN_BAR_HORIZONTAL);


}

#endif
