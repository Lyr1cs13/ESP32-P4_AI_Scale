#pragma once

#include "esp_err.h"
#include "ai_scale_perception.h"

#ifdef __cplusplus
extern "C" {
#endif

void ai_scale_perception_bridge_start(void);
void ai_scale_perception_bridge_notify(void);
void ai_scale_perception_bridge_set_callback(ai_scale_foods_updated_cb_t cb, void *user_ctx);

esp_err_t ai_scale_perception_camera_start_pipeline(void);
void ai_scale_perception_mark_inference_idle(void);

void ai_inference_task(void *arg);

#ifdef __cplusplus
}
#endif
