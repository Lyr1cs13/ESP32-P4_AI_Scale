# AI-Scale-OH

基于ESP32-P4的端侧智能营养秤工程。项目在同一块板端完成摄像头食材识别、HX711称重、触控屏烹饪方式选择、NutriCook营养模型推理和结果显示。

## 功能流程

```text
摄像头采集 + YOLO食材识别 + HX711称重
        -> 触控屏选择烹饪方式
        -> NutriCook本地营养模型推理
        -> 屏幕显示热量、成品重量和主要营养信息
        -> 秤面归零后通过板载ESP32-C6上传完整餐食记录
```

当前工程已经把感知侧、显示营养侧和云端上传耦合到同一ESP32-P4工程中。称重结果按当前秤面实际重量刷新，支持增重、减重和清空；烹饪方式确认后生成本次餐食记录，未归零前继续变化会覆盖更新本次记录，归零后结束本次餐食并异步上云。本地保留最近5次餐食记录。

## 上云逻辑

ESP32-P4通过板载ESP32-C6和ESP-Hosted连接Wi-Fi。网络请求由独立的`cloud_upload`低优先级任务执行，不阻塞触控、称重、摄像头或模型推理。

餐食归零结束后，设备上传食材名称、各食材原始重量、烹饪方式、成品重量、热量及全部营养模型输出。接口默认使用`user_id=15`。上传失败时保留当前队列记录并按配置间隔持续重试，HTTP 2xx后才处理下一条记录。

通过`idf.py menuconfig`修改：

```text
AI Scale Cloud Upload
  Wi-Fi SSID
  Wi-Fi password
  Meal record API URL
  Cloud user ID
```

上云成功日志：

```text
cloud_upload: HTTP status=201 response=...
cloud_upload: MEAL_UPLOAD_SUCCEEDED
```

## 环境要求

- ESP-IDF：推荐v5.5.4，最低要求v5.5系列。
- 目标芯片：esp32p4。
- 首次源码构建需要联网下载ESP-IDF组件管理器依赖。
- 工程已经随仓库包含`third_party/esp-dl`和`models/food_yolo.espdl`，不需要额外下载ESP-DL或YOLO模型。

不要提交或复用别人机器上的`build/`、`sdkconfig`、`managed_components/`目录；这些都是本机生成内容。换电脑后直接重新构建即可。

## 快速构建烧录

在ESP-IDF环境已导出的终端中执行：

```powershell
git clone https://github.com/Lyr1cs13/ESP32-P4_AI_Scale.git
cd ESP32-P4_AI_Scale
idf.py set-target esp32p4
idf.py build
idf.py -p COM7 flash monitor
```

如果串口不是`COM7`，把命令里的`COM7`改成实际端口，例如`COM5`。

烧录内容包括：

```text
0x2000   bootloader.bin
0x12000  partition-table.bin
0x20000  app.bin
0x620000 models/food_yolo.espdl
```

`idf.py flash`会根据工程中的分区和`esptool_py_flash_to_partition()`自动把`food_yolo.espdl`写入`yolo_model`分区。

## 运行模式

通过`idf.py menuconfig`切换：

```text
AI Scale Run Mode
  Application run mode
```

| 模式 | 用途 | 跑通现象 |
|---|---|---|
| `Perception only` | 只验证摄像头、HX711、YOLO感知侧 | 串口出现摄像头、称重和YOLO任务启动日志；放入食材并稳定后输出识别类别和重量 |
| `Display and nutrition only` | 只验证触控屏、UI、NutriCook营养模型 | 屏幕三页可滑动；串口输入食材后第一页刷新；选择烹饪方式后第三页显示热量和营养结果 |
| `Full AI scale` | 完整单板运行 | 感知侧识别和称重结果自动刷新到第一页；用户选择烹饪方式后第三页显示对应营养结果 |

默认建议使用`Full AI scale`。分阶段排查时先跑`Perception only`和`Display and nutrition only`，两侧都正常后再跑完整模式。

## 串口模拟输入

在`Display and nutrition only`或完整模式中，可以用串口模拟感知侧输入：

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
    三页触控UI、中文字体、串口模拟输入、餐食记录和营养结果显示。

components/nutrition_model/
  include/nutricook_inference.hpp
  src/nutricook_inference.cpp
  src/nutricook_raw_table.hpp
    NutriCook营养模型推理和二进制树表加载。

components/cloud_upload/
  include/cloud_upload.h
  src/cloud_upload.c
    ESP32-C6远程Wi-Fi、餐食上传队列、JSON构建、HTTP请求和失败重试。

components/perception/
  include/ai_scale_perception.h
  src/perception_camera.c
  src/food_yolo.cpp
  src/perception_bridge.c
  src/hx711.c
    摄像头、YOLO、HX711、当前秤面食材快照和对外回调。

components/example_video_common/
    摄像头初始化公共逻辑。

third_party/esp-dl/
  esp-dl/
  dl_fft/
    随工程携带的ESP-DL依赖，避免依赖工程外部ESP-DL目录。

models/
  food_yolo.espdl
    当前食材YOLO模型，烧录到`yolo_model`分区。
```

## 耦合方式

工程使用FreeRTOS任务、队列、互斥锁和回调完成耦合。

- `weight_sensor`任务读取HX711，判断稳定重量变化。
- 增重时提交最新摄像头帧到`food_yolo`任务进行识别。
- 减重时不重复识别，直接按当前秤面快照扣减并校准到实际总重。
- 空秤时清空当前食材快照，并通知UI结束当前餐食。
- `perception_bridge`把食材类别和重量聚合为营养模型输入。
- `nutrition_ui`负责触控交互、餐食生命周期、NutriCook推理和结果显示。
- `cloud_upload`通过独立队列接收最终餐食记录，在低优先级任务中联网、上传和重试。

## 迁移注意

- 不需要本机绝对路径。
- 不需要预先复制`managed_components/`。
- 不需要手动烧录`build/`中的旧产物。
- 首次构建如果组件下载失败，先确认电脑可以访问`components.espressif.com`。
- 如果移动工程目录后CMake报旧路径缓存，删除`build/`后重新执行`idf.py build`。
