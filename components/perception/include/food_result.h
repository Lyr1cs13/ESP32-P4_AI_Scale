#ifndef FOOD_RESULT_H
#define FOOD_RESULT_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FOOD_RESULT_MAX_CLASS_NAME_LEN 24
#define FOOD_RESULT_MAX_CLASSES 31

typedef struct {
    uint32_t sequence;
    int category;
    char class_name[FOOD_RESULT_MAX_CLASS_NAME_LEN];
    float item_weight_g;
    float total_weight_g;
    float score;
    int64_t timestamp_us;
} food_item_record_t;

typedef struct {
    int category;
    char class_name[FOOD_RESULT_MAX_CLASS_NAME_LEN];
    uint32_t count;
    float total_weight_g;
} food_class_total_t;

esp_err_t ai_submit_latest_frame(float total_weight_g, float item_weight_g);
void food_result_apply_weight_delta(float total_weight_g, float delta_weight_g);
int food_result_get_items(food_item_record_t *items, int max_items);
int food_result_get_class_totals(food_class_total_t *totals, int max_totals);
void food_result_clear(void);

#ifdef __cplusplus
}
#endif

#endif
