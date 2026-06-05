# AI-Scale-OH

AI-Scale-OH 是基于 ESP32-P4 的一体化 AI 营养秤工程，目标是在同一块板端完成：

```text
HX711 称重 + 摄像头采图 + YOLO 食物识别
        -> 屏幕选择烹饪方式
        -> NutriCook 本地营养模型推理
        -> 触控屏显示热量和主要营养信息
```

当前工程支持分阶段验证，也支持完整单板运行。

## 运行模式

运行模式通过 `idf.py menuconfig` 切换：

```text
AI Scale Run Mode
  Application run mode
```

三种模式如下。

| 模式 | 用途 | 跑通现象 |
|---|---|---|
| `Perception only` | 只验证摄像头、HX711、YOLO | 串口出现 `camera ready`、`HX711 ready`、`YOLO capture task started`；放入食材并稳定后出现 `Stored item` 和 `perception result` |
| `Display and nutrition only` | 只验证触摸屏、UI、NutriCook | 屏幕三页可滑动；串口输入食材后第一页刷新；选择烹饪方式后第三页显示热量和营养结果；串口出现 `prediction inference time` |
| `Full AI scale` | 完整整机模式 | 感知侧识别和称重结果自动刷新第一页；用户选择烹饪方式；第三页显示对应营养结果 |

默认是 `Full AI scale`。

如果只想快速改配置：

```powershell
idf.py menuconfig
idf.py build
idf.py -p COM5 flash monitor
```

## 串口模拟食材

在 `Display and nutrition only` 或完整模式下，可以用串口输入模拟感知侧结果：

```text
chicken:150,potato:120,carrot:60
beef:120,tomato:80,onion:30
fish:200,ginger:10,garlic:8
tofu:150,cabbage:100
```

规则：

- 食材名使用英文。
- 重量单位固定为 g，不需要写单位。
- 多个食材用 `,` 或 `;` 分隔。
- 食材名和重量可以用 `:`、`=` 或空格分隔。
- 单次最多 4 种食材。

## 工程结构

```text
main/
  main.c
    应用装配入口，根据运行模式启动感知侧、显示营养侧或完整工程。

components/board_display/
  HAL/
    MIPI DSI 屏幕、触摸、I2C、PCA9536 等板级显示驱动。

components/nutrition_ui/
  include/nutrition_app.h
  src/nutrition_app.cpp
  src/nutricook_ui_font_16.c
    三页触控 UI、中文字体、串口模拟输入、营养结果显示。

components/nutrition_model/
  include/nutricook_inference.hpp
  src/nutricook_inference.cpp
  src/nutricook_raw_table.hpp
    NutriCook 营养模型推理和模型二进制表加载。

components/perception/
  include/ai_scale_perception.h
  src/perception_camera.c
  src/food_yolo.cpp
  src/perception_bridge.c
  src/hx711.c
    摄像头、YOLO、HX711、感知结果聚合与对外回调。

models/
  food_yolo.espdl
    当前占位/验证用 YOLO 模型，烧录到 yolo_model 分区。后续真实模型可替换此文件。
```

## 分区

当前按 32MB Flash 设计：

```csv
nvs,        data, nvs,     0x14000, 0x6000
phy_init,   data, phy,     0x1a000, 0x1000
factory,    app,  factory, 0x20000, 0x1600000
yolo_model, data, spiffs,          , 0x600000
storage,    data, spiffs,          , 0x3E0000
```

构建后的烧录布局：

```text
0x2000     bootloader.bin
0x12000    partition-table.bin
0x20000    app.bin
0x1620000  models/food_yolo.espdl
```

后续 YOLO 模型变大时，优先调整 `partitions.csv` 的 `yolo_model` 分区大小。

## 构建

```powershell
cd D:\Espressif\App\AI-Scale-OH
idf.py build
idf.py -p COM5 flash monitor
```

日常开发不需要频繁 `erase-flash`。普通 `flash` 只会写入需要更新的区域。

## 对接说明

感知侧最终只需要把识别出的食材名和对应重量传给营养侧：

```c
nutrition_update_ingredients_from_names(names, weights_g, count);
```

当前完整模式已经通过 `components/perception/src/perception_bridge.c` 做了桥接：

```text
food_result_get_class_totals()
        -> perception_bridge
        -> nutrition_update_ingredients_from_names()
        -> UI 和 NutriCook 重新计算
```

感知侧模型后续替换时，保持输出食材英文名和重量即可，不需要改 NutriCook 模型。

## 回退点

本地标签：

```text
before-run-mode-switch
```

如需回到加入运行模式开关之前的组件化版本，可使用该标签定位。
