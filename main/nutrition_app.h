#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "lvgl.h"

#define NUTRITION_OUTPUT_COUNT 11

#ifdef __cplusplus
extern "C" {
#endif

void nutrition_lvgl_ui(lv_display_t *disp);

bool nutrition_update_ingredients_from_names(const char *const names[],
                                             const float weights_g[],
                                             size_t count);

bool nutrition_copy_latest_result(float outputs[NUTRITION_OUTPUT_COUNT]);

#ifdef __cplusplus
}
#endif
