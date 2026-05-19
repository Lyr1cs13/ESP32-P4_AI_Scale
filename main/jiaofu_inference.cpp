#include "jiaofu_inference.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr const char *TAG = "jiaofu_infer";

struct EmbeddedModel {
    const char *name;
    const char *begin;
    const char *end;
};

constexpr size_t kMaxInternalNodes = 255;
constexpr size_t kMaxLeaves = 256;
constexpr size_t kMaxTrees = 512;

struct TreeParseWorkspace {
    int split_feature[kMaxInternalNodes];
    int left_child[kMaxInternalNodes];
    int right_child[kMaxInternalNodes];
    float threshold[kMaxInternalNodes];
    float leaf_value[kMaxLeaves];
};

struct ParsedTree {
    uint16_t internal_count;
    uint16_t leaf_count;
    uint8_t split_feature[kMaxInternalNodes];
    int16_t left_child[kMaxInternalNodes];
    int16_t right_child[kMaxInternalNodes];
    float threshold[kMaxInternalNodes];
    float leaf_value[kMaxLeaves];
};

struct ParsedModel {
    bool loaded;
    size_t tree_count;
    ParsedTree trees[kMaxTrees];
};

extern const char lgbm_cooked_weight_g_txt_start[] asm("_binary_lgbm_cooked_weight_g_txt_start");
extern const char lgbm_cooked_weight_g_txt_end[] asm("_binary_lgbm_cooked_weight_g_txt_end");
extern const char lgbm_cooked_energy_kcal_txt_start[] asm("_binary_lgbm_cooked_energy_kcal_txt_start");
extern const char lgbm_cooked_energy_kcal_txt_end[] asm("_binary_lgbm_cooked_energy_kcal_txt_end");
extern const char lgbm_cooked_protein_g_txt_start[] asm("_binary_lgbm_cooked_protein_g_txt_start");
extern const char lgbm_cooked_protein_g_txt_end[] asm("_binary_lgbm_cooked_protein_g_txt_end");
extern const char lgbm_cooked_fat_g_txt_start[] asm("_binary_lgbm_cooked_fat_g_txt_start");
extern const char lgbm_cooked_fat_g_txt_end[] asm("_binary_lgbm_cooked_fat_g_txt_end");
extern const char lgbm_cooked_carbohydrate_g_txt_start[] asm("_binary_lgbm_cooked_carbohydrate_g_txt_start");
extern const char lgbm_cooked_carbohydrate_g_txt_end[] asm("_binary_lgbm_cooked_carbohydrate_g_txt_end");
extern const char lgbm_cooked_sodium_mg_txt_start[] asm("_binary_lgbm_cooked_sodium_mg_txt_start");
extern const char lgbm_cooked_sodium_mg_txt_end[] asm("_binary_lgbm_cooked_sodium_mg_txt_end");
extern const char lgbm_cooked_cholesterol_mg_txt_start[] asm("_binary_lgbm_cooked_cholesterol_mg_txt_start");
extern const char lgbm_cooked_cholesterol_mg_txt_end[] asm("_binary_lgbm_cooked_cholesterol_mg_txt_end");
extern const char lgbm_cooked_vitamin_c_mg_txt_start[] asm("_binary_lgbm_cooked_vitamin_c_mg_txt_start");
extern const char lgbm_cooked_vitamin_c_mg_txt_end[] asm("_binary_lgbm_cooked_vitamin_c_mg_txt_end");
extern const char lgbm_cooked_calcium_mg_txt_start[] asm("_binary_lgbm_cooked_calcium_mg_txt_start");
extern const char lgbm_cooked_calcium_mg_txt_end[] asm("_binary_lgbm_cooked_calcium_mg_txt_end");
extern const char lgbm_cooked_iron_mg_txt_start[] asm("_binary_lgbm_cooked_iron_mg_txt_start");
extern const char lgbm_cooked_iron_mg_txt_end[] asm("_binary_lgbm_cooked_iron_mg_txt_end");
extern const char lgbm_cooked_potassium_mg_txt_start[] asm("_binary_lgbm_cooked_potassium_mg_txt_start");
extern const char lgbm_cooked_potassium_mg_txt_end[] asm("_binary_lgbm_cooked_potassium_mg_txt_end");

const EmbeddedModel kModels[jiaofu::kOutputCount] = {
    {"cooked_weight_g", lgbm_cooked_weight_g_txt_start, lgbm_cooked_weight_g_txt_end},
    {"cooked_energy_kcal", lgbm_cooked_energy_kcal_txt_start, lgbm_cooked_energy_kcal_txt_end},
    {"cooked_protein_g", lgbm_cooked_protein_g_txt_start, lgbm_cooked_protein_g_txt_end},
    {"cooked_fat_g", lgbm_cooked_fat_g_txt_start, lgbm_cooked_fat_g_txt_end},
    {"cooked_carbohydrate_g", lgbm_cooked_carbohydrate_g_txt_start, lgbm_cooked_carbohydrate_g_txt_end},
    {"cooked_sodium_mg", lgbm_cooked_sodium_mg_txt_start, lgbm_cooked_sodium_mg_txt_end},
    {"cooked_cholesterol_mg", lgbm_cooked_cholesterol_mg_txt_start, lgbm_cooked_cholesterol_mg_txt_end},
    {"cooked_vitamin_c_mg", lgbm_cooked_vitamin_c_mg_txt_start, lgbm_cooked_vitamin_c_mg_txt_end},
    {"cooked_calcium_mg", lgbm_cooked_calcium_mg_txt_start, lgbm_cooked_calcium_mg_txt_end},
    {"cooked_iron_mg", lgbm_cooked_iron_mg_txt_start, lgbm_cooked_iron_mg_txt_end},
    {"cooked_potassium_mg", lgbm_cooked_potassium_mg_txt_start, lgbm_cooked_potassium_mg_txt_end},
};

ParsedModel *g_parsed_models[jiaofu::kOutputCount] = {};

bool is_fruit(jiaofu::Ingredient ingredient)
{
    using I = jiaofu::Ingredient;
    switch (ingredient) {
    case I::Apple:
    case I::Banana:
    case I::Grape:
    case I::Kiwi:
    case I::Kumquat:
    case I::Lemon:
    case I::Orange:
    case I::Peach:
    case I::Pineapple:
    case I::Strawberry:
    case I::Watermelon:
        return true;
    default:
        return false;
    }
}

bool is_meat(jiaofu::Ingredient ingredient)
{
    using I = jiaofu::Ingredient;
    switch (ingredient) {
    case I::Beef:
    case I::Chicken:
    case I::Pork:
    case I::Shrimp:
    case I::Fish:
        return true;
    default:
        return false;
    }
}

bool line_starts_with(const char *line, const char *line_end, const char *prefix)
{
    const size_t prefix_len = strlen(prefix);
    return static_cast<size_t>(line_end - line) >= prefix_len && memcmp(line, prefix, prefix_len) == 0;
}

template <typename T, typename ParseOne>
size_t parse_list(const char *line, const char *line_end, const char *key, T *values, size_t max_values, ParseOne parse_one)
{
    const size_t key_len = strlen(key);
    const char *p = line + key_len;
    size_t count = 0;
    while (p < line_end && count < max_values) {
        while (p < line_end && (*p == ' ' || *p == '\t')) {
            ++p;
        }
        if (p >= line_end || *p == '\r' || *p == '\n') {
            break;
        }
        char *next = nullptr;
        values[count++] = parse_one(p, &next);
        if (next == p) {
            break;
        }
        p = next;
    }
    return count;
}

float eval_parsed_tree(const ParsedTree &tree, const float features[jiaofu::kFeatureCount])
{
    int node = 0;
    while (node >= 0 && node < static_cast<int>(tree.internal_count)) {
        const int feature = tree.split_feature[node];
        const int next = features[feature] <= tree.threshold[node] ? tree.left_child[node] : tree.right_child[node];
        if (next < 0) {
            const int leaf = -next - 1;
            return leaf >= 0 && leaf < static_cast<int>(tree.leaf_count) ? tree.leaf_value[leaf] : NAN;
        }
        node = next;
    }
    return NAN;
}

void maybe_yield(size_t line_count)
{
    if ((line_count & 0x3f) == 0) {
        vTaskDelay(1);
    }
}

bool append_parsed_tree(ParsedModel *parsed,
                        const TreeParseWorkspace &workspace,
                        size_t internal_count,
                        size_t leaf_count)
{
    if (parsed == nullptr || parsed->tree_count >= kMaxTrees || internal_count > kMaxInternalNodes ||
        leaf_count > kMaxLeaves) {
        return false;
    }

    ParsedTree &tree = parsed->trees[parsed->tree_count++];
    tree.internal_count = static_cast<uint16_t>(internal_count);
    tree.leaf_count = static_cast<uint16_t>(leaf_count);

    for (size_t i = 0; i < internal_count; ++i) {
        tree.split_feature[i] = static_cast<uint8_t>(workspace.split_feature[i]);
        tree.left_child[i] = static_cast<int16_t>(workspace.left_child[i]);
        tree.right_child[i] = static_cast<int16_t>(workspace.right_child[i]);
        tree.threshold[i] = workspace.threshold[i];
    }
    memcpy(tree.leaf_value, workspace.leaf_value, sizeof(float) * leaf_count);
    return true;
}

ParsedModel *load_one_model(size_t model_index)
{
    static TreeParseWorkspace workspace = {};
    const EmbeddedModel &model = kModels[model_index];
    ParsedModel *parsed = static_cast<ParsedModel *>(heap_caps_calloc(1, sizeof(ParsedModel), MALLOC_CAP_SPIRAM));
    if (parsed == nullptr) {
        parsed = static_cast<ParsedModel *>(heap_caps_calloc(1, sizeof(ParsedModel), MALLOC_CAP_DEFAULT));
    }
    if (parsed == nullptr) {
        ESP_LOGE(TAG, "failed to allocate parsed model: %s", model.name);
        return nullptr;
    }

    size_t internal_count = 0;
    size_t threshold_count = 0;
    size_t left_count = 0;
    size_t right_count = 0;
    size_t line_count = 0;
    const int64_t start_us = esp_timer_get_time();

    const char *line = model.begin;
    while (line < model.end) {
        ++line_count;
        const char *line_end = static_cast<const char *>(memchr(line, '\n', model.end - line));
        if (line_end == nullptr) {
            line_end = model.end;
        }

        if (line_starts_with(line, line_end, "split_feature=")) {
            internal_count = parse_list<int>(line, line_end, "split_feature=", workspace.split_feature, kMaxInternalNodes,
                                             [](const char *p, char **next) { return static_cast<int>(strtol(p, next, 10)); });
        } else if (line_starts_with(line, line_end, "threshold=")) {
            threshold_count = parse_list<float>(line, line_end, "threshold=", workspace.threshold, kMaxInternalNodes,
                                                [](const char *p, char **next) { return strtof(p, next); });
        } else if (line_starts_with(line, line_end, "left_child=")) {
            left_count = parse_list<int>(line, line_end, "left_child=", workspace.left_child, kMaxInternalNodes,
                                         [](const char *p, char **next) { return static_cast<int>(strtol(p, next, 10)); });
        } else if (line_starts_with(line, line_end, "right_child=")) {
            right_count = parse_list<int>(line, line_end, "right_child=", workspace.right_child, kMaxInternalNodes,
                                          [](const char *p, char **next) { return static_cast<int>(strtol(p, next, 10)); });
        } else if (line_starts_with(line, line_end, "leaf_value=")) {
            const size_t leaf_count = parse_list<float>(line, line_end, "leaf_value=", workspace.leaf_value, kMaxLeaves,
                                                        [](const char *p, char **next) { return strtof(p, next); });
            if (internal_count == threshold_count && internal_count == left_count && internal_count == right_count) {
                if (!append_parsed_tree(parsed, workspace, internal_count, leaf_count)) {
                    ESP_LOGE(TAG, "failed to append tree for model: %s", model.name);
                    heap_caps_free(parsed);
                    return nullptr;
                }
            }
        }

        line = line_end + (line_end < model.end ? 1 : 0);
        maybe_yield(line_count);
    }

    parsed->loaded = true;
    ESP_LOGI(TAG,
             "loaded %s: %u trees, %.2f ms",
             model.name,
             static_cast<unsigned>(parsed->tree_count),
             static_cast<double>(esp_timer_get_time() - start_us) / 1000.0);
    return parsed;
}

bool ensure_models_loaded()
{
    static bool loaded = false;
    if (loaded) {
        return true;
    }

    const int64_t start_us = esp_timer_get_time();
    for (size_t i = 0; i < jiaofu::kOutputCount; ++i) {
        g_parsed_models[i] = load_one_model(i);
        if (g_parsed_models[i] == nullptr) {
            return false;
        }
    }

    loaded = true;
    ESP_LOGI(TAG, "all models loaded in %.2f ms", static_cast<double>(esp_timer_get_time() - start_us) / 1000.0);
    return true;
}

float predict_one_model(size_t model_index, const float features[jiaofu::kFeatureCount])
{
    const ParsedModel *model = g_parsed_models[model_index];
    if (model == nullptr || !model->loaded) {
        return NAN;
    }

    float sum = 0.0f;
    for (size_t i = 0; i < model->tree_count; ++i) {
        const float value = eval_parsed_tree(model->trees[i], features);
        if (!isnan(value)) {
            sum += value;
        }
    }
    return sum;
}

} // namespace

namespace jiaofu {

bool build_features(const IngredientWeight *items,
                    size_t item_count,
                    CookingMethod method,
                    float features[kFeatureCount])
{
    if (items == nullptr || features == nullptr || item_count == 0 || item_count > kMaxIngredientsPerDish) {
        return false;
    }

    memset(features, 0, sizeof(float) * kFeatureCount);
    float total_weight = 0.0f;
    bool has_fruit = false;
    bool has_meat = false;

    for (size_t i = 0; i < item_count; ++i) {
        const int ingredient_index = static_cast<int>(items[i].ingredient);
        if (ingredient_index < 0 || ingredient_index >= static_cast<int>(Ingredient::Count) || items[i].raw_weight_g < 0.0f) {
            return false;
        }

        features[ingredient_index] = 1.0f;
        features[31 + i] = items[i].raw_weight_g;
        total_weight += items[i].raw_weight_g;
        has_fruit = has_fruit || is_fruit(items[i].ingredient);
        has_meat = has_meat || is_meat(items[i].ingredient);
    }

    features[35] = total_weight;
    features[36] = total_weight / static_cast<float>(item_count);
    features[37] = static_cast<float>(item_count);
    features[38] = has_fruit ? 1.0f : 0.0f;
    features[39] = has_meat ? 1.0f : 0.0f;
    features[40] = static_cast<float>(method);
    return true;
}

bool predict(const IngredientWeight *items, size_t item_count, CookingMethod method, Prediction *prediction)
{
    if (prediction == nullptr) {
        return false;
    }

    float features[kFeatureCount] = {};
    if (!build_features(items, item_count, method, features)) {
        return false;
    }

    if (!ensure_models_loaded()) {
        return false;
    }

    float outputs[kOutputCount] = {};
    const int64_t start_us = esp_timer_get_time();
    for (size_t i = 0; i < kOutputCount; ++i) {
        outputs[i] = predict_one_model(i, features);
        if (outputs[i] < 0.0f || isnan(outputs[i])) {
            outputs[i] = 0.0f;
        }
    }
    ESP_LOGI(TAG, "prediction inference time: %.2f ms", static_cast<double>(esp_timer_get_time() - start_us) / 1000.0);

    outputs[1] = 4.0f * outputs[2] + 9.0f * outputs[3] + 4.0f * outputs[4];

    *prediction = {
        outputs[0],
        outputs[1],
        outputs[2],
        outputs[3],
        outputs[4],
        outputs[5],
        outputs[6],
        outputs[7],
        outputs[8],
        outputs[9],
        outputs[10],
    };
    return true;
}

const char *ingredient_name(Ingredient ingredient)
{
    static const char *kNames[] = {"apple",       "banana", "beef",        "bell_pepper", "cabbage",    "carrot",
                                  "cauliflower", "chicken", "cucumber",    "egg",         "eggplant",   "fish",
                                  "garlic",      "ginger",  "grape",       "kiwi",        "kumquat",    "lemon",
                                  "onion",       "orange",  "peach",       "pepper",      "pineapple",  "pork",
                                  "potato",      "shrimp",  "small_pepper", "strawberry",  "tofu",       "tomato",
                                  "watermelon"};
    const int index = static_cast<int>(ingredient);
    return index >= 0 && index < static_cast<int>(Ingredient::Count) ? kNames[index] : "unknown";
}

const char *cooking_method_name(CookingMethod method)
{
    static const char *kNames[] = {"boil", "braise", "deep_fry", "pan_fry", "roast", "steam", "stir_fry"};
    const int index = static_cast<int>(method);
    return index >= 0 && index < 7 ? kNames[index] : "unknown";
}

} // namespace jiaofu
