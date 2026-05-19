# 烹饪营养预测模型 — ESP32 部署交付文档

> 版本: 1.0 | 日期: 2026-04-28 | 模型目录: Stephen

---

## 1. 项目概述

本模型根据用户输入的食材组合、重量和烹饪方式，预测烹饪后的11项营养数据。

- **训练框架**: LightGBM (11个独立回归模型)
- **训练数据**: Kevin.csv (50,000条合成数据)
- **平均 R²**: 0.9385
- **特征维度**: 41维
- **输出维度**: 11维

---

## 2. 文件清单

```
jiaofu/
├── models/                          # 11个 LightGBM 模型文件
│   ├── lgbm_cooked_weight_g.txt
│   ├── lgbm_cooked_energy_kcal.txt
│   ├── lgbm_cooked_protein_g.txt
│   ├── lgbm_cooked_fat_g.txt
│   ├── lgbm_cooked_carbohydrate_g.txt
│   ├── lgbm_cooked_sodium_mg.txt
│   ├── lgbm_cooked_cholesterol_mg.txt
│   ├── lgbm_cooked_vitamin_c_mg.txt
│   ├── lgbm_cooked_calcium_mg.txt
│   ├── lgbm_cooked_iron_mg.txt
│   └── lgbm_cooked_potassium_mg.txt
├── final.json                       # 31种食材营养数据库 (每100g可食部)
├── model_meta.json                  # 模型元数据 (特征列、指标等)
├── predict_30.py                    # 验证脚本 (30道随机菜预测)
├── generate_training_data.py        # 训练数据生成脚本 (参考用)
├── README.md                        # 本文档
└── esp32_inference.h                # C语言推理代码模板
```

---

## 3. 特征构建 (41维输入)

嵌入式端必须按以下顺序构建41维 float 数组，作为11个模型的输入:

### 3.1 特征列表 (按索引)

| 索引 | 字段名 | 类型 | 说明 |
|------|--------|------|------|
| 0 | ing_apple | float | 苹果 one-hot (0/1) |
| 1 | ing_banana | float | 香蕉 one-hot (0/1) |
| 2 | ing_beef | float | 牛肉 one-hot (0/1) |
| 3 | ing_bell_pepper | float | 甜椒 one-hot (0/1) |
| 4 | ing_cabbage | float | 卷心菜 one-hot (0/1) |
| 5 | ing_carrot | float | 胡萝卜 one-hot (0/1) |
| 6 | ing_cauliflower | float | 花菜 one-hot (0/1) |
| 7 | ing_chicken | float | 鸡肉 one-hot (0/1) |
| 8 | ing_cucumber | float | 黄瓜 one-hot (0/1) |
| 9 | ing_egg | float | 鸡蛋 one-hot (0/1) |
| 10 | ing_eggplant | float | 茄子 one-hot (0/1) |
| 11 | ing_fish | float | 鱼肉 one-hot (0/1) |
| 12 | ing_garlic | float | 大蒜 one-hot (0/1) |
| 13 | ing_ginger | float | 生姜 one-hot (0/1) |
| 14 | ing_grape | float | 葡萄 one-hot (0/1) |
| 15 | ing_kiwi | float | 猕猴桃 one-hot (0/1) |
| 16 | ing_kumquat | float | 金橘 one-hot (0/1) |
| 17 | ing_lemon | float | 柠檬 one-hot (0/1) |
| 18 | ing_onion | float | 洋葱 one-hot (0/1) |
| 19 | ing_orange | float | 柑橘 one-hot (0/1) |
| 20 | ing_peach | float | 桃 one-hot (0/1) |
| 21 | ing_pepper | float | 胡椒 one-hot (0/1) |
| 22 | ing_pineapple | float | 菠萝 one-hot (0/1) |
| 23 | ing_pork | float | 猪肉 one-hot (0/1) |
| 24 | ing_potato | float | 马铃薯 one-hot (0/1) |
| 25 | ing_shrimp | float | 虾 one-hot (0/1) |
| 26 | ing_small_pepper | float | 小米椒 one-hot (0/1) |
| 27 | ing_strawberry | float | 草莓 one-hot (0/1) |
| 28 | ing_tofu | float | 豆腐 one-hot (0/1) |
| 29 | ing_tomato | float | 番茄 one-hot (0/1) |
| 30 | ing_watermelon | float | 西瓜 one-hot (0/1) |
| 31 | raw_weight_1 | float | 第1种食材重量 (g) |
| 32 | raw_weight_2 | float | 第2种食材重量 (g), 无则0 |
| 33 | raw_weight_3 | float | 第3种食材重量 (g), 无则0 |
| 34 | raw_weight_4 | float | 第4种食材重量 (g), 无则0 |
| 35 | raw_weight_total | float | 总生重 (g) |
| 36 | raw_weight_mean | float | 平均生重 (g) |
| 37 | n_ingredients | float | 食材数量 (1~4) |
| 38 | has_fruit | float | 含水果标志 (0/1) |
| 39 | has_meat | float | 含肉类标志 (0/1) |
| 40 | cooking_method_enc | float | 烹饪方式编码 (0~6) |

### 3.2 烹饪方式编码

| 编码 | 方式 | 中文 |
|------|------|------|
| 0 | boil | 煮 |
| 1 | braise | 红烧/炖 |
| 2 | deep_fry | 炸 |
| 3 | pan_fry | 煎 |
| 4 | roast | 烤 |
| 5 | steam | 蒸 |
| 6 | stir_fry | 炒 |

### 3.3 食材分类 (用于 has_fruit / has_meat)

**水果类 (11种):**
```
apple, banana, grape, kiwi, kumquat, lemon,
orange, peach, pineapple, strawberry, watermelon
```

**肉类 (5种):**
```
beef, chicken, pork, shrimp, fish
```

### 3.4 食材ID映射表

用于将食材名称映射到 one-hot 索引:

| ID | 英文名 | 中文名 |
|----|--------|--------|
| 0 | apple | 苹果 |
| 1 | banana | 香蕉 |
| 2 | beef | 牛肉 |
| 3 | bell_pepper | 甜椒 |
| 4 | cabbage | 卷心菜 |
| 5 | carrot | 胡萝卜 |
| 6 | cauliflower | 花菜 |
| 7 | chicken | 鸡肉 |
| 8 | cucumber | 黄瓜 |
| 9 | egg | 鸡蛋 |
| 10 | eggplant | 茄子 |
| 11 | fish | 鱼肉 |
| 12 | garlic | 大蒜 |
| 13 | ginger | 生姜 |
| 14 | grape | 葡萄 |
| 15 | kiwi | 猕猴桃 |
| 16 | kumquat | 金橘 |
| 17 | lemon | 柠檬 |
| 18 | onion | 洋葱 |
| 19 | orange | 柑橘 |
| 20 | peach | 桃 |
| 21 | pepper | 胡椒 |
| 22 | pineapple | 菠萝 |
| 23 | pork | 猪肉 |
| 24 | potato | 马铃薯 |
| 25 | shrimp | 虾 |
| 26 | small_pepper | 小米椒 |
| 27 | strawberry | 草莓 |
| 28 | tofu | 豆腐 |
| 29 | tomato | 番茄 |
| 30 | watermelon | 西瓜 |

---

## 4. 模型输出 (11维)

| 索引 | 输出名 | 单位 | 说明 |
|------|--------|------|------|
| 0 | cooked_weight_g | g | 烹饪后总重量 |
| 1 | cooked_energy_kcal | kcal | 烹饪后热量 |
| 2 | cooked_protein_g | g | 烹饪后蛋白质 |
| 3 | cooked_fat_g | g | 烹饪后脂肪 |
| 4 | cooked_carbohydrate_g | g | 烹饪后碳水化合物 |
| 5 | cooked_sodium_mg | mg | 烹饪后钠 |
| 6 | cooked_cholesterol_mg | mg | 烹饪后胆固醇 |
| 7 | cooked_vitamin_c_mg | mg | 烹饪后维生素C |
| 8 | cooked_calcium_mg | mg | 烹饪后钙 |
| 9 | cooked_iron_mg | mg | 烹饪后铁 |
| 10 | cooked_potassium_mg | mg | 烹饪后钾 |

---

## 5. 后处理规则 (必须执行!)

11个模型独立预测，可能出现物理不合理的结果。**推理后必须执行以下修正:**

### 5.1 裁剪非负

```c
for (int i = 0; i < 11; i++) {
    if (results[i] < 0.0f) results[i] = 0.0f;
}
```

### 5.2 能量用 Atwater 公式重算 (覆盖模型输出)

```c
// energy_kcal = 4 * protein_g + 9 * fat_g + 4 * carbohydrate_g
results[1] = 4.0f * results[2] + 9.0f * results[3] + 4.0f * results[4];
```

> 此步骤彻底消除 Atwater 偏差，保证能量守恒。

---

## 6. 模型精度指标

| 目标 | R² | RMSE | MAPE |
|------|-----|------|------|
| weight_g | 0.9973 | 16.76 | 4.1% |
| energy_kcal | 0.9459 | 116.48 | 17.2% |
| protein_g | 0.9252 | 9.04 | 18.2% |
| fat_g | 0.9299 | 11.23 | 27.8% |
| carbohydrate_g | 0.9518 | 6.35 | 13.8% |
| sodium_mg | 0.9430 | 378.33 | 27.3% |
| cholesterol_mg | 0.9072 | 171.43 | 33.7% |
| vitamin_c_mg | 0.8898 | 21.76 | 35.7% |
| calcium_mg | 0.9170 | 48.46 | 19.8% |
| iron_mg | 0.9376 | 1.11 | 12.8% |
| potassium_mg | 0.9788 | 83.09 | 7.3% |
| **平均** | **0.9385** | | **19.8%** |

---

## 7. ESP32 部署步骤

### 7.1 模型转换 (在PC上完成)

```bash
pip install m2cgen
```

```python
import lightgbm as lgb
import m2cgen as m2c

targets = [
    "cooked_weight_g", "cooked_energy_kcal", "cooked_protein_g",
    "cooked_fat_g", "cooked_carbohydrate_g", "cooked_sodium_mg",
    "cooked_cholesterol_mg", "cooked_vitamin_c_mg", "cooked_calcium_mg",
    "cooked_iron_mg", "cooked_potassium_mg",
]

for target in targets:
    model = lgb.Booster(model_file=f"models/lgbm_{target}.txt")
    code = m2c.export_to_c(model)
    with open(f"esp32_model_{target}.c", "w") as f:
        f.write(code)
```

### 7.2 内存评估

| 项目 | 估算大小 |
|------|---------|
| 11个模型决策树 (C代码) | ~100~300 KB (Flash) |
| 41维输入数组 | 164 bytes (RAM) |
| 11个输出值 | 44 bytes (RAM) |
| 推理栈空间 | ~几 KB (RAM) |

ESP32-P4: 768KB SRAM + 外部 PSRAM, **内存充足**。

### 7.3 集成验证

1. 在PC上运行 `python predict_30.py` 得到30道菜的预测基准值
2. 在ESP32上用相同输入运行C推理
3. 对比结果,差异应 < 0.1% (浮点精度)

---

## 8. 注意事项

1. **食材最多4种**: 特征设计中 `raw_weight_1~4` 只有4个位置,超过4种食材需扩展特征
2. **香辛料重量**: pepper(胡椒)正常用量0.5~5g, ginger(生姜)/garlic(大蒜)5~30g, 输入时应注意
3. **final.json 是参考数据库**: 包含31种食材每100g的完整营养数据,可用于UI显示或额外计算,推理时不直接使用
4. **模型不依赖final.json**: 模型输入是41维特征向量,不读取final.json
5. **能量不要直接用模型输出**: 必须用Atwater公式重算,否则可能出现物理不一致
