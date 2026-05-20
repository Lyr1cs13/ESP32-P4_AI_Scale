# AI-Scale

AI-Scale 是基于 ESP32-P4、480x640 MIPI 触控屏和本地轻量营养模型的智能营养秤原型。系统接收感知侧给出的食材种类与重量，用户在触控屏选择烹饪方式，然后在设备端估算烹饪后的热量和主要营养成分。

当前版本仍是联调前版本，重点验证“感知输入 + 触控选择 + 本地营养推理 + 屏幕显示”的完整链路。

## 当前功能

- ESP32-P4 + ST7701 MIPI 480x640 触控屏显示。
- 三页触控 UI：
  - 页面 1：显示当前称重食材种类、重量和总重量。
  - 页面 2：选择烹饪方式，包含“不烹饪/生食”选项。
  - 页面 3：显示热量、成品重量、蛋白质/脂肪/碳水克数和供能占比。
- 支持右滑进入下一页，第三页继续右滑回第一页；也支持左滑返回上一页。
- 串口文本模拟感知侧输入，便于快速联调。
- 集成 NutriCook LightGBM 营养估算模型。
- LightGBM 文本模型在构建阶段离线打包为二进制树表，运行时直接映射，避免首次推理时在 ESP32-P4 上解析大文本模型。
- 完整 11 项营养结果保存在设备端，供后续上云模块读取。
- 启用 LVGL SimSun CJK 字体，支持中文 UI 显示。

## 硬件平台

- ESP32-P4
- 32MB Flash
- 32MB PSRAM
- ST7701 MIPI 480x640 触控屏
- CST826/兼容触控控制器
- ESP-IDF v5.5.x
- LVGL 9.x

## 数据链路

```text
摄像头 YOLO 食材识别 + 重量传感器称重
        |
        v
食材种类 + 食材重量
        |
        v
触控屏选择烹饪方式
        |
        v
ESP32-P4 本地营养模型推理
        |
        v
屏幕显示 + 本地保存完整结果 + 后续上云
```

## 感知侧对接

感知侧同学负责输出秤上食材的信息：

```c
typedef struct {
    const char *name;   // NutriCook 支持的英文食材名，例如 "chicken"
    float weight_g;     // 重量，单位 g
} food_item_t;
```

约束：

- 单次最多 4 种食材。
- 食材名必须映射到 NutriCook 支持的 31 类食材之一。
- 重量单位固定为 g。
- YOLO 识别出不支持的类别时，需要在感知侧或融合层做过滤、提示或映射。

当前临时输入方式是串口文本：

```text
chicken:150,potato:120,carrot:60
beef:120,tomato:80,onion:30
fish:200,ginger:10,garlic:8
tofu:150,cabbage:100
```

## 支持的食材

```text
apple, banana, beef, bell_pepper, cabbage, carrot, cauliflower,
chicken, cucumber, egg, eggplant, fish, garlic, ginger, grape,
kiwi, kumquat, lemon, onion, orange, peach, pepper, pineapple,
pork, potato, shrimp, small_pepper, strawberry, tofu, tomato,
watermelon
```

## 支持的烹饪方式

```text
raw, boil, braise, deep_fry, pan_fry, roast, steam, stir_fry
```

`raw` 表示不烹饪，直接按原始食材营养表估算。

## 输出结果

模型完整输出 11 项：

```text
cooked_weight_g
cooked_energy_kcal
cooked_protein_g
cooked_fat_g
cooked_carbohydrate_g
cooked_sodium_mg
cooked_cholesterol_mg
cooked_vitamin_c_mg
cooked_calcium_mg
cooked_iron_mg
cooked_potassium_mg
```

屏幕只重点显示用户最直观看到的信息：热量、成品重量、蛋白质、脂肪、碳水及三者供能占比。完整结果可通过接口读取：

```c
bool nutrition_copy_latest_result(float outputs[11]);
```

## 构建

```powershell
cd D:\Espressif\App\AI-Scale
idf.py reconfigure
idf.py build
idf.py -p COM5 flash monitor
```

如果本地 `build` 目录来自其他工程，先换一个构建目录或执行清理：

```powershell
idf.py -B build-ai-scale build
```

本项目需要启用 PSRAM、32MB Flash、大 app 分区、LVGL 中文字体。相关默认配置在：

- `sdkconfig.defaults`
- `sdkconfig.defaults.esp32p4`
- `partitions.csv`

## 模型说明

NutriCook 是轻量营养估算模型，适合做本地快速估算，不等同于精确营养检测。当前部署没有对 LightGBM 树做量化、剪枝或近似替代，只是把文本模型离线打包为二进制树表，因此不会因为这次部署方式额外损失模型精度。

最终准确度主要受训练数据、YOLO 食材识别准确率、重量传感器误差、烹饪方式简化建模和实际烹饪差异影响。

