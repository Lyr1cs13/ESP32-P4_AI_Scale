#ifndef __PCA9536_H__
#define __PCA9536_H__
#include "i2c_bus.h"
#include "esp_err.h"
#include "esp_log.h"


// PCA9536配置
#define PCA9536_ADDR         0x41        // I2C地址
// #define I2C_MASTER_SCL_IO    8           // SCL引脚
// #define I2C_MASTER_SDA_IO    7           // SDA引脚
#define I2C_MASTER_FREQ_HZ   400000      // I2C频率

// PCA9536寄存器
#define PCA9536_INPUT_REG    0x00
#define PCA9536_OUTPUT_REG   0x01
#define PCA9536_POLARITY_REG 0x02
#define PCA9536_CONFIG_REG   0x03

// 引脚定义
#define PCA9536_PIN0 0x01
#define PCA9536_PIN1 0x02
#define PCA9536_PIN2 0x04
#define PCA9536_PIN3 0x08

esp_err_t pca9536_init();
esp_err_t pca9536_set_pin_high(uint8_t pin);
esp_err_t pca9536_set_pin_low(uint8_t pin);

#endif // __PCA9536_H__