#include "I2c_drv.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "I2C_DRV";

bool i2c_initialized = false;
i2c_master_bus_handle_t i2c_handle = NULL;  // I2C Handle

esp_err_t i2c_drv_init(void)
{
    /* I2C was initialized before */
    if (i2c_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .i2c_port = BSP_I2C_NUM,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = 1,
            .allow_pd = 0,
        },

    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_conf, &i2c_handle));

    i2c_initialized = true;
     //i2c_scan();
    return ESP_OK;
}


/**
 * @brief Scans the I2C bus for connected devices.
 *
 * This function iterates through all possible 7-bit I2C addresses (from 0x01 to 0x7F)
 * and attempts to communicate with each address. If a device acknowledges its address,
 * its address is printed to the console.
 *
 * This is a blocking function and should be called after I2C bus initialization.
 */
void i2c_scan(void)
{
    // 1. 获取已经初始化好的 I2C 总线句柄
    // 确保在调用此函数前，您的 i2c_drv_init() 已经被成功调用
    i2c_master_bus_handle_t bus_handle = i2c_drv_get_handle();
    if (bus_handle == NULL) {
        ESP_LOGI(TAG, "I2C bus has not been initialized. Call i2c_drv_init() first.");
        return;
    }

    ESP_LOGI(TAG, "Starting I2C bus scan...");

    uint8_t address;
    esp_err_t ret;
    int devices_found = 0;

    for (address = 1; address < 127; address++) {
        // i2c_master_probe 是一个专门用来探测设备是否存在的函数
        // 它会发送设备地址，然后等待ACK。它比一次完整的读写事务要快。
        // 第三个参数是超时时间，单位是毫秒。
        ret = i2c_master_probe(bus_handle, address, 50);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found device at I2C address: 0x%02X", address);
            devices_found++;
        } else if (ret == ESP_ERR_TIMEOUT) {
            // ESP_ERR_TIMEOUT 表示从机没有在规定时间内响应（没有ACK），这是最常见的情况
            // 我们不需要打印日志，继续扫描下一个地址
        } else {
            // 其他错误，例如总线错误
            ESP_LOGI(TAG, "Error probing address 0x%02X: %s", address, esp_err_to_name(ret));
        }

        // 短暂延时，避免过于频繁地占用总线
        // vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (devices_found == 0) {
        ESP_LOGW(TAG, "No I2C devices found on the bus.");
    } else {
        ESP_LOGI(TAG, "Scan finished. Found %d device(s).", devices_found);
    }
}


esp_err_t i2c_drv_deinit(void)
{
    ESP_ERROR_CHECK(i2c_del_master_bus(i2c_handle));
    i2c_initialized = false;
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_drv_get_handle(void)
{
    return i2c_handle;
}
