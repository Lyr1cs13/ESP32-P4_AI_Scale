#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AI_SCALE_PERCEPTION_MAX_FOODS 4

typedef void (*ai_scale_foods_updated_cb_t)(const char *const names[],
                                            const float weights_g[],
                                            size_t count,
                                            void *user_ctx);

typedef struct {
    ai_scale_foods_updated_cb_t foods_updated_cb;
    void *user_ctx;
    bool enable_weight_sensor;
    bool enable_camera_yolo;
} ai_scale_perception_config_t;

esp_err_t ai_scale_perception_prepare_camera(void);
esp_err_t ai_scale_perception_start(const ai_scale_perception_config_t *config);
void ai_scale_perception_clear(void);

#ifdef __cplusplus
}
#endif

