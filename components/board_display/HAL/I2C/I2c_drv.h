#ifndef _I2C_DRV_H_
#define _I2C_DRV_H_
// #include "i2c_bus.h"
#include "config.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"



esp_err_t i2c_drv_init(void);

esp_err_t i2c_drv_deinit(void);

i2c_master_bus_handle_t i2c_drv_get_handle(void);
void i2c_scan(void);
#endif //_I2C_DRV_H_