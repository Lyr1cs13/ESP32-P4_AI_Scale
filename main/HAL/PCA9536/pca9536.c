#include "pca9536.h"
#include "../I2C/I2c_drv.h"
static const char *TAG = "PCA9536";

i2c_master_dev_handle_t _dev_handle = NULL; // PCA9536设备句柄


// void i2c_scan(void) {
//     for (uint8_t addr = 0x08; addr < 0x78; addr++) {
//         uint8_t dummy = 0;
//         esp_err_t ret = i2c_master_transmit(_dev_handle, &dummy, 0, 1000);
//         if (ret == ESP_OK) {
//             ESP_LOGI(TAG, "找到 I2C 设备，地址: 0x%02X", addr);
//         }
//     }
// }


// 初始化PCA9536
esp_err_t pca9536_init()
{
    i2c_drv_init();

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = 400 * 1000,
        .device_address = PCA9536_ADDR,
    };

    esp_err_t ret = i2c_master_bus_add_device(i2c_drv_get_handle(), &i2c_dev_conf, &_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "添加 PCA9536 设备失败");
        return ret;
    }

    // !! 必须先配置引脚为输出模式 !!
    uint8_t config_data[2] = {PCA9536_CONFIG_REG, 0x00}; // 0x00 表示所有引脚为输出
    ret = i2c_master_transmit(_dev_handle, config_data, sizeof(config_data), 50);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置 PCA9536 引脚为输出失败");
        i2c_master_bus_rm_device(_dev_handle); // 出错时清理
        return ret;
    }
    
    // // 然后再设置初始电平
    // uint8_t output_data[2] = {PCA9536_OUTPUT_REG, 0x00}; // 初始输出低电平
    // ret = i2c_master_transmit(_dev_handle, output_data, sizeof(output_data), 50);

    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "设置 PCA9536 初始电平失败");
    //     i2c_master_bus_rm_device(_dev_handle);
    //     return ret;
    // }

    ESP_LOGI(TAG, "PCA9536 initialized successfully");

    return ESP_OK;
}
esp_err_t pca9536_set_pin_high(uint8_t pin)
{
    esp_err_t ret;
    uint8_t current_val;
    
    // 1. 先发送要读取的寄存器地址，然后读取1个字节的数据
    uint8_t reg_addr = PCA9536_OUTPUT_REG;
    ret = i2c_master_transmit_receive(
        _dev_handle,      // 设备句柄
        &reg_addr,        // 要发送的数据（寄存器地址）
        1,                // 发送的字节数
        &current_val,     // 接收数据的缓冲区
        1,                // 期望接收的字节数
        50                // 超时 (50ms)
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取PCA9536输出寄存器失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 修改值
    uint8_t new_val = current_val | pin;

    // 如果值没有变化，就不需要再次写入，可以节省I2C总线操作
    if (new_val == current_val) {
        return ESP_OK;
    }

    // 3. 将新值写回寄存器
    uint8_t data_to_send[2] = {PCA9536_OUTPUT_REG, new_val};
    return i2c_master_transmit(_dev_handle, data_to_send, sizeof(data_to_send), 50);
}

// 设置引脚低电平
 esp_err_t pca9536_set_pin_low(uint8_t pin)
{
    esp_err_t ret;
    uint8_t current_val;
    
    // 1. 先发送要读取的寄存器地址，然后读取1个字节的数据
    uint8_t reg_addr = PCA9536_OUTPUT_REG;
    ret = i2c_master_transmit_receive(
        _dev_handle,      // 设备句柄
        &reg_addr,        // 要发送的数据（寄存器地址）
        1,                // 发送的字节数
        &current_val,     // 接收数据的缓冲区
        1,                // 期望接收的字节数
        50                // 超时 (50ms)
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取PCA9536输出寄存器失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 修改值
    uint8_t new_val = current_val & ~pin;

    // 如果值没有变化，就不需要再次写入，可以节省I2C总线操作
    if (new_val == current_val) {
        return ESP_OK;
    }

    // 3. 将新值写回寄存器
    uint8_t data_to_send[2] = {PCA9536_OUTPUT_REG, new_val};
    return i2c_master_transmit(_dev_handle, data_to_send, sizeof(data_to_send), 50);
}

i2c_master_dev_handle_t pca9536_get_handle(void)
{
    return _dev_handle;
}