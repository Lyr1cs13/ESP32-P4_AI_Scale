#pragma once

#include <stddef.h>

namespace jiaofu {

static constexpr size_t kMaxIngredientsPerDish = 4;
static constexpr size_t kFeatureCount = 41;
static constexpr size_t kOutputCount = 11;

enum class Ingredient : int {
    Apple = 0,
    Banana,
    Beef,
    BellPepper,
    Cabbage,
    Carrot,
    Cauliflower,
    Chicken,
    Cucumber,
    Egg,
    Eggplant,
    Fish,
    Garlic,
    Ginger,
    Grape,
    Kiwi,
    Kumquat,
    Lemon,
    Onion,
    Orange,
    Peach,
    Pepper,
    Pineapple,
    Pork,
    Potato,
    Shrimp,
    SmallPepper,
    Strawberry,
    Tofu,
    Tomato,
    Watermelon,
    Count,
};

enum class CookingMethod : int {
    Boil = 0,
    Braise,
    DeepFry,
    PanFry,
    Roast,
    Steam,
    StirFry,
};

struct IngredientWeight {
    Ingredient ingredient;
    float raw_weight_g;
};

struct Prediction {
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
};

bool build_features(const IngredientWeight *items,
                    size_t item_count,
                    CookingMethod method,
                    float features[kFeatureCount]);

bool predict(const IngredientWeight *items, size_t item_count, CookingMethod method, Prediction *prediction);

const char *ingredient_name(Ingredient ingredient);
const char *cooking_method_name(CookingMethod method);

} // namespace jiaofu
