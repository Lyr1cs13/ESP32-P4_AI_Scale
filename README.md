# AI-Scale-OH

AI-Scale-OH 是基于 ESP32-P4、480x640 MIPI 触控屏、摄像头、HX711 重量传感器、YOLO 食物识别模型和 NutriCook 营养评估模型的一体化 AI 营养秤工程。

当前工程已经把原本独立的显示营养侧和感知侧合并到同一块 ESP32-P4 板端运行：

```text
HX711称重 -> 重量稳定触发 -> 摄像头采一帧 -> YOLO识别食物
        -> 聚合食物种类和重量 -> 屏幕选择烹饪方式
        -> NutriCook本地推理 -> 屏幕显示营养结果
```

## 当前功能

- 480x640 MIPI 触控屏三页交互：
  - 页面 1：显示当前秤上食材和重量。
  - 页面 2：选择烹饪方式，包含“不烹饪”。
  - 页面 3：显示热量、成品重量、蛋白质、脂肪、碳水和供能占比。
- HX711 重量稳定检测。
- 摄像头单帧采集，不保留网页视频流。
- YOLO 食物识别模型通过独立 `yolo_model` 分区加载。
- NutriCook 营养模型打包进 app 固件，运行时直接映射。
- 感知结果自动调用营养侧接口刷新 UI。
- 串口模拟输入仍保留，便于没有接传感器时快速验证。
- 完整 11 项营养结果保存在设备端，预留给后续上云模块读取。

## 工程结构

```text
main/
  main.c                    屏幕初始化、感知回调注册、应用启动
  nutrition_app.cpp         三页触控 UI、串口模拟输入、NutriCook 调用
  nutricook_inference.*     NutriCook 模型推理

components/perception/
  include/ai_scale_perception.h     感知组件公开接口
  src/perception_camera.c           摄像头单帧采集、HX711触发、任务启动
  src/food_yolo.cpp                 YOLO模型加载、识别、结果记录
  src/perception_bridge.c           食材聚合结果发布给营养侧
  src/hx711.c                       HX711驱动

models/
  food_yolo.espdl            食物识别模型，烧录到 yolo_model 分区
```

## RTOS 调度

当前采用事件驱动，不做连续视频流，避免摄像头、YOLO、LVGL 和 NutriCook 抢资源。

| 任务 | 职责 | 优先级 | 说明 |
|---|---|---:|---|
| LVGL | 触控和屏幕刷新 | 驱动默认 | 不允许感知任务直接操作 LVGL 对象 |
| `weight_sensor` | HX711称重、稳定判断 | 3 | 100ms 周期，默认 GPIO22/GPIO23 |
| `food_yolo` | YOLO 推理 | 5 | 固定 Core 1，队列长度 1，避免推理堆积 |
| `perception_bridge` | 聚合结果转给营养侧 | 3 | 通过回调调用营养侧接口 |
| `nutrition_serial` | 串口模拟输入 | 4 | 调试入口 |
| `nutrition_infer` | NutriCook 推理 | 5 | 仅在结果页需要时触发 |

YOLO 推理期间会设置 busy 标记，新的重量稳定事件会被跳过，避免最新帧缓冲被覆盖。NutriCook 推理很轻，只在烹饪方式或食材变化后重新计算。

## Flash 分区

当前按 32MB Flash 设计：

```csv
nvs,        data, nvs,     0x14000, 0x6000
phy_init,   data, phy,     0x1a000, 0x1000
factory,    app,  factory, 0x20000, 0x1600000
yolo_model, data, spiffs,          , 0x600000
storage,    data, spiffs,          , 0x3E0000
```

构建结果中 app 约 4.8MB，22MB app 分区剩余约 78%。YOLO 模型单独烧录到 `0x1620000`，后续替换识别模型不需要改 NutriCook 代码。

## 串口模拟输入

没有接入传感器时，可以通过串口输入：

```text
chicken:150,potato:120,carrot:60
beef:120,tomato:80,onion:30
fish:200,ginger:10,garlic:8
tofu:150,cabbage:100
```

格式要求：

- 食材名使用英文。
- 重量单位固定为 g。
- 多个食材用 `,` 或 `;` 分隔。
- 食材名和重量可用 `:`、`=` 或空格分隔。
- 单次最多 4 种食材。

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

`raw` 表示不烹饪。

## 构建与烧录

首次从复制工程构建时，建议删除旧 build 缓存：

```powershell
cd D:\Espressif\App\AI-Scale-OH
idf.py fullclean
idf.py build
idf.py -p COM5 flash monitor
```

当前构建生成的烧录布局为：

```text
0x2000     bootloader.bin
0x12000    partition-table.bin
0x20000    app.bin
0x1620000  models/food_yolo.espdl
```

日常小改可直接：

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

不要日常使用 `erase-flash`。普通 `flash` 只写必要区域。

## 模型说明

NutriCook 是轻量营养估算模型，不等同于精确营养检测。当前部署没有对 NutriCook 做量化、剪枝或近似替代，只是把文本模型离线打包成二进制树表，因此部署方式本身不会额外损失模型精度。

实际结果主要受以下因素影响：

- 食物识别准确率。
- HX711 标定与称重误差。
- 食材数据库和训练数据质量。
- 烹饪方式被简化建模。
- 真实烹饪中水分、油脂、调料吸收差异。

