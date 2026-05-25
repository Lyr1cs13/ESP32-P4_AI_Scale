# AI-Scale

AI-Scale 是基于 ESP32-P4、480x640 MIPI 触控屏和本地 NutriCook 轻量营养模型的智能营养秤原型。系统接收感知侧提供的食材种类与重量，用户在触控屏选择烹饪方式，设备端本地估算烹饪后的热量和主要营养信息。

当前版本是联调前版本，重点验证：

```text
食材识别 + 重量输入 + 触控选择 + 本地推理 + 屏幕显示
```

## 当前功能

- ESP32-P4 本地运行 NutriCook 营养估算模型。
- 支持 480x640 MIPI 触控屏中文 UI。
- 三页交互：
  - 页面 1：显示当前食材与总重量。
  - 页面 2：选择烹饪方式。
  - 页面 3：显示热量、成品重量、蛋白质、脂肪、碳水和供能占比。
- 支持串口文本模拟感知输入。
- 支持代码接口接收感知侧输入，便于后续与摄像头识别和重量传感器融合。
- 完整 11 项营养结果在设备端保留，可供后续上云模块读取。
- LightGBM 文本模型在构建阶段打包为二进制树表，运行时直接映射，避免上电后解析大文本模型。

## 硬件与环境

- ESP32-P4
- 32MB Flash
- 32MB PSRAM
- ST7701 MIPI 480x640 触控屏
- CST826/CST816 兼容触控控制器
- ESP-IDF v5.5.x
- LVGL 9.x

## 数据链路

```text
摄像头/YOLO 食材识别
重量传感器称重
        |
        v
食材英文名 + 重量 g
        |
        v
触控屏选择烹饪方式
        |
        v
ESP32-P4 本地 NutriCook 推理
        |
        v
屏幕显示 + 本地保存完整结果 + 后续上云
```

## 感知侧对接

当前预留了两种输入方式。

### 方式 1：串口模拟

用于快速联调，不需要接入摄像头和称重模块。

```text
chicken:150,potato:120,carrot:60
beef:120,tomato:80,onion:30
fish:200,ginger:10,garlic:8
tofu:150,cabbage:100
```

格式要求：

- 使用英文食材名。
- 重量单位固定为 g。
- 多个食材用 `,` 或 `;` 分隔。
- 食材名和重量用 `:`、`=` 或空格分隔。

### 方式 2：代码接口

感知侧完成 YOLO 识别和称重融合后，可以直接调用：

```c
#include "nutrition_app.h"

bool nutrition_update_ingredients_from_names(const char *const names[],
                                             const float weights_g[],
                                             size_t count);
```

示例：

```c
const char *names[] = {"chicken", "potato", "carrot"};
float weights[] = {150.0f, 120.0f, 60.0f};

nutrition_update_ingredients_from_names(names, weights, 3);
```

调用成功后，第一页会刷新食材和重量；如果用户已经选择烹饪方式，进入结果页后会重新推理。

对接约束：

- 单次最多 4 种食材。
- `names[]` 必须使用 NutriCook 支持的英文食材名。
- `weights_g[]` 单位固定为 g，且必须大于 0。
- 不支持的 YOLO 类别需要在感知侧或融合层过滤、提示或映射。

## 支持食材

```text
apple, banana, beef, bell_pepper, cabbage, carrot, cauliflower,
chicken, cucumber, egg, eggplant, fish, garlic, ginger, grape,
kiwi, kumquat, lemon, onion, orange, peach, pepper, pineapple,
pork, potato, shrimp, small_pepper, strawberry, tofu, tomato,
watermelon
```

## 支持烹饪方式

```text
raw, boil, braise, deep_fry, pan_fry, roast, steam, stir_fry
```

`raw` 表示不烹饪，按原始食材营养估算。

## 输出结果

屏幕重点显示：

- 热量 kcal
- 成品重量 g
- 蛋白质 g
- 脂肪 g
- 碳水 g
- 蛋白质/脂肪/碳水供能占比

完整结果包含 11 项：

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

后续上云模块可读取最近一次有效结果：

```c
#include "nutrition_app.h"

float outputs[NUTRITION_OUTPUT_COUNT];
bool ok = nutrition_copy_latest_result(outputs);
```

## 构建与烧录

首次或配置变化后建议干净构建：

```powershell
cd D:\Espressif\App\AI-Scale
idf.py fullclean
idf.py build
idf.py -p COM5 flash monitor
```

日常代码小改可直接：

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

不要日常使用 `erase-flash`，普通 `flash` 只写必要区域。

## 配置文件

- `sdkconfig.defaults`
- `sdkconfig.defaults.esp32p4`
- `partitions.csv`

当前工程使用 32MB Flash 和 31MB app 分区。NutriCook 模型二进制树表随固件打包，运行时映射加载。

## 模型说明

NutriCook 是轻量营养估算模型，适合本地快速估算，不等同于精确营养检测。当前部署没有对 LightGBM 树做量化、剪枝或近似替代，只是把文本模型离线打包为二进制树表，因此部署方式本身不会额外损失模型精度。

实际准确度主要受训练数据、食材识别准确率、重量传感器误差、烹饪方式简化建模和真实烹饪差异影响。

## 版本回退

字体显示确认正常的版本已打标签：

```powershell
git checkout ui-font-working-20260525
```

回到最新主线：

```powershell
git checkout main
git pull
```
