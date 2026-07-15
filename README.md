# AI-Scale-OH

基于ESP32-P4的端云协同智能营养秤工程。项目在同一块ESP32-P4上完成摄像头食材识别、HX711称重、触控屏交互、NutriCook营养推理、餐食记录管理，并通过板载ESP32-C6将完整营养结果上传云端。

## 功能流程

```text
HX711真实称重触发
  -> 摄像头采集
  -> YOLO食材识别
  -> 触控屏选择烹饪方式
  -> NutriCook端侧营养模型推理
  -> 屏幕显示热量、成品重量和主要营养结果
  -> 秤面归零后通过ESP32-C6异步上云
```

主要功能：

- MIPI-CSI摄像头与ESP-DL YOLO模型识别食材类别。
- HX711获取实际重量，支持增重、减重和清空。
- 减重时按完整已放食材项匹配移除，避免把所有食材克数按比例错误缩小。
- 480x640触控屏提供三页交互：称重信息、烹饪方式、营养结果。
- NutriCook在端侧输出成品重量、热量和11项完整营养数据。
- 营养模型使用离线二进制树表，避免启动时全量扫描文本模型。
- 本地维护餐食生命周期，保留最近5次餐食记录。
- 云上传在独立FreeRTOS任务中执行，不阻塞称重、触控或模型推理。

## 硬件与环境

硬件：

- 主控：ESP32-P4，32MB Flash，32MB PSRAM。
- 显示：ST7701 MIPI-DSI 480x640触控屏，CST816S触控控制器。
- 摄像头：当前配置支持SC2336和OV5647自动检测。
- 称重：HX711，默认`DOUT=GPIO22`、`SCK=GPIO23`。
- 联网：板载ESP32-C6，通过SDIO运行ESP-Hosted协处理器固件。

软件：

- ESP-IDF v5.5.x，推荐v5.5.4。
- 首次构建需要联网访问Espressif组件服务。
- 仓库已包含`third_party/esp-dl/`、`models/food_yolo.espdl`、NutriCook模型和分区表。
- 不需要从本地其他目录复制ESP-DL或模型文件。

## 获取与构建

```powershell
git clone https://github.com/Lyr1cs13/ESP32-P4_AI_Scale.git
cd ESP32-P4_AI_Scale
idf.py set-target esp32p4
idf.py build
idf.py -p COM7 flash monitor
```

如果`idf.py`无法识别，请打开ESP-IDF PowerShell，或先导出你本机ESP-IDF环境。不同电脑安装路径不同，不要照搬示例绝对路径。

## 默认配置

默认运行模式为完整工程：

```text
CONFIG_AI_SCALE_RUN_MODE_FULL=y
```

默认云端配置：

```text
Wi-Fi SSID: LYY
Wi-Fi password: wccc9556
Record URL: http://106.53.198.194/api/v1/weigh-in/record
User ID: 15
```

最终请求地址由程序自动拼接：

```text
POST http://106.53.198.194/api/v1/weigh-in/record?user_id=15
```

如需修改热点、接口或用户ID：

```powershell
idf.py menuconfig
```

进入：

```text
AI Scale Cloud Upload
```

## 三种运行模式

在`idf.py menuconfig`中进入：

```text
AI Scale Run Mode
  Application run mode
```

| 模式 | 用途 | 跑通标志 |
|---|---|---|
| `Full AI scale` | 完整单板运行 | 放入食材后屏幕刷新食材和重量，选择烹饪方式后显示营养结果，清空秤面后上云 |
| `Perception only` | 只验证摄像头、HX711和YOLO | 串口出现摄像头、HX711和YOLO任务日志，放入食材后输出类别和重量 |
| `Display and nutrition only` | 只验证屏幕和NutriCook | 三页UI可操作，串口输入食材后显示营养结果 |

## 使用流程

1. 空秤启动，等待HX711去皮完成。
2. 逐个放入食材，等待重量稳定和识别结果刷新。
3. 第一页查看食材类别、各自重量和总重量。
4. 第二页选择烹饪方式，包括“不烹饪”。
5. 第三页查看热量、成品重量和主要营养结果。
6. 归零前如果继续增重、减重或重新选择烹饪方式，会覆盖当前餐食记录。
7. 秤面清空到0g后，本次餐食正式结束并进入云上传队列。
8. 下一次增重会创建新的独立餐食记录。

只有已经完成营养推理的餐食，才会在归零时上云。

## 串口模拟输入

`Display and nutrition only`和`Full AI scale`都保留串口输入。摄像头或HX711仍在工作时可能覆盖串口模拟数据，因此纯串口测试推荐使用`Display and nutrition only`。

示例：

```text
beef:180,potato:120
fish:200,ginger:10,garlic:8
tofu:150,cabbage:100
```

模拟秤面归零：

```text
clear
```

或：

```text
0
```

## 云端数据

设备上传JSON字段：

```text
ingredients
raw_weights_g
cooking_method
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
cooking_time_minutes
```

烹饪方式枚举：

```text
raw
boil
braise
deep_fry
pan_fry
roast
steam
stir_fry
```

HTTP 2xx表示上传成功；失败时当前记录保留在队列中，并按配置间隔重试。

## 工程结构

```text
main/                         应用入口与运行模式选择
components/board_display/     MIPI-DSI显示、触摸和板级驱动
components/perception/        摄像头、HX711、YOLO和感知桥接
components/nutrition_ui/      三页UI、餐食状态机和串口输入
components/nutrition_model/   NutriCook推理与二进制树表
components/cloud_upload/      ESP32-C6联网、JSON和HTTP上传队列
components/example_video_common/ 摄像头初始化公共逻辑
third_party/esp-dl/           工程内ESP-DL依赖
models/food_yolo.espdl        食材识别模型
nutricook/                    NutriCook原始模型和训练侧资料
tools/                        模型打包与辅助脚本
docs/                         项目架构图
```

## 性能配置

- ESP32-P4运行在360MHz，32MB PSRAM运行在200MHz。
- Flash使用80MHz QIO。
- YOLO推理任务固定在CPU1，LVGL和称重任务固定在CPU0。
- LVGL状态轮询周期为50ms。
- HX711采用2次平均、20ms轮询和500ms触发冷却，稳定判定保留两次窗口。
- NutriCook使用二进制树表映射，不改变模型参数和数值精度。

## 迁移与排错

- 不要复制其他电脑的`build/`、`sdkconfig`、`managed_components/`目录。
- 换电脑后从源码重新执行`idf.py set-target esp32p4`和`idf.py build`。
- 如果工程移动后出现CMake旧路径错误，删除本工程`build/`后重新构建。
- 如果首次构建无法下载组件，检查是否能访问`components.espressif.com`。
- 如果摄像头初始化失败，检查MIPI-CSI排线方向、传感器型号和SCCB配置。
- 如果触控无响应，确认CST816S初始化日志和触摸I2C地址。
- 如果云上传持续失败，先看HTTP状态码和服务器响应，不要只依据Wi-Fi已连接判断成功。
