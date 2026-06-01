#include "ai_scale_perception.h"
#include "perception_internal.h"
#include "food_result.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "perception";
static bool s_started;

esp_err_t ai_scale_perception_prepare_camera(void)
{
    return ai_scale_perception_camera_start_pipeline();
}

esp_err_t ai_scale_perception_start(const ai_scale_perception_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "config is null");

    ai_scale_perception_bridge_set_callback(config->foods_updated_cb, config->user_ctx);
    ai_scale_perception_bridge_start();

    if (!config->enable_camera_yolo) {
        ESP_LOGW(TAG, "camera/YOLO disabled; perception waits for external updates");
        return ESP_OK;
    }

    if (s_started) {
        return ESP_OK;
    }

    s_started = true;
    ESP_LOGI(TAG, "perception pipeline started");
    return ESP_OK;
}

void ai_scale_perception_clear(void)
{
    food_result_clear();
    ai_scale_perception_bridge_notify();
}

