#include "nutricook_inference.hpp"

#include "nutrition_app.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(nutricook_ui_font_16);

namespace {

constexpr const char *TAG = "nutrition_ui";
constexpr size_t kInputBufferSize = 160;
constexpr uint32_t kScreenW = 480;
constexpr uint32_t kScreenH = 640;
constexpr size_t kMealRecordCapacity = 5;

struct IngredientState {
    nutricook::IngredientWeight items[nutricook::kMaxIngredientsPerDish];
    size_t count;
};

enum class Page : uint8_t {
    Scale = 0,
    Method = 1,
    Result = 2,
};

enum class InferenceState {
    Idle,
    Running,
    Done,
    Failed,
};

struct CloudNutritionRecord {
    IngredientState input;
    nutricook::CookingMethod method;
    nutricook::Prediction prediction;
    bool valid;
};

struct MealRecord {
    CloudNutritionRecord record;
    int64_t started_at_us;
    int64_t updated_at_us;
    uint32_t revision;
    bool valid;
    bool closed;
};

struct MethodOption {
    const char *name_cn;
    nutricook::CookingMethod method;
    lv_obj_t *btn;
    lv_obj_t *label;
};

MethodOption s_methods[] = {
    {"不烹饪", nutricook::CookingMethod::Raw, nullptr, nullptr},
    {"水煮", nutricook::CookingMethod::Boil, nullptr, nullptr},
    {"红烧", nutricook::CookingMethod::Braise, nullptr, nullptr},
    {"油炸", nutricook::CookingMethod::DeepFry, nullptr, nullptr},
    {"煎制", nutricook::CookingMethod::PanFry, nullptr, nullptr},
    {"烤制", nutricook::CookingMethod::Roast, nullptr, nullptr},
    {"清蒸", nutricook::CookingMethod::Steam, nullptr, nullptr},
    {"炒制", nutricook::CookingMethod::StirFry, nullptr, nullptr},
};

SemaphoreHandle_t s_mutex = nullptr;
IngredientState s_ingredients = {};
nutricook::CookingMethod s_selected_method = nutricook::CookingMethod::Raw;
InferenceState s_inference_state = InferenceState::Idle;
CloudNutritionRecord s_cloud_record = {};
bool s_prediction_dirty = true;
bool s_meal_confirmed = false;
bool s_pending_meal_commit = false;
uint32_t s_input_revision = 0;
uint32_t s_meal_revision = 0;
int s_active_meal_slot = -1;
size_t s_meal_record_count = 0;
MealRecord s_meal_records[kMealRecordCapacity] = {};
nutrition_meal_finalized_cb_t s_meal_finalized_callback = nullptr;
void *s_meal_finalized_user_ctx = nullptr;
Page s_page = Page::Scale;

lv_obj_t *s_pages[3] = {};
lv_obj_t *s_dots[3] = {};
lv_obj_t *s_food_name_labels[nutricook::kMaxIngredientsPerDish] = {};
lv_obj_t *s_food_weight_labels[nutricook::kMaxIngredientsPerDish] = {};
lv_obj_t *s_total_weight_label = nullptr;
lv_obj_t *s_method_status_label = nullptr;
lv_obj_t *s_result_title = nullptr;
lv_obj_t *s_energy_label = nullptr;
lv_obj_t *s_weight_label = nullptr;
lv_obj_t *s_macro_center_label = nullptr;
lv_obj_t *s_protein_amount_label = nullptr;
lv_obj_t *s_protein_pct_label = nullptr;
lv_obj_t *s_fat_amount_label = nullptr;
lv_obj_t *s_fat_pct_label = nullptr;
lv_obj_t *s_carb_amount_label = nullptr;
lv_obj_t *s_carb_pct_label = nullptr;
lv_obj_t *s_arc_protein = nullptr;
lv_obj_t *s_arc_fat = nullptr;
lv_obj_t *s_arc_carb = nullptr;

const lv_color_t kBg = lv_color_hex(0xF7F8FA);
const lv_color_t kPanel = lv_color_hex(0xFFFFFF);
const lv_color_t kLine = lv_color_hex(0xDDE4EC);
const lv_color_t kText = lv_color_hex(0x101828);
const lv_color_t kMuted = lv_color_hex(0x667085);
const lv_color_t kBlue = lv_color_hex(0x1D4ED8);
const lv_color_t kGreen = lv_color_hex(0x159A5B);
const lv_color_t kAmber = lv_color_hex(0xD89A00);
const lv_color_t kRose = lv_color_hex(0xC2185B);

const lv_font_t *font_cn()
{
    return &nutricook_ui_font_16;
}

const lv_font_t *font_num_big()
{
    return &lv_font_montserrat_32;
}

const lv_font_t *font_num_mid()
{
    return &lv_font_montserrat_22;
}

void lock_state()
{
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

void unlock_state()
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

void set_default_ingredients()
{
    s_ingredients.count = 3;
    s_ingredients.items[0] = {nutricook::Ingredient::Chicken, 150.0f};
    s_ingredients.items[1] = {nutricook::Ingredient::Potato, 120.0f};
    s_ingredients.items[2] = {nutricook::Ingredient::Carrot, 60.0f};
}

void clear_ingredients()
{
    memset(&s_ingredients, 0, sizeof(s_ingredients));
}

bool ingredient_from_name(const char *name, nutricook::Ingredient *ingredient)
{
    for (int i = 0; i < static_cast<int>(nutricook::Ingredient::Count); ++i) {
        const auto candidate = static_cast<nutricook::Ingredient>(i);
        if (strcmp(name, nutricook::ingredient_name(candidate)) == 0) {
            *ingredient = candidate;
            return true;
        }
    }
    return false;
}

void normalize_token(char *token)
{
    char *read = token;
    char *write = token;
    while (*read) {
        if (*read != ' ' && *read != '\t') {
            *write++ = static_cast<char>(tolower(static_cast<unsigned char>(*read)));
        }
        ++read;
    }
    *write = '\0';
}

bool parse_ingredient_line(char *line, IngredientState *out)
{
    IngredientState parsed = {};
    char *saveptr = nullptr;
    for (char *part = strtok_r(line, ",;", &saveptr); part != nullptr;
         part = strtok_r(nullptr, ",;", &saveptr)) {
        while (*part == ' ' || *part == '\t') {
            ++part;
        }
        char *sep = strpbrk(part, ":= ");
        if (sep == nullptr) {
            return false;
        }

        *sep++ = '\0';
        while (*sep == ' ' || *sep == '\t' || *sep == ':' || *sep == '=') {
            ++sep;
        }

        normalize_token(part);
        nutricook::Ingredient ingredient = nutricook::Ingredient::Apple;
        if (!ingredient_from_name(part, &ingredient)) {
            return false;
        }

        const float weight = strtof(sep, nullptr);
        if (weight <= 0.0f || parsed.count >= nutricook::kMaxIngredientsPerDish) {
            return false;
        }
        parsed.items[parsed.count++] = {ingredient, weight};
    }

    if (parsed.count == 0) {
        return false;
    }
    *out = parsed;
    return true;
}

const char *method_name_cn(nutricook::CookingMethod method);

float total_weight(const IngredientState &state)
{
    float total = 0.0f;
    for (size_t i = 0; i < state.count; ++i) {
        total += state.items[i].raw_weight_g;
    }
    return total;
}

int allocate_meal_slot_locked()
{
    const int slot = static_cast<int>(s_meal_revision % kMealRecordCapacity);
    ++s_meal_revision;
    if (s_meal_record_count < kMealRecordCapacity) {
        ++s_meal_record_count;
    }
    memset(&s_meal_records[slot], 0, sizeof(s_meal_records[slot]));
    s_meal_records[slot].started_at_us = esp_timer_get_time();
    s_meal_records[slot].updated_at_us = s_meal_records[slot].started_at_us;
    s_meal_records[slot].revision = 1;
    s_active_meal_slot = slot;
    return slot;
}

void commit_current_meal_locked(const CloudNutritionRecord &record)
{
    if (record.input.count == 0 || !record.valid) {
        return;
    }

    int slot = s_active_meal_slot;
    if (slot < 0 || slot >= static_cast<int>(kMealRecordCapacity)) {
        slot = allocate_meal_slot_locked();
    }

    MealRecord &meal = s_meal_records[slot];
    meal.record = record;
    meal.updated_at_us = esp_timer_get_time();
    ++meal.revision;
    meal.valid = true;
    meal.closed = false;
    s_meal_confirmed = true;
    s_pending_meal_commit = false;

    ESP_LOGI(TAG,
             "meal slot %d saved: revision=%" PRIu32 ", foods=%u, method=%s, energy=%.1f kcal",
             slot,
             meal.revision,
             static_cast<unsigned>(record.input.count),
             method_name_cn(record.method),
             static_cast<double>(record.prediction.cooked_energy_kcal));
}

bool close_current_meal_locked(CloudNutritionRecord *finalized_record)
{
    if (s_active_meal_slot >= 0 && s_active_meal_slot < static_cast<int>(kMealRecordCapacity)) {
        MealRecord &meal = s_meal_records[s_active_meal_slot];
        if (meal.valid && !meal.closed) {
            meal.closed = true;
            meal.updated_at_us = esp_timer_get_time();
            if (finalized_record != nullptr) {
                *finalized_record = meal.record;
            }
            ESP_LOGI(TAG, "meal slot %d closed", s_active_meal_slot);
            s_active_meal_slot = -1;
            s_meal_confirmed = false;
            s_pending_meal_commit = false;
            return true;
        }
    }

    s_active_meal_slot = -1;
    s_meal_confirmed = false;
    s_pending_meal_commit = false;
    return false;
}

void notify_meal_finalized(const CloudNutritionRecord &record)
{
    nutrition_meal_finalized_cb_t callback = nullptr;
    void *user_ctx = nullptr;
    lock_state();
    callback = s_meal_finalized_callback;
    user_ctx = s_meal_finalized_user_ctx;
    unlock_state();
    if (callback == nullptr || !record.valid) {
        return;
    }

    nutrition_finalized_meal_t meal = {};
    meal.ingredient_count = record.input.count;
    for (size_t i = 0; i < record.input.count; ++i) {
        snprintf(meal.ingredients[i], sizeof(meal.ingredients[i]), "%s",
                 nutricook::ingredient_name(record.input.items[i].ingredient));
        meal.raw_weights_g[i] = record.input.items[i].raw_weight_g;
    }
    snprintf(meal.cooking_method, sizeof(meal.cooking_method), "%s",
             nutricook::cooking_method_name(record.method));
    meal.outputs[0] = record.prediction.cooked_weight_g;
    meal.outputs[1] = record.prediction.cooked_energy_kcal;
    meal.outputs[2] = record.prediction.cooked_protein_g;
    meal.outputs[3] = record.prediction.cooked_fat_g;
    meal.outputs[4] = record.prediction.cooked_carbohydrate_g;
    meal.outputs[5] = record.prediction.cooked_sodium_mg;
    meal.outputs[6] = record.prediction.cooked_cholesterol_mg;
    meal.outputs[7] = record.prediction.cooked_vitamin_c_mg;
    meal.outputs[8] = record.prediction.cooked_calcium_mg;
    meal.outputs[9] = record.prediction.cooked_iron_mg;
    meal.outputs[10] = record.prediction.cooked_potassium_mg;
    callback(&meal, user_ctx);
}

void format_one_decimal(float value, char *buffer, size_t buffer_size)
{
    int scaled = static_cast<int>(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    const char *sign = "";
    if (scaled < 0) {
        sign = "-";
        scaled = -scaled;
    }
    snprintf(buffer, buffer_size, "%s%d.%d", sign, scaled / 10, scaled % 10);
}

lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

void style_page(lv_obj_t *page)
{
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(page, kBg, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
}

void style_panel(lv_obj_t *obj)
{
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_bg_color(obj, kPanel, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, kLine, 0);
    lv_obj_set_style_shadow_width(obj, 8, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_10, 0);
    lv_obj_set_style_shadow_ofs_y(obj, 2, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void style_plain(lv_obj_t *obj)
{
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

const char *method_name_cn(nutricook::CookingMethod method)
{
    for (const auto &option : s_methods) {
        if (option.method == method) {
            return option.name_cn;
        }
    }
    return "未知";
}

void show_page(Page page);
void start_inference_if_needed();

void serial_input_task(void *)
{
    ESP_LOGI(TAG, "serial input format: chicken:150,potato:120,carrot:60");
    char buffer[kInputBufferSize] = {};
    size_t len = 0;
    while (true) {
        const int ch = getchar();
        if (ch == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (len > 0) {
                buffer[len] = '\0';
                IngredientState parsed = {};
                if (parse_ingredient_line(buffer, &parsed)) {
                    lock_state();
                    s_ingredients = parsed;
                    ++s_input_revision;
                    s_prediction_dirty = true;
                    if (s_meal_confirmed) {
                        s_pending_meal_commit = true;
                    }
                    if (s_inference_state != InferenceState::Running) {
                        s_inference_state = InferenceState::Idle;
                    }
                    unlock_state();
                    ESP_LOGI(TAG, "updated ingredients from serial");
                } else {
                    ESP_LOGW(TAG, "bad ingredient line");
                }
                len = 0;
            }
        } else if (len + 1 < sizeof(buffer)) {
            buffer[len++] = static_cast<char>(ch);
        }
    }
}

void set_arc_segment(lv_obj_t *arc, int start, int end, lv_color_t color)
{
    lv_arc_set_bg_angles(arc, start, end);
    lv_arc_set_angles(arc, start, end);
    lv_obj_set_style_arc_width(arc, 26, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 26, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
}

void update_method_styles()
{
    for (auto &option : s_methods) {
        const bool selected = option.method == s_selected_method;
        lv_obj_set_style_bg_color(option.btn, selected ? kText : kPanel, 0);
        lv_obj_set_style_border_color(option.btn, selected ? kText : kLine, 0);
        lv_obj_set_style_border_width(option.btn, selected ? 2 : 1, 0);
        if (option.label) {
            lv_obj_set_style_text_color(option.label, selected ? lv_color_hex(0xFFFFFF) : kText, 0);
        }
    }
}

void confirm_current_meal()
{
    bool should_start = false;
    lock_state();
    if (s_ingredients.count > 0) {
        s_meal_confirmed = true;
        s_pending_meal_commit = true;
        s_prediction_dirty = true;
        ++s_input_revision;
        if (s_inference_state != InferenceState::Running) {
            s_inference_state = InferenceState::Idle;
            should_start = true;
        }
    }
    unlock_state();

    if (should_start) {
        start_inference_if_needed();
    }
}

void start_inference_if_needed()
{
    bool should_start = false;
    lock_state();
    if (s_prediction_dirty && s_inference_state != InferenceState::Running && s_ingredients.count > 0) {
        s_inference_state = InferenceState::Running;
        should_start = true;
    }
    unlock_state();

    if (should_start) {
        xTaskCreate(
            [](void *) {
                IngredientState input = {};
                nutricook::CookingMethod method = nutricook::CookingMethod::Raw;
                uint32_t revision = 0;
                lock_state();
                input = s_ingredients;
                method = s_selected_method;
                revision = s_input_revision;
                unlock_state();

                nutricook::Prediction prediction = {};
                const bool ok = nutricook::predict(input.items, input.count, method, &prediction);

                lock_state();
                if (ok && revision == s_input_revision) {
                    CloudNutritionRecord record = {input, method, prediction, true};
                    s_cloud_record = record;
                    s_prediction_dirty = false;
                    s_inference_state = InferenceState::Done;
                    if (s_pending_meal_commit || s_meal_confirmed) {
                        commit_current_meal_locked(record);
                    }
                } else if (ok) {
                    s_inference_state = InferenceState::Idle;
                    s_prediction_dirty = true;
                } else {
                    s_inference_state = InferenceState::Failed;
                }
                unlock_state();
                vTaskDelete(nullptr);
            },
            "nutrition_infer",
            12288,
            nullptr,
            5,
            nullptr);
    }
}

void show_page(Page page)
{
    s_page = page;
    for (int i = 0; i < 3; ++i) {
        if (i == static_cast<int>(page)) {
            lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_style_bg_color(s_dots[i], i == static_cast<int>(page) ? kBlue : lv_color_hex(0xCBD5E1), 0);
    }

    if (page == Page::Result) {
        start_inference_if_needed();
    }
}

void go_next_page()
{
    const Page from = s_page;
    const int next = (static_cast<int>(s_page) + 1) % 3;
    if (from == Page::Method && static_cast<Page>(next) == Page::Result) {
        confirm_current_meal();
    }
    show_page(static_cast<Page>(next));
}

void go_prev_page()
{
    const int prev = (static_cast<int>(s_page) + 2) % 3;
    show_page(static_cast<Page>(prev));
}

void gesture_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT) {
        go_next_page();
    } else if (dir == LV_DIR_RIGHT) {
        go_prev_page();
    }
}

void next_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        go_next_page();
    }
}

void method_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    MethodOption *option = static_cast<MethodOption *>(lv_event_get_user_data(e));
    lock_state();
    s_selected_method = option->method;
    ++s_input_revision;
    s_prediction_dirty = true;
    if (s_meal_confirmed) {
        s_pending_meal_commit = true;
    }
    if (s_inference_state != InferenceState::Running) {
        s_inference_state = InferenceState::Idle;
    }
    unlock_state();
    update_method_styles();
    if (s_meal_confirmed) {
        start_inference_if_needed();
    }
}

lv_obj_t *make_action_button(lv_obj_t *parent, const char *text, lv_color_t bg)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 180, 52);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_shadow_width(btn, 10, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 4, 0);
    lv_obj_t *label = make_label(btn, text, font_cn(), lv_color_hex(0xFFFFFF));
    lv_obj_center(label);
    return btn;
}

void create_header(lv_obj_t *page, const char *title, const char *subtitle)
{
    lv_obj_t *label = make_label(page, title, font_cn(), kText);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 28, 22);

    lv_obj_t *sub = make_label(page, subtitle, font_cn(), kMuted);
    lv_obj_set_width(sub, 388);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 28, 50);
}

void create_page_dots(lv_obj_t *page)
{
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *dot = lv_obj_create(page);
        lv_obj_set_size(dot, 34, 6);
        lv_obj_set_style_radius(dot, 3, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xCBD5E1), 0);
        lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, (i - 1) * 42, -18);
        s_dots[i] = dot;
    }
}

void create_scale_page(lv_obj_t *scr)
{
    lv_obj_t *page = lv_obj_create(scr);
    s_pages[static_cast<int>(Page::Scale)] = page;
    style_page(page);
    create_header(page, "称重信息", "当前秤上食材");

    lv_obj_t *weight_card = lv_obj_create(page);
    lv_obj_set_size(weight_card, 424, 154);
    lv_obj_align(weight_card, LV_ALIGN_TOP_MID, 0, 94);
    style_panel(weight_card);

    lv_obj_t *caption = make_label(weight_card, "总重量", font_cn(), kMuted);
    lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 22, 18);

    s_total_weight_label = make_label(weight_card, "-- g", font_num_big(), kText);
    lv_obj_align(s_total_weight_label, LV_ALIGN_LEFT_MID, 22, 18);

    lv_obj_t *unit = make_label(weight_card, "最多 4 种食材", font_cn(), kBlue);
    lv_obj_align(unit, LV_ALIGN_RIGHT_MID, -22, 20);

    lv_obj_t *list_card = lv_obj_create(page);
    lv_obj_set_size(list_card, 424, 238);
    lv_obj_align(list_card, LV_ALIGN_TOP_MID, 0, 272);
    style_panel(list_card);
    lv_obj_set_style_pad_all(list_card, 22, 0);

    lv_obj_t *list_title = make_label(list_card, "秤上食材", font_cn(), kText);
    lv_obj_align(list_title, LV_ALIGN_TOP_LEFT, 0, 0);

    for (size_t i = 0; i < nutricook::kMaxIngredientsPerDish; ++i) {
        lv_obj_t *row = lv_obj_create(list_card);
        lv_obj_set_size(row, 380, 34);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, 42 + static_cast<int>(i) * 40);
        style_plain(row);

        s_food_name_labels[i] = make_label(row, "--", font_cn(), kText);
        lv_obj_set_width(s_food_name_labels[i], 230);
        lv_obj_align(s_food_name_labels[i], LV_ALIGN_LEFT_MID, 0, 0);

        s_food_weight_labels[i] = make_label(row, "-- g", font_num_mid(), kText);
        lv_obj_set_width(s_food_weight_labels[i], 120);
        lv_obj_set_style_text_align(s_food_weight_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(s_food_weight_labels[i], LV_ALIGN_RIGHT_MID, 0, 0);
    }

    lv_obj_t *next = make_action_button(page, "选择烹饪", kBlue);
    lv_obj_align(next, LV_ALIGN_BOTTOM_MID, 0, -56);
    lv_obj_add_event_cb(next, next_event_cb, LV_EVENT_CLICKED, nullptr);
}

lv_obj_t *make_method_button(lv_obj_t *parent, MethodOption *option)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 202, 68);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 6, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_10, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 2, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, kLine, 0);
    lv_obj_set_style_bg_color(btn, kPanel, 0);
    lv_obj_add_event_cb(btn, method_event_cb, LV_EVENT_CLICKED, option);

    option->label = make_label(btn, option->name_cn, font_cn(), kText);
    lv_obj_center(option->label);
    return btn;
}

void create_method_page(lv_obj_t *scr)
{
    lv_obj_t *page = lv_obj_create(scr);
    s_pages[static_cast<int>(Page::Method)] = page;
    style_page(page);
    create_header(page, "烹饪方式", "选择当前食材的处理方式");

    lv_obj_t *grid = lv_obj_create(page);
    lv_obj_set_size(grid, 424, 320);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 104);
    style_plain(grid);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);

    static int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t rows[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, cols, rows);

    for (size_t i = 0; i < sizeof(s_methods) / sizeof(s_methods[0]); ++i) {
        s_methods[i].btn = make_method_button(grid, &s_methods[i]);
        lv_obj_set_grid_cell(s_methods[i].btn,
                             LV_GRID_ALIGN_STRETCH,
                             static_cast<int>(i % 2),
                             1,
                             LV_GRID_ALIGN_STRETCH,
                             static_cast<int>(i / 2),
                             1);
    }

    s_method_status_label = make_label(page, "当前选择  不烹饪", font_cn(), kMuted);
    lv_obj_set_width(s_method_status_label, 424);
    lv_obj_align(s_method_status_label, LV_ALIGN_TOP_MID, 0, 462);

    lv_obj_t *next = make_action_button(page, "查看结果", kGreen);
    lv_obj_align(next, LV_ALIGN_BOTTOM_MID, 0, -56);
    lv_obj_add_event_cb(next, next_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    update_method_styles();
}

void make_macro_row(lv_obj_t *parent,
                    int y,
                    const char *title,
                    lv_color_t color,
                    lv_obj_t **amount_label,
                    lv_obj_t **pct_label)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, 384, 36);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    style_plain(row);

    lv_obj_t *swatch = lv_obj_create(row);
    lv_obj_set_size(swatch, 10, 10);
    lv_obj_set_style_radius(swatch, 5, 0);
    lv_obj_set_style_bg_color(swatch, color, 0);
    lv_obj_set_style_border_width(swatch, 0, 0);
    lv_obj_align(swatch, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *name = make_label(row, title, font_cn(), kText);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 22, 0);

    *amount_label = make_label(row, "-- g", font_num_mid(), kText);
    lv_obj_set_width(*amount_label, 96);
    lv_obj_set_style_text_align(*amount_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(*amount_label, LV_ALIGN_RIGHT_MID, -74, 0);

    *pct_label = make_label(row, "--%", font_num_mid(), kMuted);
    lv_obj_set_width(*pct_label, 58);
    lv_obj_set_style_text_align(*pct_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(*pct_label, LV_ALIGN_RIGHT_MID, 0, 0);
}

void create_result_page(lv_obj_t *scr)
{
    lv_obj_t *page = lv_obj_create(scr);
    s_pages[static_cast<int>(Page::Result)] = page;
    style_page(page);
    create_header(page, "营养结果", "热量与主要营养构成");

    s_result_title = make_label(page, "等待计算", font_cn(), kMuted);
    lv_obj_set_width(s_result_title, 424);
    lv_obj_align(s_result_title, LV_ALIGN_TOP_MID, 0, 76);

    lv_obj_t *energy_card = lv_obj_create(page);
    lv_obj_set_size(energy_card, 424, 94);
    lv_obj_align(energy_card, LV_ALIGN_TOP_MID, 0, 108);
    style_panel(energy_card);

    lv_obj_t *energy_caption = make_label(energy_card, "热量", font_cn(), kMuted);
    lv_obj_align(energy_caption, LV_ALIGN_TOP_LEFT, 18, 14);

    s_energy_label = make_label(energy_card, "-- kcal", font_num_big(), kText);
    lv_obj_align(s_energy_label, LV_ALIGN_LEFT_MID, 18, 18);

    s_weight_label = make_label(energy_card, "成品 -- g", font_cn(), kBlue);
    lv_obj_align(s_weight_label, LV_ALIGN_RIGHT_MID, -18, 16);

    lv_obj_t *chart_box = lv_obj_create(page);
    lv_obj_set_size(chart_box, 206, 206);
    lv_obj_align(chart_box, LV_ALIGN_TOP_MID, 0, 216);
    style_plain(chart_box);

    s_arc_protein = lv_arc_create(chart_box);
    s_arc_fat = lv_arc_create(chart_box);
    s_arc_carb = lv_arc_create(chart_box);
    lv_obj_set_size(s_arc_protein, 198, 198);
    lv_obj_set_size(s_arc_fat, 198, 198);
    lv_obj_set_size(s_arc_carb, 198, 198);
    lv_obj_center(s_arc_protein);
    lv_obj_center(s_arc_fat);
    lv_obj_center(s_arc_carb);
    set_arc_segment(s_arc_protein, 0, 120, kGreen);
    set_arc_segment(s_arc_fat, 120, 240, kAmber);
    set_arc_segment(s_arc_carb, 240, 360, kRose);

    s_macro_center_label = make_label(chart_box, "供能\n占比", font_cn(), kText);
    lv_obj_set_style_text_align(s_macro_center_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_macro_center_label);

    lv_obj_t *macro_panel = lv_obj_create(page);
    lv_obj_set_size(macro_panel, 424, 136);
    lv_obj_align(macro_panel, LV_ALIGN_TOP_MID, 0, 444);
    style_panel(macro_panel);

    make_macro_row(macro_panel, 12, "蛋白质", kGreen, &s_protein_amount_label, &s_protein_pct_label);
    make_macro_row(macro_panel, 50, "脂肪", kAmber, &s_fat_amount_label, &s_fat_pct_label);
    make_macro_row(macro_panel, 88, "碳水", kRose, &s_carb_amount_label, &s_carb_pct_label);

    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
}

void update_scale_page(const IngredientState &ingredients)
{
    for (size_t i = 0; i < nutricook::kMaxIngredientsPerDish; ++i) {
        if (i < ingredients.count) {
            char weight[32] = {};
            snprintf(weight, sizeof(weight), "%.0f g", static_cast<double>(ingredients.items[i].raw_weight_g));
            lv_label_set_text(s_food_name_labels[i], nutricook::ingredient_name(ingredients.items[i].ingredient));
            lv_label_set_text(s_food_weight_labels[i], weight);
            lv_obj_clear_flag(s_food_name_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_food_weight_labels[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_food_name_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_food_weight_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    char weight[32] = {};
    snprintf(weight, sizeof(weight), "%.0f g", static_cast<double>(total_weight(ingredients)));
    lv_label_set_text(s_total_weight_label, weight);
}

void update_result_page(const CloudNutritionRecord &record)
{
    const float protein_kcal = record.prediction.cooked_protein_g * 4.0f;
    const float fat_kcal = record.prediction.cooked_fat_g * 9.0f;
    const float carb_kcal = record.prediction.cooked_carbohydrate_g * 4.0f;
    float total = protein_kcal + fat_kcal + carb_kcal;
    if (total <= 0.0f) {
        total = 1.0f;
    }

    const int protein_angle = static_cast<int>(360.0f * protein_kcal / total);
    const int fat_angle = static_cast<int>(360.0f * fat_kcal / total);
    const int carb_angle = 360 - protein_angle - fat_angle;
    const float protein_pct = protein_kcal * 100.0f / total;
    const float fat_pct = fat_kcal * 100.0f / total;
    const float carb_pct = carb_kcal * 100.0f / total;

    int start = 0;
    set_arc_segment(s_arc_protein, start, start + protein_angle, kGreen);
    start += protein_angle;
    set_arc_segment(s_arc_fat, start, start + fat_angle, kAmber);
    start += fat_angle;
    set_arc_segment(s_arc_carb, start, start + carb_angle, kRose);

    char text[192] = {};
    snprintf(text, sizeof(text), "%s方案", method_name_cn(record.method));
    lv_label_set_text(s_result_title, text);

    snprintf(text, sizeof(text), "%d kcal", static_cast<int>(record.prediction.cooked_energy_kcal + 0.5f));
    lv_label_set_text(s_energy_label, text);

    char amount[24] = {};
    format_one_decimal(record.prediction.cooked_weight_g, amount, sizeof(amount));
    snprintf(text, sizeof(text), "成品 %s g", amount);
    lv_label_set_text(s_weight_label, text);

    format_one_decimal(record.prediction.cooked_protein_g, amount, sizeof(amount));
    snprintf(text, sizeof(text), "%s g", amount);
    lv_label_set_text(s_protein_amount_label, text);
    snprintf(text, sizeof(text), "%d%%", static_cast<int>(protein_pct + 0.5f));
    lv_label_set_text(s_protein_pct_label, text);

    format_one_decimal(record.prediction.cooked_fat_g, amount, sizeof(amount));
    snprintf(text, sizeof(text), "%s g", amount);
    lv_label_set_text(s_fat_amount_label, text);
    snprintf(text, sizeof(text), "%d%%", static_cast<int>(fat_pct + 0.5f));
    lv_label_set_text(s_fat_pct_label, text);

    format_one_decimal(record.prediction.cooked_carbohydrate_g, amount, sizeof(amount));
    snprintf(text, sizeof(text), "%s g", amount);
    lv_label_set_text(s_carb_amount_label, text);
    snprintf(text, sizeof(text), "%d%%", static_cast<int>(carb_pct + 0.5f));
    lv_label_set_text(s_carb_pct_label, text);
}

void clear_result_page()
{
    lv_label_set_text(s_result_title, "等待餐食");
    lv_label_set_text(s_energy_label, "-- kcal");
    lv_label_set_text(s_weight_label, "成品 -- g");
    lv_label_set_text(s_protein_amount_label, "-- g");
    lv_label_set_text(s_protein_pct_label, "--%");
    lv_label_set_text(s_fat_amount_label, "-- g");
    lv_label_set_text(s_fat_pct_label, "--%");
    lv_label_set_text(s_carb_amount_label, "-- g");
    lv_label_set_text(s_carb_pct_label, "--%");
    set_arc_segment(s_arc_protein, 0, 120, kGreen);
    set_arc_segment(s_arc_fat, 120, 240, kAmber);
    set_arc_segment(s_arc_carb, 240, 360, kRose);
}

void ui_timer_cb(lv_timer_t *)
{
    IngredientState ingredients = {};
    InferenceState state = InferenceState::Idle;
    CloudNutritionRecord record = {};
    bool dirty = false;
    bool meal_confirmed = false;
    nutricook::CookingMethod method = nutricook::CookingMethod::Raw;

    lock_state();
    ingredients = s_ingredients;
    state = s_inference_state;
    record = s_cloud_record;
    dirty = s_prediction_dirty;
    meal_confirmed = s_meal_confirmed;
    method = s_selected_method;
    unlock_state();

    update_scale_page(ingredients);

    char text[96] = {};
    snprintf(text, sizeof(text), "当前选择  %s", method_name_cn(method));
    lv_label_set_text(s_method_status_label, text);

    if ((s_page == Page::Result || meal_confirmed) && dirty && state != InferenceState::Running &&
        ingredients.count > 0) {
        start_inference_if_needed();
    }

    if (ingredients.count == 0) {
        clear_result_page();
    } else if (record.valid && state != InferenceState::Running && !dirty) {
        update_result_page(record);
    } else if (state == InferenceState::Running) {
        lv_label_set_text(s_result_title, "正在计算");
    } else if (state == InferenceState::Failed) {
        lv_label_set_text(s_result_title, "计算失败");
    } else if (dirty) {
        lv_label_set_text(s_result_title, "等待计算");
    }
}

} // namespace

extern "C" void nutrition_lvgl_ui(lv_display_t *disp)
{
    LV_UNUSED(disp);
    s_mutex = xSemaphoreCreateMutex();
    set_default_ingredients();

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_size(scr, kScreenW, kScreenH);
    lv_obj_set_style_bg_color(scr, kBg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, gesture_event_cb, LV_EVENT_GESTURE, nullptr);

    create_scale_page(scr);
    create_method_page(scr);
    create_result_page(scr);
    create_page_dots(scr);
    show_page(Page::Scale);

    lv_timer_create(ui_timer_cb, 250, nullptr);
    xTaskCreate(serial_input_task, "nutrition_serial", 4096, nullptr, 4, nullptr);
}

extern "C" bool nutrition_update_ingredients_from_names(const char *const names[],
                                                        const float weights_g[],
                                                        size_t count)
{
    if (count == 0) {
        CloudNutritionRecord finalized_record = {};
        bool meal_finalized = false;
        lock_state();
        clear_ingredients();
        ++s_input_revision;
        s_prediction_dirty = false;
        s_inference_state = InferenceState::Idle;
        s_cloud_record.valid = false;
        meal_finalized = close_current_meal_locked(&finalized_record);
        unlock_state();

        if (meal_finalized) {
            notify_meal_finalized(finalized_record);
        }

        ESP_LOGI(TAG, "updated ingredients from perception: empty scale, current meal ended");
        return true;
    }

    if (names == nullptr || weights_g == nullptr || count > nutricook::kMaxIngredientsPerDish) {
        return false;
    }

    IngredientState parsed = {};
    for (size_t i = 0; i < count; ++i) {
        if (names[i] == nullptr || weights_g[i] <= 0.0f) {
            return false;
        }

        char name[32] = {};
        snprintf(name, sizeof(name), "%s", names[i]);
        normalize_token(name);

        nutricook::Ingredient ingredient = nutricook::Ingredient::Apple;
        if (!ingredient_from_name(name, &ingredient)) {
            ESP_LOGW(TAG, "unsupported perception ingredient '%s'; fallback to apple for UI/model update", name);
            ingredient = nutricook::Ingredient::Apple;
        }

        parsed.items[parsed.count++] = {ingredient, weights_g[i]};
    }

    lock_state();
    s_ingredients = parsed;
    ++s_input_revision;
    s_prediction_dirty = true;
    const bool should_refresh_meal = s_meal_confirmed;
    if (should_refresh_meal) {
        s_pending_meal_commit = true;
    }
    if (s_inference_state != InferenceState::Running) {
        s_inference_state = InferenceState::Idle;
    }
    unlock_state();

    ESP_LOGI(TAG, "updated ingredients from perception: %u item(s)", static_cast<unsigned>(parsed.count));
    if (should_refresh_meal) {
        start_inference_if_needed();
    }
    return true;
}

extern "C" bool nutrition_copy_latest_result(float outputs[nutricook::kOutputCount])
{
    if (outputs == nullptr) {
        return false;
    }

    lock_state();
    const bool valid = s_cloud_record.valid;
    if (valid) {
        outputs[0] = s_cloud_record.prediction.cooked_weight_g;
        outputs[1] = s_cloud_record.prediction.cooked_energy_kcal;
        outputs[2] = s_cloud_record.prediction.cooked_protein_g;
        outputs[3] = s_cloud_record.prediction.cooked_fat_g;
        outputs[4] = s_cloud_record.prediction.cooked_carbohydrate_g;
        outputs[5] = s_cloud_record.prediction.cooked_sodium_mg;
        outputs[6] = s_cloud_record.prediction.cooked_cholesterol_mg;
        outputs[7] = s_cloud_record.prediction.cooked_vitamin_c_mg;
        outputs[8] = s_cloud_record.prediction.cooked_calcium_mg;
        outputs[9] = s_cloud_record.prediction.cooked_iron_mg;
        outputs[10] = s_cloud_record.prediction.cooked_potassium_mg;
    }
    unlock_state();
    return valid;
}

extern "C" void nutrition_set_meal_finalized_callback(nutrition_meal_finalized_cb_t callback,
                                                        void *user_ctx)
{
    lock_state();
    s_meal_finalized_callback = callback;
    s_meal_finalized_user_ctx = user_ctx;
    unlock_state();
}


