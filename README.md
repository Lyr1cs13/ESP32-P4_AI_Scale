# AI-Scale

AI-Scale 是一个基于 ESP32-P4 的智能营养秤原型项目。项目将触控屏交互、食材识别/称重输入、以及本地轻量营养预测模型组合起来，用于根据食材种类、重量和烹饪方式估算菜品的营养成分与热量。

当前版本是嵌入式端的非最终对接版本，重点验证以下链路：

1. 感知侧给出食材种类和重量。
2. 用户在触控屏上选择烹饪方式。
3. ESP32-P4 在本地运行营养评估模型。
4. 屏幕显示热量、蛋白质、脂肪、碳水等主要结果。
5. 完整 11 项营养预测结果保存在设备端，后续可用于上云和用户健康服务。

## 项目目标

最终系统希望形成如下流程：

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
本地营养模型推理
        |
        v
屏幕展示 + 后续上云 + 用户长期健康规划
```

## 当前已实现功能

- ESP32-P4 + ST7701 MIPI 触控屏基础显示与触摸交互。
- LVGL 两页式 UI：
  - 首页选择烹饪方式。
  - 结果页显示热量和主要宏量营养。
- 支持通过串口模拟感知侧输入食材种类和重量。
- 集成 jiaofu LightGBM 营养预测模型。
- 模型首次运行时加载到 PSRAM，后续推理直接使用解析后的树结构。
- 保存完整预测结果，预留给上云模块读取。

## 硬件平台

当前项目面向：

- ESP32-P4
- 32MB Flash
- 32MB PSRAM
- ST7701 MIPI 480x640 触控屏
- CST826/兼容触摸控制器
- ESP-IDF v5.5.x
- LVGL 9.x

## 营养模型输入

模型需要三类输入：

1. 食材种类
2. 每种食材重量，单位 g
3. 烹饪方式

当前支持最多 4 种食材。食材名称使用英文枚举，来自 jiaofu 模型特征：

```text
apple, banana, beef, bell_pepper, cabbage, carrot, cauliflower,
chicken, cucumber, egg, eggplant, fish, garlic, ginger, grape,
kiwi, kumquat, lemon, onion, orange, peach, pepper, pineapple,
pork, potato, shrimp, small_pepper, strawberry, tofu, tomato,
watermelon
```

当前支持的烹饪方式：

```text
boil, braise, deep_fry, pan_fry, roast, steam, stir_fry
```

## 串口模拟输入

在最终感知侧接口完成前，当前版本用串口模拟食材识别与称重结果。

输入格式：

```text
食材名:重量,食材名:重量,食材名:重量
```

示例：

```text
chicken:150,potato:120,carrot:60
beef:120,tomato:80,onion:30
fish:200,ginger:10,garlic:8
tofu:150,cabbage:100
```

输入后在屏幕首页点击 `Analyze`，设备会结合当前触控选择的烹饪方式进行营养预测。

## 感知侧对接说明

感知侧负责摄像头和重量传感器侧：

- 摄像头本地 YOLO 模型识别秤上的食品种类。
- 重量传感器提供每种食品的重量。
- 将识别结果整理为 AI-Scale 可接收的数据。

建议对接数据结构：

```c
typedef struct {
    const char *name;      // 食材英文名，例如 "chicken"
    float weight_g;        // 重量，单位 g
} food_item_t;
```

约束：

- 单次最多传入 4 种食材。
- 食材名称必须能映射到 jiaofu 支持的 31 种食材之一。
- 重量单位固定为 g。
- 如果 YOLO 识别出不支持的食材，需要在感知侧或融合层做过滤、提示或映射。

当前临时对接方式是串口文本。后续可以替换为：

- UART 二进制协议
- FreeRTOS 队列
- 共享 C 接口
- 网络/本地消息协议

## 输出结果

模型完整输出 11 项营养数据：

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

屏幕当前重点展示人最直观看到的信息：

- 热量 kcal
- 蛋白质 g
- 脂肪 g
- 碳水 g
- 蛋白质/脂肪/碳水的热量占比

完整 11 项结果保存在设备端，供后续上云模块读取。

当前预留接口：

```c
bool nutrition_copy_latest_result(float outputs[11]);
```

返回 `true` 表示已经有可用预测结果。

## 模型精度说明

jiaofu 是轻量营养估算模型，适合做快速本地估算，不等同于精确营养检测。

当前 ESP32-P4 端没有对模型做量化、剪枝或近似替代，推理逻辑直接还原 LightGBM 树结构，因此部署本身不会额外引入明显精度损失。最终准确度主要受训练数据、食材识别准确率、重量传感器误差、烹饪方式简化建模等因素影响。

## 构建与烧录

进入项目目录：

```powershell
cd D:\Espressif\App\AI-Scale
```

配置、构建、烧录：

```powershell
idf.py reconfigure
idf.py build
idf.py -p COM5 flash monitor
```

首次运行时模型会加载到 PSRAM，耗时会比普通 UI demo 更长。模型加载完成后，后续单次推理会明显更快。

## 目录说明

```text
AI-Scale/
├── main/
│   ├── main.c                    # 屏幕初始化入口
│   ├── nutrition_app.cpp          # 营养秤 UI、串口模拟输入、推理调度
│   ├── jiaofu_inference.cpp       # jiaofu 模型推理实现
│   ├── jiaofu_inference.hpp       # 模型输入输出接口
│   └── HAL/                       # 屏幕、触摸、I2C 等硬件驱动
├── jiaofu/
│   ├── models/                    # LightGBM 文本模型
│   └── final.json                 # 食材营养基础数据
├── partitions.csv                 # 32MB Flash 大 app 分区
├── sdkconfig.defaults.esp32p4     # ESP32-P4 默认配置
└── CMakeLists.txt
```

## 当前版本边界

当前不是最终联调版本，仍有以下待完善内容：

- 感知侧真实数据接口尚未最终确定，目前用串口模拟。
- YOLO 食材类别与 jiaofu 支持食材之间的映射策略需要双方统一。
- 上云协议、用户账户、长期健康规划服务暂未实现。
- UI 当前只展示主要营养信息，完整数据已保存但未上传。
- 模型仍是轻量估算模型，后续需要结合真实样本继续校准。

## 后续计划

- 与摄像头/重量传感器模块完成真实数据对接。
- 增加食材识别置信度和异常提示。
- 增加云端上传接口。
- 设计用户端营养记录、趋势分析和长期健康规划服务。
- 将 LightGBM 文本模型离线转换为更紧凑的二进制树表，缩短首次加载时间。
