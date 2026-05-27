#include "hx711.h"

#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HX711_HIGH 1
#define HX711_LOW 0
#define HX711_CLOCK_DELAY_US 20
#define HX711_TARE_SAMPLES 20

static gpio_num_t s_gpio_pd_sck = GPIO_NUM_23;
static gpio_num_t s_gpio_dout = GPIO_NUM_22;
static hx711_gain_t s_gain = HX711_GAIN_128;
static uint32_t s_offset = 0;
static float s_scale = 380.0f;

void hx711_init(gpio_num_t dout, gpio_num_t pd_sck, hx711_gain_t gain)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 0,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };

    s_gpio_pd_sck = pd_sck;
    s_gpio_dout = dout;

    io_conf.pin_bit_mask = 1ULL << s_gpio_pd_sck;
    gpio_config(&io_conf);

    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << s_gpio_dout;
    gpio_config(&io_conf);

    hx711_set_gain(gain);
}

bool hx711_is_ready(void)
{
    return gpio_get_level(s_gpio_dout) == 0;
}

void hx711_set_gain(hx711_gain_t gain)
{
    s_gain = gain;
    gpio_set_level(s_gpio_pd_sck, HX711_LOW);
    hx711_read();
}

uint32_t hx711_read(void)
{
    uint32_t value = 0;

    gpio_set_level(s_gpio_pd_sck, HX711_LOW);
    while (!hx711_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    portDISABLE_INTERRUPTS();
    for (int i = 0; i < 24; i++) {
        gpio_set_level(s_gpio_pd_sck, HX711_HIGH);
        esp_rom_delay_us(HX711_CLOCK_DELAY_US);
        value <<= 1;
        gpio_set_level(s_gpio_pd_sck, HX711_LOW);
        esp_rom_delay_us(HX711_CLOCK_DELAY_US);

        if (gpio_get_level(s_gpio_dout)) {
            value++;
        }
    }

    for (int i = 0; i < s_gain; i++) {
        gpio_set_level(s_gpio_pd_sck, HX711_HIGH);
        esp_rom_delay_us(HX711_CLOCK_DELAY_US);
        gpio_set_level(s_gpio_pd_sck, HX711_LOW);
        esp_rom_delay_us(HX711_CLOCK_DELAY_US);
    }
    portENABLE_INTERRUPTS();

    value ^= 0x800000;
    return value;
}

uint32_t hx711_read_average(uint8_t times)
{
    uint64_t sum = 0;

    if (times == 0) {
        times = 1;
    }

    for (uint8_t i = 0; i < times; i++) {
        sum += hx711_read();
    }

    return (uint32_t)(sum / times);
}

uint32_t hx711_get_value(uint8_t times)
{
    uint32_t avg = hx711_read_average(times);

    if (avg > s_offset) {
        return avg - s_offset;
    }

    return 0;
}

float hx711_get_units(uint8_t times)
{
    return (float)hx711_get_value(times) / s_scale;
}

void hx711_tare(void)
{
    hx711_set_offset(hx711_read_average(HX711_TARE_SAMPLES));
}

void hx711_set_scale(float scale)
{
    s_scale = (scale == 0.0f) ? 1.0f : scale;
}

float hx711_get_scale(void)
{
    return s_scale;
}

void hx711_set_offset(uint32_t offset)
{
    s_offset = offset;
}

uint32_t hx711_get_offset(void)
{
    return s_offset;
}

void hx711_power_down(void)
{
    gpio_set_level(s_gpio_pd_sck, HX711_LOW);
    esp_rom_delay_us(HX711_CLOCK_DELAY_US);
    gpio_set_level(s_gpio_pd_sck, HX711_HIGH);
    esp_rom_delay_us(HX711_CLOCK_DELAY_US);
}

void hx711_power_up(void)
{
    gpio_set_level(s_gpio_pd_sck, HX711_LOW);
}
