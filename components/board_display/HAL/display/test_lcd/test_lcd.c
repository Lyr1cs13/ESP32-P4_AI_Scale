#include "test_lcd.h"

const char *TAG = "test_lcd";

#define  EXAMPLE_MIPI_DSI_LANE_NUM 1 
#define  EXAMPLE_MIPI_DSI_LANE_BITRATE_MBPS 680

void test_lcd_init(void)
{

    //使能MIPI DSI PHY电源
    enable_dsi_phy_power();
    //初始化LCD背光
    bsp_init_lcd_backlight();
    bsp_set_lcd_backlight(0); // 先关闭背光
    //创建MIPI DSI总线
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    //创建MIPI DSI总线配置
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = EXAMPLE_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = EXAMPLE_MIPI_DSI_LANE_BITRATE_MBPS,
    };
    //创建MIPI DSI总线
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));
    //安装MIPI DSI DBI IO驱动
    esp_lcd_panel_io_handle_t mipi_dbi_io;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,   // 根据LCD规格书配置
        .lcd_param_bits = 8, // 根据LCD规格书配置
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));
    //创建MIPI DSI DPI面板
    esp_lcd_panel_handle_t mipi_dpi_panel = NULL;
    // create MIPI DSI DPI panel
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = TEST_MIPI_DSI_DPI_CLK_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB888, // modify 888
        .video_timing = {
            .h_size = TEST_MIPI_DSI_LCD_H_RES,
            .v_size = TEST_MIPI_DSI_LCD_V_RES,
            .hsync_back_porch = TEST_MIPI_DSI_LCD_HBP,
            .hsync_pulse_width = TEST_MIPI_DSI_LCD_HSYNC,
            .hsync_front_porch = TEST_MIPI_DSI_LCD_HFP,
            .vsync_back_porch = TEST_MIPI_DSI_LCD_VBP,
            .vsync_pulse_width = TEST_MIPI_DSI_LCD_VSYNC,
            .vsync_front_porch = TEST_MIPI_DSI_LCD_VFP,
        },
#if CONFIG_EXAMPLE_USE_DMA2D_COPY_FRAME
        .flags.use_dma2d = true, // use DMA2D to copy draw buffer into frame buffer
#endif
    };

        // 5. LCD init commands
    dsi_vendor_config_t vendor_config = {
        .init_cmds = NULL, //inch_397_init_cmd WLK280_init_cmd
        .init_cmds_size = sizeof(NULL) / sizeof(dsi_lcd_init_cmd_t),
        // .init_cmds = NULL,
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .reset_gpio_num = TEST_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_dsi(mipi_dbi_io, &lcd_dev_config, &mipi_dpi_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(mipi_dpi_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(mipi_dpi_panel));

    esp_lcd_dpi_panel_set_pattern(mipi_dpi_panel, MIPI_DSI_PATTERN_BAR_VERTICAL);
 
}