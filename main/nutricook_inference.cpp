#include "nutricook_inference.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "nutricook_raw_table.hpp"

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace {

constexpr const char *TAG = "nutricook_infer";
constexpr uint32_t kBinaryMagic = 0x3142464a; // JFB1, little endian

struct BinaryModelHeader {
    uint32_t tree_count;
    uint32_t offset;
};

struct BinaryTreeView {
    uint16_t internal_count;
    uint16_t leaf_count;
    const uint8_t *split_feature;
    const int16_t *left_child;
    const int16_t *right_child;
    const float *threshold;
    const float *leaf_value;
};

struct BinaryModelView {
    uint32_t tree_count;
    BinaryTreeView trees[512];
};

extern const uint8_t nutricook_models_bin_start[] asm("_binary_nutricook_models_bin_start");
extern const uint8_t nutricook_models_bin_end[] asm("_binary_nutricook_models_bin_end");

BinaryModelView g_models[nutricook::kOutputCount] = {};
bool g_models_mapped = false;

const uint8_t *align_ptr(const uint8_t *p, uintptr_t boundary)
{
    const uintptr_t value = reinterpret_cast<uintptr_t>(p);
    return reinterpret_cast<const uint8_t *>((value + boundary - 1) & ~(boundary - 1));
}

uint16_t read_u16(const uint8_t *p)
{
    uint16_t value = 0;
    memcpy(&value, p, sizeof(value));
    return value;
}

uint32_t read_u32(const uint8_t *p)
{
    uint32_t value = 0;
    memcpy(&value, p, sizeof(value));
    return value;
}

bool is_fruit(nutricook::Ingredient ingredient)
{
    using I = nutricook::Ingredient;
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

bool is_meat(nutricook::Ingredient ingredient)
{
    using I = nutricook::Ingredient;
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

int16_t child_value(const int16_t *values, uint16_t index)
{
    int16_t value = 0;
    memcpy(&value, values + index, sizeof(value));
    return value;
}

float float_value(const float *values, uint16_t index)
{
    float value = 0.0f;
    memcpy(&value, values + index, sizeof(value));
    return value;
}

bool map_one_model(uint32_t model_index, const uint8_t *base, const uint8_t *end, const BinaryModelHeader &header)
{
    if (model_index >= nutricook::kOutputCount || base + header.offset + sizeof(uint32_t) > end) {
        return false;
    }

    const uint8_t *p = base + header.offset;
    const uint32_t tree_count = read_u32(p);
    p += sizeof(uint32_t);
    if (tree_count != header.tree_count || tree_count > 512) {
        return false;
    }

    BinaryModelView &view = g_models[model_index];
    view.tree_count = tree_count;

    for (uint32_t i = 0; i < tree_count; ++i) {
        p = align_ptr(p, 4);
        if (p + 4 > end) {
            return false;
        }

        BinaryTreeView &tree = view.trees[i];
        tree.internal_count = read_u16(p);
        tree.leaf_count = read_u16(p + 2);
        p += 4;

        if (tree.internal_count > 255 || tree.leaf_count > 256) {
            return false;
        }

        tree.split_feature = p;
        p += tree.internal_count;
        p = align_ptr(p, 2);

        tree.left_child = reinterpret_cast<const int16_t *>(p);
        p += sizeof(int16_t) * tree.internal_count;
        tree.right_child = reinterpret_cast<const int16_t *>(p);
        p += sizeof(int16_t) * tree.internal_count;
        p = align_ptr(p, 4);

        tree.threshold = reinterpret_cast<const float *>(p);
        p += sizeof(float) * tree.internal_count;
        tree.leaf_value = reinterpret_cast<const float *>(p);
        p += sizeof(float) * tree.leaf_count;

        if (p > end) {
            return false;
        }
    }

    return true;
}

bool ensure_models_mapped()
{
    if (g_models_mapped) {
        return true;
    }

    const int64_t start_us = esp_timer_get_time();
    const uint8_t *base = nutricook_models_bin_start;
    const uint8_t *end = nutricook_models_bin_end;
    if (end <= base + 8 || read_u32(base) != kBinaryMagic) {
        ESP_LOGE(TAG, "bad packed model blob");
        return false;
    }

    const uint32_t model_count = read_u32(base + 4);
    if (model_count != nutricook::kOutputCount) {
        ESP_LOGE(TAG, "unexpected model count: %lu", static_cast<unsigned long>(model_count));
        return false;
    }

    const uint8_t *header_ptr = base + 8;
    for (uint32_t i = 0; i < model_count; ++i) {
        BinaryModelHeader header = {};
        memcpy(&header, header_ptr + i * sizeof(BinaryModelHeader), sizeof(header));
        if (!map_one_model(i, base, end, header)) {
            ESP_LOGE(TAG, "failed to map packed model %lu", static_cast<unsigned long>(i));
            return false;
        }
    }

    g_models_mapped = true;
    ESP_LOGI(TAG, "binary models mapped in %.2f ms", static_cast<double>(esp_timer_get_time() - start_us) / 1000.0);
    return true;
}

float eval_tree(const BinaryTreeView &tree, const float features[nutricook::kFeatureCount])
{
    int node = 0;
    while (node >= 0 && node < static_cast<int>(tree.internal_count)) {
        const uint16_t index = static_cast<uint16_t>(node);
        const uint8_t feature = tree.split_feature[index];
        const float threshold = float_value(tree.threshold, index);
        const int16_t next = features[feature] <= threshold ? child_value(tree.left_child, index) : child_value(tree.right_child, index);
        if (next < 0) {
            const int leaf = -next - 1;
            return leaf >= 0 && leaf < static_cast<int>(tree.leaf_count) ? float_value(tree.leaf_value, static_cast<uint16_t>(leaf)) : NAN;
        }
        node = next;
    }
    return NAN;
}

float predict_one_model(size_t model_index, const float features[nutricook::kFeatureCount])
{
    const BinaryModelView &model = g_models[model_index];
    float sum = 0.0f;
    for (uint32_t i = 0; i < model.tree_count; ++i) {
        const float value = eval_tree(model.trees[i], features);
        if (!isnan(value)) {
            sum += value;
        }
    }
    return sum;
}

void fill_prediction(const float outputs[nutricook::kOutputCount], nutricook::Prediction *prediction)
{
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
}

bool predict_raw(const nutricook::IngredientWeight *items, size_t item_count, nutricook::Prediction *prediction)
{
    if (items == nullptr || prediction == nullptr || item_count == 0 || item_count > nutricook::kMaxIngredientsPerDish) {
        return false;
    }

    float outputs[nutricook::kOutputCount] = {};
    for (size_t i = 0; i < item_count; ++i) {
        const int ingredient_index = static_cast<int>(items[i].ingredient);
        const float weight = items[i].raw_weight_g;
        if (ingredient_index < 0 || ingredient_index >= static_cast<int>(nutricook::Ingredient::Count) || weight < 0.0f) {
            return false;
        }

        const float ratio = weight / 100.0f;
        outputs[0] += weight;
        for (size_t field = 0; field < 10; ++field) {
            outputs[field + 1] += nutricook::kRawNutritionPer100g[ingredient_index][field] * ratio;
        }
    }

    outputs[1] = 4.0f * outputs[2] + 9.0f * outputs[3] + 4.0f * outputs[4];
    fill_prediction(outputs, prediction);
    return true;
}

} // namespace

namespace nutricook {

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

    if (method == CookingMethod::Raw) {
        return predict_raw(items, item_count, prediction);
    }

    float features[kFeatureCount] = {};
    if (!build_features(items, item_count, method, features)) {
        return false;
    }

    if (!ensure_models_mapped()) {
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
    fill_prediction(outputs, prediction);
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
    if (method == CookingMethod::Raw) {
        return "raw";
    }
    static const char *kNames[] = {"boil", "braise", "deep_fry", "pan_fry", "roast", "steam", "stir_fry"};
    const int index = static_cast<int>(method);
    return index >= 0 && index < 7 ? kNames[index] : "unknown";
}

} // namespace nutricook


