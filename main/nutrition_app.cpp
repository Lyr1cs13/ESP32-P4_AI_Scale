#include "jiaofu_inference.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr const char *TAG = "nutrition_ui";
constexpr size_t kInputBufferSize = 160;

struct IngredientState {
    jiaofu::IngredientWeight items[jiaofu::kMaxIngredientsPerDish];
    size_t count;
};

enum class InferenceState {
    Idle,
    Running,
    Done,
    Failed,
};

struct CloudNutritionRecord {
    IngredientState input;
    jiaofu::CookingMethod method;
    jiaofu::Prediction prediction;
    bool valid;
};

struct MethodOption {
    const char *name;
    const char *subtitle;
    jiaofu::CookingMethod method;
    lv_obj_t *btn;
};

MethodOption s_methods[] = {
    {"Boil", "Water cooked", jiaofu::CookingMethod::Boil, nullptr},
    {"Braise", "Sauce cooked", jiaofu::CookingMethod::Braise, nullptr},
    {"Deep Fry", "High oil", jiaofu::CookingMethod::DeepFry, nullptr},
    {"Pan Fry", "Hot pan", jiaofu::CookingMethod::PanFry, nullptr},
    {"Roast", "Dry heat", jiaofu::CookingMethod::Roast, nullptr},
    {"Steam", "Low fat", jiaofu::CookingMethod::Steam, nullptr},
    {"Stir Fry", "Quick heat", jiaofu::CookingMethod::StirFry, nullptr},
};

SemaphoreHandle_t s_mutex = nullptr;
IngredientState s_ingredients = {};
jiaofu::CookingMethod s_selected_method = jiaofu::CookingMethod::Boil;
InferenceState s_inference_state = InferenceState::Idle;
CloudNutritionRecord s_cloud_record = {};

lv_obj_t *s_select_page = nullptr;
lv_obj_t *s_result_page = nullptr;
lv_obj_t *s_ingredient_label = nullptr;
lv_obj_t *s_status_label = nullptr;
lv_obj_t *s_result_title = nullptr;
lv_obj_t *s_energy_label = nullptr;
lv_obj_t *s_macro_share_label = nullptr;
lv_obj_t *s_protein_value_label = nullptr;
lv_obj_t *s_fat_value_label = nullptr;
lv_obj_t *s_carb_value_label = nullptr;
lv_obj_t *s_arc_protein = nullptr;
lv_obj_t *s_arc_fat = nullptr;
lv_obj_t *s_arc_carb = nullptr;

const lv_color_t kBg = lv_color_hex(0x07111F);
const lv_color_t kCard = lv_color_hex(0x0D1B2A);
const lv_color_t kBorder = lv_color_hex(0x1F3B5B);
const lv_color_t kText = lv_color_hex(0xEAF3FF);
const lv_color_t kMuted = lv_color_hex(0x8FA9C2);
const lv_color_t kBlue = lv_color_hex(0x4CC9F0);
const lv_color_t kGreen = lv_color_hex(0x37D67A);
const lv_color_t kAmber = lv_color_hex(0xFFB703);
const lv_color_t kViolet = lv_color_hex(0x8B5CF6);

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
    s_ingredients.items[0] = {jiaofu::Ingredient::Chicken, 150.0f};
    s_ingredients.items[1] = {jiaofu::Ingredient::Potato, 120.0f};
    s_ingredients.items[2] = {jiaofu::Ingredient::Carrot, 60.0f};
}

bool ingredient_from_name(const char *name, jiaofu::Ingredient *ingredient)
{
    for (int i = 0; i < static_cast<int>(jiaofu::Ingredient::Count); ++i) {
        const auto candidate = static_cast<jiaofu::Ingredient>(i);
        if (strcmp(name, jiaofu::ingredient_name(candidate)) == 0) {
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
        jiaofu::Ingredient ingredient = jiaofu::Ingredient::Apple;
        if (!ingredient_from_name(part, &ingredient)) {
            return false;
        }

        const float weight = strtof(sep, nullptr);
        if (weight <= 0.0f || parsed.count >= jiaofu::kMaxIngredientsPerDish) {
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

void format_ingredients(const IngredientState &state, char *buffer, size_t buffer_size)
{
    size_t used = 0;
    buffer[0] = '\0';
    for (size_t i = 0; i < state.count; ++i) {
        const int written = snprintf(buffer + used,
                                     buffer_size - used,
                                     "%s%s %.0fg",
                                     i == 0 ? "" : ", ",
                                     jiaofu::ingredient_name(state.items[i].ingredient),
                                     static_cast<double>(state.items[i].raw_weight_g));
        if (written < 0) {
            break;
        }
        used += static_cast<size_t>(written);
        if (used >= buffer_size) {
            buffer[buffer_size - 1] = '\0';
            break;
        }
    }
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
                    s_inference_state = InferenceState::Idle;
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

void style_plain(lv_obj_t *obj)
{
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
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

void update_method_styles()
{
    for (size_t i = 0; i < sizeof(s_methods) / sizeof(s_methods[0]); ++i) {
        const bool selected = s_methods[i].method == s_selected_method;
        lv_obj_t *btn = s_methods[i].btn;
        lv_obj_set_style_bg_color(btn, selected ? lv_color_hex(0x265DFF) : lv_color_hex(0x102437), 0);
        lv_obj_set_style_border_color(btn, selected ? kBlue : kBorder, 0);
        lv_obj_set_style_translate_y(btn, selected ? -3 : 0, 0);
    }
}

void method_event_cb(lv_event_t *e)
{
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    s_selected_method = s_methods[index].method;
    update_method_styles();
}

lv_obj_t *make_method_button(lv_obj_t *parent, size_t index)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 132, 70);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, method_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(index));

    lv_obj_t *name = make_label(btn, s_methods[index].name, &lv_font_montserrat_16, kText);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 10, 9);
    lv_obj_t *sub = make_label(btn, s_methods[index].subtitle, &lv_font_montserrat_14, kMuted);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 10, -8);
    return btn;
}

void set_arc_segment(lv_obj_t *arc, int start, int end, lv_color_t color)
{
    lv_arc_set_bg_angles(arc, start, end);
    lv_arc_set_angles(arc, start, end);
    lv_obj_set_style_arc_width(arc, 30, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 30, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
}

void update_result_page(const CloudNutritionRecord &record)
{
    char text[192] = {};
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
    set_arc_segment(s_arc_carb, start, start + carb_angle, kViolet);

    char input_text[128] = {};
    format_ingredients(record.input, input_text, sizeof(input_text));
    snprintf(text, sizeof(text), "%s | %.150s", jiaofu::cooking_method_name(record.method), input_text);
    lv_label_set_text(s_result_title, text);

    snprintf(text, sizeof(text), "%d kcal", static_cast<int>(record.prediction.cooked_energy_kcal + 0.5f));
    lv_label_set_text(s_energy_label, text);

    lv_label_set_text(s_macro_share_label, "Macro\ncalorie share");

    char amount[24] = {};
    format_one_decimal(record.prediction.cooked_protein_g, amount, sizeof(amount));
    snprintf(text, sizeof(text), "Protein\n%s g\n%d%%", amount, static_cast<int>(protein_pct + 0.5f));
    lv_label_set_text(s_protein_value_label, text);

    format_one_decimal(record.prediction.cooked_fat_g, amount, sizeof(amount));
    snprintf(text, sizeof(text), "Fat\n%s g\n%d%%", amount, static_cast<int>(fat_pct + 0.5f));
    lv_label_set_text(s_fat_value_label, text);

    format_one_decimal(record.prediction.cooked_carbohydrate_g, amount, sizeof(amount));
    snprintf(text, sizeof(text), "Carbs\n%s g\n%d%%", amount, static_cast<int>(carb_pct + 0.5f));
    lv_label_set_text(s_carb_value_label, text);
}

void inference_task(void *)
{
    IngredientState input = {};
    jiaofu::CookingMethod method = jiaofu::CookingMethod::Boil;
    lock_state();
    input = s_ingredients;
    method = s_selected_method;
    unlock_state();

    jiaofu::Prediction prediction = {};
    const bool ok = jiaofu::predict(input.items, input.count, method, &prediction);

    lock_state();
    if (ok) {
        s_cloud_record = {input, method, prediction, true};
        s_inference_state = InferenceState::Done;
    } else {
        s_inference_state = InferenceState::Failed;
    }
    unlock_state();
    vTaskDelete(nullptr);
}

void analyze_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    lock_state();
    const bool busy = s_inference_state == InferenceState::Running;
    if (!busy) {
        s_inference_state = InferenceState::Running;
    }
    unlock_state();

    if (!busy) {
        lv_label_set_text(s_status_label, "Analyzing nutrition...");
        xTaskCreate(inference_task, "nutrition_infer", 8192, nullptr, 5, nullptr);
    }
}

void back_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_add_flag(s_result_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_select_page, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_timer_cb(lv_timer_t *)
{
    IngredientState ingredients = {};
    InferenceState state = InferenceState::Idle;
    CloudNutritionRecord record = {};

    lock_state();
    ingredients = s_ingredients;
    state = s_inference_state;
    record = s_cloud_record;
    unlock_state();

    char input_text[128] = {};
    format_ingredients(ingredients, input_text, sizeof(input_text));
    char label_text[160] = {};
    snprintf(label_text, sizeof(label_text), "Input: %s", input_text);
    lv_label_set_text(s_ingredient_label, label_text);

    if (state == InferenceState::Idle) {
        lv_label_set_text(s_status_label, "Serial: chicken:150,potato:120,carrot:60");
    } else if (state == InferenceState::Running) {
        lv_label_set_text(s_status_label, "Analyzing nutrition...");
    } else if (state == InferenceState::Failed) {
        lv_label_set_text(s_status_label, "Prediction failed");
    } else if (state == InferenceState::Done && record.valid) {
        update_result_page(record);
        lv_obj_add_flag(s_select_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_result_page, LV_OBJ_FLAG_HIDDEN);

        lock_state();
        s_inference_state = InferenceState::Idle;
        unlock_state();
    }
}

void create_select_page(lv_obj_t *scr)
{
    s_select_page = lv_obj_create(scr);
    lv_obj_set_size(s_select_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_select_page, kBg, 0);
    lv_obj_set_style_border_width(s_select_page, 0, 0);
    lv_obj_clear_flag(s_select_page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = make_label(s_select_page, "Nutrition Estimate", &lv_font_montserrat_24, kText);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 22, 20);

    s_ingredient_label = make_label(s_select_page, "Input: --", &lv_font_montserrat_14, kMuted);
    lv_obj_set_width(s_ingredient_label, 430);
    lv_obj_align(s_ingredient_label, LV_ALIGN_TOP_LEFT, 22, 58);

    lv_obj_t *card = lv_obj_create(s_select_page);
    lv_obj_set_size(card, 436, 430);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(card, kCard, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, kBorder, 0);
    lv_obj_set_style_pad_all(card, 14, 0);

    lv_obj_t *prompt = make_label(card, "Cooking Method", &lv_font_montserrat_16, kText);
    lv_obj_align(prompt, LV_ALIGN_TOP_LEFT, 4, 0);

    lv_obj_t *grid = lv_obj_create(card);
    lv_obj_set_size(grid, 408, 300);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 40);
    style_plain(grid);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);

    static int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

    for (size_t i = 0; i < sizeof(s_methods) / sizeof(s_methods[0]); ++i) {
        s_methods[i].btn = make_method_button(grid, i);
        lv_obj_set_grid_cell(s_methods[i].btn,
                             LV_GRID_ALIGN_STRETCH,
                             static_cast<int>(i % 3),
                             1,
                             LV_GRID_ALIGN_STRETCH,
                             static_cast<int>(i / 3),
                             1);
    }

    lv_obj_t *analyze = lv_btn_create(card);
    lv_obj_set_size(analyze, 180, 52);
    lv_obj_set_style_radius(analyze, 8, 0);
    lv_obj_set_style_bg_color(analyze, kBlue, 0);
    lv_obj_align(analyze, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_event_cb(analyze, analyze_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *analyze_label = make_label(analyze, "Analyze", &lv_font_montserrat_16, lv_color_hex(0x00111A));
    lv_obj_center(analyze_label);

    s_status_label = make_label(s_select_page, "", &lv_font_montserrat_14, kMuted);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    update_method_styles();
}

void create_result_page(lv_obj_t *scr)
{
    s_result_page = lv_obj_create(scr);
    lv_obj_set_size(s_result_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_result_page, kBg, 0);
    lv_obj_set_style_border_width(s_result_page, 0, 0);
    lv_obj_clear_flag(s_result_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_result_page, LV_OBJ_FLAG_HIDDEN);

    s_result_title = make_label(s_result_page, "Result", &lv_font_montserrat_14, kMuted);
    lv_obj_set_width(s_result_title, 300);
    lv_obj_align(s_result_title, LV_ALIGN_TOP_LEFT, 156, 18);

    lv_obj_t *back = lv_btn_create(s_result_page);
    lv_obj_set_size(back, 132, 52);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 16, 12);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x102437), 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, kBorder, 0);
    lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *back_label = make_label(back, "< Back", &lv_font_montserrat_16, kText);
    lv_obj_center(back_label);

    lv_obj_t *energy_card = lv_obj_create(s_result_page);
    lv_obj_set_size(energy_card, 420, 82);
    lv_obj_align(energy_card, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_clear_flag(energy_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(energy_card, 8, 0);
    lv_obj_set_style_bg_color(energy_card, kCard, 0);
    lv_obj_set_style_border_width(energy_card, 1, 0);
    lv_obj_set_style_border_color(energy_card, kBorder, 0);

    lv_obj_t *energy_caption = make_label(energy_card, "Calories", &lv_font_montserrat_14, kMuted);
    lv_obj_align(energy_caption, LV_ALIGN_LEFT_MID, 18, -16);

    s_energy_label = make_label(energy_card, "-- kcal", &lv_font_montserrat_24, kText);
    lv_obj_align(s_energy_label, LV_ALIGN_LEFT_MID, 18, 18);

    lv_obj_t *energy_note = make_label(energy_card, "Atwater recalculated", &lv_font_montserrat_14, kBlue);
    lv_obj_align(energy_note, LV_ALIGN_RIGHT_MID, -18, 0);

    lv_obj_t *chart_box = lv_obj_create(s_result_page);
    lv_obj_set_size(chart_box, 300, 300);
    lv_obj_align(chart_box, LV_ALIGN_TOP_MID, 0, 146);
    style_plain(chart_box);

    s_arc_protein = lv_arc_create(chart_box);
    s_arc_fat = lv_arc_create(chart_box);
    s_arc_carb = lv_arc_create(chart_box);
    lv_obj_set_size(s_arc_protein, 252, 252);
    lv_obj_set_size(s_arc_fat, 252, 252);
    lv_obj_set_size(s_arc_carb, 252, 252);
    lv_obj_center(s_arc_protein);
    lv_obj_center(s_arc_fat);
    lv_obj_center(s_arc_carb);
    set_arc_segment(s_arc_protein, 0, 120, kGreen);
    set_arc_segment(s_arc_fat, 120, 240, kAmber);
    set_arc_segment(s_arc_carb, 240, 360, kViolet);

    s_macro_share_label = make_label(chart_box, "Macro\ncalorie share", &lv_font_montserrat_14, kText);
    lv_obj_set_style_text_align(s_macro_share_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_macro_share_label);

    lv_obj_t *macro_grid = lv_obj_create(s_result_page);
    lv_obj_set_size(macro_grid, 430, 102);
    lv_obj_align(macro_grid, LV_ALIGN_TOP_MID, 0, 456);
    style_plain(macro_grid);
    lv_obj_set_style_pad_column(macro_grid, 10, 0);
    static int32_t macro_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t macro_rows[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(macro_grid, macro_cols, macro_rows);

    auto make_macro_card = [](lv_obj_t *parent, int col, const char *initial, lv_color_t color, lv_obj_t **value_label) {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_size(card, 136, 96);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_bg_color(card, kCard, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, kBorder, 0);
        lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

        lv_obj_t *swatch = lv_obj_create(card);
        lv_obj_set_size(swatch, 12, 12);
        lv_obj_set_style_radius(swatch, 3, 0);
        lv_obj_set_style_bg_color(swatch, color, 0);
        lv_obj_set_style_border_width(swatch, 0, 0);
        lv_obj_align(swatch, LV_ALIGN_TOP_LEFT, 10, 10);

        *value_label = make_label(card, initial, &lv_font_montserrat_14, kText);
        lv_obj_set_style_text_align(*value_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(*value_label, LV_ALIGN_TOP_LEFT, 10, 30);
    };

    make_macro_card(macro_grid, 0, "Protein\n-- g\n--%", kGreen, &s_protein_value_label);
    make_macro_card(macro_grid, 1, "Fat\n-- g\n--%", kAmber, &s_fat_value_label);
    make_macro_card(macro_grid, 2, "Carbs\n-- g\n--%", kViolet, &s_carb_value_label);
}

} // namespace

extern "C" void nutrition_lvgl_ui(lv_display_t *disp)
{
    LV_UNUSED(disp);
    s_mutex = xSemaphoreCreateMutex();
    set_default_ingredients();

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, kBg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    create_select_page(scr);
    create_result_page(scr);
    lv_timer_create(ui_timer_cb, 250, nullptr);
    xTaskCreate(serial_input_task, "nutrition_serial", 4096, nullptr, 4, nullptr);
}

extern "C" bool nutrition_copy_latest_result(float outputs[jiaofu::kOutputCount])
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
