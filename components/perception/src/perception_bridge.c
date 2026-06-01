#include "ai_scale_perception.h"
#include "perception_internal.h"
#include "food_result.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "perception_bridge";

static ai_scale_foods_updated_cb_t s_foods_updated_cb;
static void *s_user_ctx;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_notify;

void ai_scale_perception_bridge_set_callback(ai_scale_foods_updated_cb_t cb, void *user_ctx)
{
    s_foods_updated_cb = cb;
    s_user_ctx = user_ctx;
}

void ai_scale_perception_bridge_notify(void)
{
    if (s_notify) {
        xSemaphoreGive(s_notify);
    }
}

static void bridge_task(void *arg)
{
    (void)arg;
    food_class_total_t totals[FOOD_RESULT_MAX_CLASSES] = {};

    while (true) {
        xSemaphoreTake(s_notify, portMAX_DELAY);

        if (!s_foods_updated_cb) {
            continue;
        }

        const int total_count = food_result_get_class_totals(totals, FOOD_RESULT_MAX_CLASSES);
        if (total_count <= 0) {
            continue;
        }

        const char *names[AI_SCALE_PERCEPTION_MAX_FOODS] = {};
        float weights[AI_SCALE_PERCEPTION_MAX_FOODS] = {};
        size_t out_count = 0;

        for (int i = 0; i < total_count && out_count < AI_SCALE_PERCEPTION_MAX_FOODS; ++i) {
            if (totals[i].total_weight_g <= 0.0f) {
                continue;
            }
            names[out_count] = totals[i].class_name;
            weights[out_count] = totals[i].total_weight_g;
            ++out_count;
        }

        if (out_count > 0) {
            ESP_LOGI(TAG, "publish %u food classes to nutrition UI", (unsigned)out_count);
            s_foods_updated_cb(names, weights, out_count, s_user_ctx);
        }
    }
}

void ai_scale_perception_bridge_start(void)
{
    if (!s_notify) {
        s_notify = xSemaphoreCreateBinary();
    }
    if (!s_task) {
        xTaskCreate(bridge_task, "perception_bridge", 4096, NULL, 3, &s_task);
    }
}

