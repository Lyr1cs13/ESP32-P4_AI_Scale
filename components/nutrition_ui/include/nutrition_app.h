#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "lvgl.h"

#define NUTRITION_OUTPUT_COUNT 11
#define NUTRITION_MAX_MEAL_INGREDIENTS 4
#define NUTRITION_INGREDIENT_NAME_SIZE 24
#define NUTRITION_METHOD_NAME_SIZE 16

typedef struct {
    size_t ingredient_count;
    char ingredients[NUTRITION_MAX_MEAL_INGREDIENTS][NUTRITION_INGREDIENT_NAME_SIZE];
    float raw_weights_g[NUTRITION_MAX_MEAL_INGREDIENTS];
    char cooking_method[NUTRITION_METHOD_NAME_SIZE];
    float outputs[NUTRITION_OUTPUT_COUNT];
} nutrition_finalized_meal_t;

typedef void (*nutrition_meal_finalized_cb_t)(const nutrition_finalized_meal_t *meal,
                                              void *user_ctx);

#ifdef __cplusplus
extern "C" {
#endif

void nutrition_lvgl_ui(lv_display_t *disp);

bool nutrition_update_ingredients_from_names(const char *const names[],
                                             const float weights_g[],
                                             size_t count);

bool nutrition_copy_latest_result(float outputs[NUTRITION_OUTPUT_COUNT]);

void nutrition_set_meal_finalized_callback(nutrition_meal_finalized_cb_t callback,
                                            void *user_ctx);

#ifdef __cplusplus
}
#endif
