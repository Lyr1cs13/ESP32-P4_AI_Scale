# AI-Scale-OH

基于ESP32-P4的端云协同智能营养秤工程。在同一块ESP32-P4上完成摄像头食材识别、HX711称重、触控屏交互、NutriCook营养推理、餐食记录管理，并通过板载ESP32-C6将完整结果上传云端。

## 功能流程

```text
摄像头采集 + YOLO食材识别 + HX711真实称重
        -> 触控屏选择烹饪方式
        -> NutriCook端侧营养模型推理
        -> 屏幕显示热量、成品重量和主要营养结果
        -> 秤面归零后通过ESP32-C6上传完整餐食记录
```

主要功能：

- MIPI-CSI摄像头与ESP-DL YOLO模型识别食材类别。
- HX711获取实际重量，支持增重、减重和清空。
- 480×640触控屏提供三页交互：称重信息、烹饪方式、营养结果。
- NutriCook在端侧输出成品重量、热量及11项完整营养数据。
- 营养模型使用离线二进制树表，避免启动时扫描文本模型。
- 本地维护餐食生命周期并保留最近5次餐食记录。
- ESP32-P4通过板载ESP32-C6和ESP-Hosted连接Wi-Fi。
- 云上传在独立FreeRTOS任务中执行，不阻塞称重、触控或模型推理。
- 支持感知侧、显示营养侧和完整工程三种运行模式。

## 硬件与环境

### 硬件

- 主控：ESP32-P4，32MB Flash，32MB PSRAM。
- 显示：ST7701 MIPI-DSI 480×640触控屏，CST816S触摸控制器。
- 摄像头：当前配置支持SC2336和OV5647自动检测。
- 称重：HX711，默认`DOUT=GPIO22`、`SCK=GPIO23`。
- 联网：板载ESP32-C6，通过SDIO运行ESP-Hosted协处理器固件。

### 软件

- ESP-IDF v5.5.4，最低使用同一v5.5系列。
- Python和工具链由ESP-IDF安装器提供。
- 首次构建需要联网访问Espressif组件服务。

仓库已经包含：

- `third_party/esp-dl/`：ESP-DL源码和ESP32-P4模型加载库。
- `models/food_yolo.espdl`：食材识别模型。
- `components/nutrition_model/`：NutriCook推理与二进制树表。
- `partitions.csv`：32MB Flash分区表。

不需要从其他本地磁盘路径复制ESP-DL或模型文件。

## 获取工程

```powershell
git clone https://github.com/Lyr1cs13/ESP32-P4_AI_Scale.git
cd ESP32-P4_AI_Scale
```

Windows下先打开ESP-IDF 5.5 PowerShell，或者执行本机ESP-IDF的环境脚本。例如：

```powershell
. 'D:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'
```

不同电脑的ESP-IDF安装路径可能不同，不要照搬示例绝对路径。

## 首次配置

```powershell
idf.py set-target esp32p4
idf.py menuconfig
```

### 运行模式

进入：

```text
AI Scale Run Mode
  Application run mode
```

| 模式 | 用途 | 跑通标志 |
|---|---|---|
| `Perception only` | 摄像头、HX711和YOLO独立验证 | 串口出现摄像头、HX711和YOLO任务日志，放入食材后输出类别和重量 |
| `Display and nutrition only` | 触控屏与NutriCook独立验证 | 三页UI可操作，串口输入食材后显示营养结果 |
| `Full AI scale` | 完整单板运行 | 感知结果自动刷新屏幕，选择烹饪方式后计算营养，归零后上云 |

默认配置为`Full AI scale`。

### Wi-Fi与云端

进入：

```text
AI Scale Cloud Upload
  Enable meal record cloud upload
  Wi-Fi SSID
  Wi-Fi password
  Meal record API URL
  Cloud user ID
```

默认接口基础地址：

```text
http://106.53.198.194/api/v1/weigh-in/record
```

程序会自动附加用户参数，默认最终请求为：

```text
POST http://106.53.198.194/api/v1/weigh-in/record?user_id=15
```

仓库不保存个人Wi-Fi名称和密码。每台设备首次使用时必须在`menuconfig`配置网络。修改配置后重新构建烧录。

ESP32-P4本身没有Wi-Fi射频，代码通过`esp_wifi_remote`调用板载ESP32-C6。开发板出厂C6通常已有ESP-Hosted固件；若串口长期无法出现`Transport active`或报告严重RPC版本不兼容，需要为C6烧录与工程匹配的ESP-Hosted从机固件。

## 编译与烧录

```powershell
idf.py build
idf.py -p COM7 flash monitor
```

将`COM7`替换为实际串口。可用以下命令查看Windows串口：

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

`idf.py flash`会自动烧录：

```text
0x2000   bootloader.bin
0x12000  partition-table.bin
0x20000  app.bin
0x620000 models/food_yolo.espdl
```

YOLO模型通过根目录`CMakeLists.txt`中的`esptool_py_flash_to_partition()`写入`yolo_model`分区，无需手工追加烧录命令。

## 使用流程

1. 空秤启动，等待HX711去皮完成。
2. 逐个放入食材，等待重量稳定和识别结果刷新。
3. 第一页查看食材类别、各自重量和总重量。
4. 第二页选择烹饪方式，包括“不烹饪”。
5. 进入第三页，等待NutriCook完成推理并显示结果。
6. 在食材尚未全部取下时，修改重量或烹饪方式会覆盖当前餐食记录。
7. 将秤面清空至0g，本次餐食正式结束并进入云上传队列。
8. 下一次增重会创建新的餐食记录。

只有已经进入结果页并完成营养推理的餐食才能在归零时上传。

## 餐食生命周期

- **开始**：空秤后首次稳定增重。
- **追加**：继续放入食材，将识别结果和新增重量加入当前餐食。
- **减重**：按真实总重修正当前食材快照，不重复运行YOLO。
- **确认**：选择烹饪方式并进入结果页，保存当前营养结果。
- **修改**：归零前重量或烹饪方式变化，会重新推理并覆盖当前记录。
- **结束**：总重归零，关闭当前记录并异步上云。
- **新餐**：归零后再次增重，创建新的独立记录。

## 串口模拟联调

`Display and nutrition only`和`Full AI scale`都保留串口输入。摄像头或HX711仍在工作时可能覆盖串口模拟数据，因此纯串口测试推荐使用`Display and nutrition only`。

输入示例：

```text
beef:180,potato:120
fish:200,ginger:10,garlic:8
tofu:150,cabbage:100
```

规则：

- 食材名称使用模型支持的英文名称。
- 重量单位为g，不写单位。
- 多个食材使用`,`或`;`分隔。
- 名称与重量可用`:`、`=`或空格分隔。
- 单次最多支持4种食材。

选择烹饪方式并进入结果页后，输入以下命令模拟秤面归零：

```text
clear
```

也可以输入：

```text
0
```

## 云端数据

设备上传JSON包含：

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

端侧发送的烹饪方式枚举：

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

云端数据库约束必须允许以上枚举。若返回`weigh_records_cooking_method_check`，表示后端尚未允许对应值，并非端侧循环或网络失败。

上传成功日志：

```text
cloud_upload: HTTP status=200 response=...
cloud_upload: MEAL_UPLOAD_SUCCEEDED
```

上传失败时，当前记录保留在队列中并按配置间隔重试。HTTP 2xx后才处理下一条记录。

## 工程结构

```text
main/
  main.c                         应用入口与三种运行模式

components/board_display/       MIPI-DSI显示、触摸和板级驱动
components/perception/          摄像头、HX711、YOLO和感知桥接
components/nutrition_ui/        三页UI、餐食状态机和串口输入
components/nutrition_model/     NutriCook推理与二进制树表
components/cloud_upload/        ESP32-C6联网、JSON和HTTP上传队列
components/example_video_common/摄像头初始化公共逻辑

third_party/esp-dl/             工程内ESP-DL依赖
models/food_yolo.espdl          食材识别模型
nutricook/                      原始模型和训练侧资料
tools/                          模型打包与辅助脚本
docs/                           可编辑项目架构图
```

## 性能配置

- ESP32-P4运行于360MHz，外部32MB PSRAM运行于200MHz，Flash使用80MHz QIO。
- YOLO推理任务固定在CPU1，LVGL与称重任务固定在CPU0，减少推理期间的界面卡顿。
- LVGL使用DMA双缓冲，界面状态轮询周期为50ms。
- HX711采用2次平均、20ms轮询和500ms触发冷却；稳定判定仍保留两次窗口，兼顾响应速度与抗抖动。
- 营养模型继续使用二进制树表映射，不改变模型参数和数值精度。

## 模块协作

- `weight_sensor`任务读取HX711并判断稳定重量变化。
- 增重事件触发摄像头采集并向`food_yolo`任务提交推理请求。
- `perception_bridge`通过回调把食材类别和重量传递给UI。
- `nutrition_ui`管理触控交互、餐食状态和NutriCook异步推理。
- `cloud_upload`通过独立队列接收已结束餐食，联网发送并处理重试。
- 共享状态通过Mutex保护，任务间通过Queue、Task Notify、Semaphore和回调传递事件。

## 架构图

- [四层物联网架构详图](docs/膳衡智枢_四层物联网架构图.drawio)
- [四层物联网架构精简图](docs/膳衡智枢_四层物联网架构图_精简版.drawio)

以上文件可直接导入[app.diagrams.net](https://app.diagrams.net)继续编辑。

## 迁移与排错

- 不复制其他电脑的`build/`、`sdkconfig`和`managed_components/`目录。
- 换电脑后从源码重新执行`idf.py set-target esp32p4`和`idf.py build`。
- 如果工程移动后出现CMake旧路径错误，删除本工程`build/`后重新构建。
- 如果`idf.py`无法识别，说明当前终端尚未导出ESP-IDF环境。
- 如果首次构建无法下载组件，检查是否能访问`components.espressif.com`。
- 如果摄像头初始化失败，检查MIPI-CSI排线方向、传感器型号和SCCB引脚。
- 如果触控无响应，确认CST816S初始化日志和触摸I2C地址。
- 如果云上传持续失败，先查看HTTP状态码和服务器响应，不要仅依据Wi-Fi已连接判断成功。
