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
constexpr uint32_t kScreenW = 480;
constexpr uint32_t kScreenH = 640;

struct IngredientState {
    jiaofu::IngredientWeight items[jiaofu::kMaxIngredientsPerDish];
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
    jiaofu::CookingMethod method;
    jiaofu::Prediction prediction;
    bool valid;
};

struct MethodOption {
    const char *name_cn;
    const char *subtitle_cn;
    jiaofu::CookingMethod method;
    lv_obj_t *btn;
};

MethodOption s_methods[] = {
    {"不烹饪", "按生食/原始营养估算", jiaofu::CookingMethod::Raw, nullptr},
    {"水煮", "清淡，含水烹调", jiaofu::CookingMethod::Boil, nullptr},
    {"红烧", "酱汁炖煮", jiaofu::CookingMethod::Braise, nullptr},
    {"油炸", "高油高温", jiaofu::CookingMethod::DeepFry, nullptr},
    {"煎制", "平底锅加热", jiaofu::CookingMethod::PanFry, nullptr},
    {"烤制", "干热烘烤", jiaofu::CookingMethod::Roast, nullptr},
    {"清蒸", "低油蒸汽", jiaofu::CookingMethod::Steam, nullptr},
    {"炒制", "快速翻炒", jiaofu::CookingMethod::StirFry, nullptr},
};

SemaphoreHandle_t s_mutex = nullptr;
IngredientState s_ingredients = {};
jiaofu::CookingMethod s_selected_method = jiaofu::CookingMethod::Raw;
InferenceState s_inference_state = InferenceState::Idle;
CloudNutritionRecord s_cloud_record = {};
bool s_prediction_dirty = true;
Page s_page = Page::Scale;

lv_obj_t *s_pages[3] = {};
lv_obj_t *s_dots[3] = {};
lv_obj_t *s_food_list = nullptr;
lv_obj_t *s_total_weight_label = nullptr;
lv_obj_t *s_scale_status_label = nullptr;
lv_obj_t *s_method_status_label = nullptr;
lv_obj_t *s_result_status_label = nullptr;
lv_obj_t *s_result_title = nullptr;
lv_obj_t *s_energy_label = nullptr;
lv_obj_t *s_weight_label = nullptr;
lv_obj_t *s_macro_center_label = nullptr;
lv_obj_t *s_protein_value_label = nullptr;
lv_obj_t *s_fat_value_label = nullptr;
lv_obj_t *s_carb_value_label = nullptr;
lv_obj_t *s_micro_label = nullptr;
lv_obj_t *s_arc_protein = nullptr;
lv_obj_t *s_arc_fat = nullptr;
lv_obj_t *s_arc_carb = nullptr;

const lv_color_t kBg = lv_color_hex(0xF5F7FB);
const lv_color_t kPanel = lv_color_hex(0xFFFFFF);
const lv_color_t kPanelSoft = lv_color_hex(0xEAF0F6);
const lv_color_t kLine = lv_color_hex(0xD6DEE8);
const lv_color_t kText = lv_color_hex(0x17202A);
const lv_color_t kMuted = lv_color_hex(0x667085);
const lv_color_t kBlue = lv_color_hex(0x2563EB);
const lv_color_t kGreen = lv_color_hex(0x16A34A);
const lv_color_t kAmber = lv_color_hex(0xF59E0B);
const lv_color_t kRose = lv_color_hex(0xE11D48);

const lv_font_t *font_cn()
{
    return &lv_font_simsun_16_cjk;
}

const lv_font_t *font_num_big()
{
    return &lv_font_montserrat_32;
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

float total_weight(const IngredientState &state)
{
    float total = 0.0f;
    for (size_t i = 0; i < state.count; ++i) {
        total += state.items[i].raw_weight_g;
    }
    return total;
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
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void style_plain(lv_obj_t *obj)
{
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

const char *method_name_cn(jiaofu::CookingMethod method)
{
    for (const auto &option : s_methods) {
        if (option.method == method) {
            return option.name_cn;
        }
    }
    return "未知";
}

void show_page(Page page);

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
                    s_prediction_dirty = true;
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
    lv_obj_set_style_arc_width(arc, 30, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 30, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
}

void update_method_styles()
{
    for (auto &option : s_methods) {
        const bool selected = option.method == s_selected_method;
        lv_obj_set_style_bg_color(option.btn, selected ? lv_color_hex(0xDBEAFE) : kPanel, 0);
        lv_obj_set_style_border_color(option.btn, selected ? kBlue : kLine, 0);
        lv_obj_set_style_border_width(option.btn, selected ? 2 : 1, 0);
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
                jiaofu::CookingMethod method = jiaofu::CookingMethod::Raw;
                lock_state();
                input = s_ingredients;
                method = s_selected_method;
                unlock_state();

                jiaofu::Prediction prediction = {};
                const bool ok = jiaofu::predict(input.items, input.count, method, &prediction);

                lock_state();
                if (ok) {
                    s_cloud_record = {input, method, prediction, true};
                    s_prediction_dirty = false;
                    s_inference_state = InferenceState::Done;
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
    const int next = (static_cast<int>(s_page) + 1) % 3;
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
    if (dir == LV_DIR_RIGHT) {
        go_next_page();
    } else if (dir == LV_DIR_LEFT) {
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
    s_prediction_dirty = true;
    if (s_inference_state != InferenceState::Running) {
        s_inference_state = InferenceState::Idle;
    }
    unlock_state();
    update_method_styles();
}

lv_obj_t *make_action_button(lv_obj_t *parent, const char *text, lv_color_t bg)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 180, 52);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t *label = make_label(btn, text, font_cn(), lv_color_hex(0xFFFFFF));
    lv_obj_center(label);
    return btn;
}

void create_header(lv_obj_t *page, const char *title, const char *subtitle)
{
    lv_obj_t *label = make_label(page, title, font_cn(), kText);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 24, 18);

    lv_obj_t *sub = make_label(page, subtitle, font_cn(), kMuted);
    lv_obj_set_width(sub, 360);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 24, 46);
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
    create_header(page, "称重信息", "感知侧识别的食材会显示在这里");

    lv_obj_t *weight_card = lv_obj_create(page);
    lv_obj_set_size(weight_card, 432, 134);
    lv_obj_align(weight_card, LV_ALIGN_TOP_MID, 0, 92);
    style_panel(weight_card);

    lv_obj_t *caption = make_label(weight_card, "当前总重量", font_cn(), kMuted);
    lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 20, 16);

    s_total_weight_label = make_label(weight_card, "-- g", font_num_big(), kText);
    lv_obj_align(s_total_weight_label, LV_ALIGN_LEFT_MID, 20, 20);

    lv_obj_t *unit = make_label(weight_card, "最多支持 4 种食材组合", font_cn(), kBlue);
    lv_obj_align(unit, LV_ALIGN_RIGHT_MID, -18, 24);

    lv_obj_t *list_card = lv_obj_create(page);
    lv_obj_set_size(list_card, 432, 246);
    lv_obj_align(list_card, LV_ALIGN_TOP_MID, 0, 248);
    style_panel(list_card);
    lv_obj_set_style_pad_all(list_card, 18, 0);

    lv_obj_t *list_title = make_label(list_card, "秤上食材", font_cn(), kText);
    lv_obj_align(list_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_food_list = make_label(list_card, "等待输入", font_cn(), kText);
    lv_obj_set_width(s_food_list, 390);
    lv_obj_align(s_food_list, LV_ALIGN_TOP_LEFT, 0, 42);

    s_scale_status_label = make_label(page, "串口示例：chicken:150,potato:120,carrot:60", font_cn(), kMuted);
    lv_obj_set_width(s_scale_status_label, 432);
    lv_obj_align(s_scale_status_label, LV_ALIGN_TOP_MID, 0, 514);

    lv_obj_t *next = make_action_button(page, "选择烹饪方式", kBlue);
    lv_obj_align(next, LV_ALIGN_BOTTOM_MID, 0, -48);
    lv_obj_add_event_cb(next, next_event_cb, LV_EVENT_CLICKED, nullptr);
}

lv_obj_t *make_method_button(lv_obj_t *parent, MethodOption *option)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 202, 78);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, kLine, 0);
    lv_obj_set_style_bg_color(btn, kPanel, 0);
    lv_obj_add_event_cb(btn, method_event_cb, LV_EVENT_CLICKED, option);

    lv_obj_t *name = make_label(btn, option->name_cn, font_cn(), kText);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 12, 10);
    lv_obj_t *sub = make_label(btn, option->subtitle_cn, font_cn(), kMuted);
    lv_obj_set_width(sub, 176);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 12, 40);
    return btn;
}

void create_method_page(lv_obj_t *scr)
{
    lv_obj_t *page = lv_obj_create(scr);
    s_pages[static_cast<int>(Page::Method)] = page;
    style_page(page);
    create_header(page, "烹饪方式", "选择当前秤上食材将要采用的处理方式");

    lv_obj_t *grid = lv_obj_create(page);
    lv_obj_set_size(grid, 432, 370);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 92);
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

    s_method_status_label = make_label(page, "当前选择：不烹饪", font_cn(), kMuted);
    lv_obj_set_width(s_method_status_label, 432);
    lv_obj_align(s_method_status_label, LV_ALIGN_TOP_MID, 0, 486);

    lv_obj_t *next = make_action_button(page, "查看营养结果", kGreen);
    lv_obj_align(next, LV_ALIGN_BOTTOM_MID, 0, -48);
    lv_obj_add_event_cb(next, next_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    update_method_styles();
}

void make_macro_card(lv_obj_t *parent, int col, const char *title, lv_color_t color, lv_obj_t **value_label)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 136, 94);
    style_panel(card);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

    lv_obj_t *swatch = lv_obj_create(card);
    lv_obj_set_size(swatch, 14, 14);
    lv_obj_set_style_radius(swatch, 3, 0);
    lv_obj_set_style_bg_color(swatch, color, 0);
    lv_obj_set_style_border_width(swatch, 0, 0);
    lv_obj_align(swatch, LV_ALIGN_TOP_LEFT, 10, 10);

    lv_obj_t *name = make_label(card, title, font_cn(), kMuted);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 32, 8);

    *value_label = make_label(card, "-- g\n--%", font_cn(), kText);
    lv_obj_set_width(*value_label, 112);
    lv_obj_set_style_text_align(*value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(*value_label, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void create_result_page(lv_obj_t *scr)
{
    lv_obj_t *page = lv_obj_create(scr);
    s_pages[static_cast<int>(Page::Result)] = page;
    style_page(page);
    create_header(page, "营养结果", "右滑可回到称重页，完整数据已保留用于上云");

    s_result_title = make_label(page, "等待计算", font_cn(), kMuted);
    lv_obj_set_width(s_result_title, 432);
    lv_obj_align(s_result_title, LV_ALIGN_TOP_MID, 0, 76);

    lv_obj_t *energy_card = lv_obj_create(page);
    lv_obj_set_size(energy_card, 432, 96);
    lv_obj_align(energy_card, LV_ALIGN_TOP_MID, 0, 104);
    style_panel(energy_card);

    lv_obj_t *energy_caption = make_label(energy_card, "热量", font_cn(), kMuted);
    lv_obj_align(energy_caption, LV_ALIGN_TOP_LEFT, 18, 14);

    s_energy_label = make_label(energy_card, "-- kcal", font_num_big(), kText);
    lv_obj_align(s_energy_label, LV_ALIGN_LEFT_MID, 18, 18);

    s_weight_label = make_label(energy_card, "成品重量 -- g", font_cn(), kBlue);
    lv_obj_align(s_weight_label, LV_ALIGN_RIGHT_MID, -18, 16);

    lv_obj_t *chart_box = lv_obj_create(page);
    lv_obj_set_size(chart_box, 252, 252);
    lv_obj_align(chart_box, LV_ALIGN_TOP_MID, 0, 216);
    style_plain(chart_box);

    s_arc_protein = lv_arc_create(chart_box);
    s_arc_fat = lv_arc_create(chart_box);
    s_arc_carb = lv_arc_create(chart_box);
    lv_obj_set_size(s_arc_protein, 236, 236);
    lv_obj_set_size(s_arc_fat, 236, 236);
    lv_obj_set_size(s_arc_carb, 236, 236);
    lv_obj_center(s_arc_protein);
    lv_obj_center(s_arc_fat);
    lv_obj_center(s_arc_carb);
    set_arc_segment(s_arc_protein, 0, 120, kGreen);
    set_arc_segment(s_arc_fat, 120, 240, kAmber);
    set_arc_segment(s_arc_carb, 240, 360, kRose);

    s_macro_center_label = make_label(chart_box, "三大营养\n供能占比", font_cn(), kText);
    lv_obj_set_style_text_align(s_macro_center_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_macro_center_label);

    lv_obj_t *macro_grid = lv_obj_create(page);
    lv_obj_set_size(macro_grid, 432, 104);
    lv_obj_align(macro_grid, LV_ALIGN_TOP_MID, 0, 468);
    style_plain(macro_grid);
    lv_obj_set_style_pad_column(macro_grid, 10, 0);
    static int32_t macro_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t macro_rows[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(macro_grid, macro_cols, macro_rows);

    make_macro_card(macro_grid, 0, "蛋白质", kGreen, &s_protein_value_label);
    make_macro_card(macro_grid, 1, "脂肪", kAmber, &s_fat_value_label);
    make_macro_card(macro_grid, 2, "碳水", kRose, &s_carb_value_label);

    s_micro_label = make_label(page, "钠 -- mg  胆固醇 -- mg", font_cn(), kMuted);
    lv_obj_set_width(s_micro_label, 432);
    lv_obj_align(s_micro_label, LV_ALIGN_BOTTOM_MID, 0, -48);

    s_result_status_label = make_label(page, "", font_cn(), kMuted);
    lv_obj_set_width(s_result_status_label, 432);
    lv_obj_align(s_result_status_label, LV_ALIGN_BOTTOM_MID, 0, -78);

    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
}

void update_scale_page(const IngredientState &ingredients)
{
    char text[256] = {};
    char one[72] = {};
    text[0] = '\0';
    for (size_t i = 0; i < ingredients.count; ++i) {
        snprintf(one,
                 sizeof(one),
                 "%u. %s   %.0f g\n",
                 static_cast<unsigned>(i + 1),
                 jiaofu::ingredient_name(ingredients.items[i].ingredient),
                 static_cast<double>(ingredients.items[i].raw_weight_g));
        strncat(text, one, sizeof(text) - strlen(text) - 1);
    }
    lv_label_set_text(s_food_list, ingredients.count > 0 ? text : "等待输入");

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

    char input_text[128] = {};
    format_ingredients(record.input, input_text, sizeof(input_text));

    char text[192] = {};
    snprintf(text, sizeof(text), "%s | %.100s", method_name_cn(record.method), input_text);
    lv_label_set_text(s_result_title, text);

    snprintf(text, sizeof(text), "%d kcal", static_cast<int>(record.prediction.cooked_energy_kcal + 0.5f));
    lv_label_set_text(s_energy_label, text);

    char amount[24] = {};
    format_one_decimal(record.prediction.cooked_weight_g, amount, sizeof(amount));
    snprintf(text, sizeof(text), "成品重量 %s g", amount);
    lv_label_set_text(s_weight_label, text);

    format_one_decimal(record.prediction.cooked_protein_g, amount, sizeof(amount));
    snprintf(text, sizeof(text), "%s g\n%d%%", amount, static_cast<int>(protein_pct + 0.5f));
    lv_label_set_text(s_protein_value_label, text);

    format_one_decimal(record.prediction.cooked_fat_g, amount, sizeof(amount));
    snprintf(text, sizeof(text), "%s g\n%d%%", amount, static_cast<int>(fat_pct + 0.5f));
    lv_label_set_text(s_fat_value_label, text);

    format_one_decimal(record.prediction.cooked_carbohydrate_g, amount, sizeof(amount));
    snprintf(text, sizeof(text), "%s g\n%d%%", amount, static_cast<int>(carb_pct + 0.5f));
    lv_label_set_text(s_carb_value_label, text);

    snprintf(text,
             sizeof(text),
             "钠 %.0f mg  胆固醇 %.0f mg",
             static_cast<double>(record.prediction.cooked_sodium_mg),
             static_cast<double>(record.prediction.cooked_cholesterol_mg));
    lv_label_set_text(s_micro_label, text);
}

void ui_timer_cb(lv_timer_t *)
{
    IngredientState ingredients = {};
    InferenceState state = InferenceState::Idle;
    CloudNutritionRecord record = {};
    bool dirty = false;
    jiaofu::CookingMethod method = jiaofu::CookingMethod::Raw;

    lock_state();
    ingredients = s_ingredients;
    state = s_inference_state;
    record = s_cloud_record;
    dirty = s_prediction_dirty;
    method = s_selected_method;
    unlock_state();

    update_scale_page(ingredients);

    char text[96] = {};
    snprintf(text, sizeof(text), "当前选择：%s", method_name_cn(method));
    lv_label_set_text(s_method_status_label, text);

    if (state == InferenceState::Running) {
        lv_label_set_text(s_result_status_label, "正在计算营养信息...");
    } else if (state == InferenceState::Failed) {
        lv_label_set_text(s_result_status_label, "模型推理失败，请检查输入");
    } else if (dirty) {
        lv_label_set_text(s_result_status_label, "进入本页后自动计算");
    } else {
        lv_label_set_text(s_result_status_label, "结果已更新，可用于后续上云");
    }

    if (record.valid && state != InferenceState::Running && !dirty) {
        update_result_page(record);
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
