# AI-Scale-OH

AI-Scale-OH是基于ESP32-P4的端侧智能营养秤工程，用于在同一块板端完成食材感知、称重、触控交互、营养模型推理和结果显示。

整体流程：

```text
摄像头采集+YOLO食材识别+HX711称重
        -> 触控屏选择烹饪方式
        -> NutriCook本地营养模型推理
        -> 屏幕显示热量、成品重量和主要营养信息
```

当前工程已经把`ESP32-P4_AI_Scale-lyy`感知侧逻辑和原显示营养侧逻辑合并到同一工程中，并做了组件化整理。

## 运行模式

通过`idf.py menuconfig`切换：

```text
AI Scale Run Mode
  Application run mode
```

三种模式：

| 模式 | 用途 | 跑通现象 |
|---|---|---|
| `Perception only` | 只验证摄像头、HX711、YOLO感知侧 | 串口出现摄像头、称重和YOLO任务启动日志；放入食材并稳定后输出识别和重量结果 |
| `Display and nutrition only` | 只验证触控屏、UI、NutriCook营养模型 | 屏幕三页可滑动；串口输入食材后第一页刷新；选择烹饪方式后第三页显示热量和营养结果；串口出现`prediction inference time` |
| `Full AI scale` | 完整单板运行 | 感知侧识别和称重结果自动刷新到第一页；用户选择烹饪方式；第三页显示对应营养结果 |

默认建议使用`Full AI scale`。分阶段调试时先跑`Perception only`和`Display and nutrition only`，两侧都正常后再跑完整模式。

## 串口模拟输入

在`Display and nutrition only`或完整模式中，可以用串口模拟感知侧输出：

```text
chicken:150,potato:120,carrot:60
beef:120,tomato:80,onion:30
fish:200,ginger:10,garlic:8
tofu:150,cabbage:100
```

规则：

- 食材名使用英文。
- 重量单位固定为g，不需要写单位。
- 多个食材用`,`或`;`分隔。
- 食材名和重量可用`:`、`=`或空格分隔。
- 单次最多支持4种食材。

## 工程结构

```text
main/
  main.c
    应用入口，根据运行模式启动感知侧、显示营养侧或完整工程。

components/board_display/
  HAL/
    MIPI DSI屏幕、触摸、I2C、PCA9536等板级显示驱动。

components/nutrition_ui/
  include/nutrition_app.h
  src/nutrition_app.cpp
  src/nutricook_ui_font_16.c
    三页触控UI、中文字体、串口模拟输入、营养结果显示。

components/nutrition_model/
  include/nutricook_inference.hpp
  src/nutricook_inference.cpp
  src/nutricook_raw_table.hpp
    NutriCook营养模型推理和二进制树表加载。

components/perception/
  include/ai_scale_perception.h
  src/perception_camera.c
  src/food_yolo.cpp
  src/perception_bridge.c
  src/hx711.c
    摄像头、YOLO、HX711、感知结果聚合与对外回调。

components/example_video_common/
    摄像头初始化公共逻辑。当前工程复用屏幕侧I2C总线，避免触摸和摄像头SCCB重复占用I2C。

third_party/esp-dl/
  esp-dl/
  dl_fft/
    随工程携带的ESP-DL依赖，避免依赖工程外部的ESP-DL目录。

managed_components/
    随工程携带的ESP-IDF组件管理器依赖，降低迁移时重新下载依赖的风险。

models/
  food_yolo.espdl
    当前食材YOLO模型，烧录到`yolo_model`分区。

flash_package/
    可直接转发给其他电脑的烧录文件和脚本。
```

## 耦合方式

工程使用FreeRTOS任务、队列、互斥锁和回调完成耦合。

- 感知侧负责摄像头采集、YOLO识别、HX711称重和稳定性判断。
- `perception_bridge`把食材类别和重量聚合成营养侧需要的输入。
- 显示营养侧负责LVGL触控交互、烹饪方式选择和NutriCook推理。
- 完整模式下，感知结果通过桥接回调刷新UI输入；用户选择烹饪方式后触发营养计算。
- 摄像头初始化失败时不再导致整机重启，屏幕和营养侧仍可继续用于串口模拟验证。

## 分区与烧录包

关键分区：

```text
factory app  -> 0x20000
yolo_model   -> 0x620000
```

最新可转发烧录包位于：

```text
flash_package/
flash_package.zip
```

默认烧录串口为`COM7`。解压后在`flash_package`目录运行：

```powershell
.\flash.ps1
```

或：

```cmd
flash.bat
```

手动烧录命令：

```powershell
python -m esptool --chip esp32p4 -p COM7 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 32MB 0x2000 bootloader.bin 0x12000 partition-table.bin 0x20000 app.bin 0x620000 food_yolo.espdl
```

## 迁移与复现

- 源码构建需要ESP-IDF v5.5.4或兼容版本。
- 工程内已经包含`third_party/esp-dl`，不再依赖工程外部的ESP-DL目录。
- 工程内保留`managed_components`和`sdkconfig`，用于降低不同电脑重新配置导致的差异。
- `build/`目录不作为源码交付内容，它包含本机CMake缓存；换机器构建时应重新执行`idf.py build`。
- 只需要直接烧录时，使用`flash_package.zip`即可，不需要完整源码和`build/`目录。
