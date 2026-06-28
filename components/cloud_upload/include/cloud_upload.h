#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLOUD_UPLOAD_MAX_INGREDIENTS 4
#define CLOUD_UPLOAD_INGREDIENT_NAME_SIZE 24
#define CLOUD_UPLOAD_METHOD_NAME_SIZE 16

typedef struct {
    size_t ingredient_count;
    char ingredients[CLOUD_UPLOAD_MAX_INGREDIENTS][CLOUD_UPLOAD_INGREDIENT_NAME_SIZE];
    float raw_weights_g[CLOUD_UPLOAD_MAX_INGREDIENTS];
    char cooking_method[CLOUD_UPLOAD_METHOD_NAME_SIZE];
    float cooked_weight_g;
    float cooked_energy_kcal;
    float cooked_protein_g;
    float cooked_fat_g;
    float cooked_carbohydrate_g;
    float cooked_sodium_mg;
    float cooked_cholesterol_mg;
    float cooked_vitamin_c_mg;
    float cooked_calcium_mg;
    float cooked_iron_mg;
    float cooked_potassium_mg;
    int cooking_time_minutes;
} cloud_meal_record_t;

esp_err_t cloud_upload_start(void);
bool cloud_upload_submit(const cloud_meal_record_t *record);

#ifdef __cplusplus
}
#endif
