#ifndef HX711_H
#define HX711_H

#include <stdbool.h>
#include "driver/gpio.h"

typedef enum {
    HX711_GAIN_128 = 1,
    HX711_GAIN_32 = 2,
    HX711_GAIN_64 = 3,
} hx711_gain_t;

void hx711_init(gpio_num_t dout, gpio_num_t pd_sck, hx711_gain_t gain);
bool hx711_is_ready(void);
void hx711_set_gain(hx711_gain_t gain);
uint32_t hx711_read(void);
uint32_t hx711_read_average(uint8_t times);
uint32_t hx711_get_value(uint8_t times);
float hx711_get_units(uint8_t times);
void hx711_tare(void);
void hx711_set_scale(float scale);
float hx711_get_scale(void);
void hx711_set_offset(uint32_t offset);
uint32_t hx711_get_offset(void);
void hx711_power_down(void);
void hx711_power_up(void);

#endif
