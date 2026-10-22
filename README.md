# DynamicxEcatTool

DynamicxEcatTool 是一个基于 **Qt6 + QML + C++** 的 EtherCAT 调试与测试工具，用于在上位机侧完成网卡扫描、从站连接、通信状态查看与参数配置等操作。项目使用 [SOEM](https://github.com/OpenEtherCATsociety/SOEM) 进行 EtherCAT 主站通信，并通过 `yaml-cpp` 解析设备与从站配置文件。

## 测试平台

- Windows 11
- Ubuntu 20.04

## 主要功能

- 网卡扫描与选择
- EtherCAT 从站连接/断开与状态显示
- 测试界面、调试界面、参数配置界面（QML）
- 基于 YAML 的设备/从站配置加载
- 电机在线状态与日志输出
- DC SYNC0 同步测试、从站配置回读及主机周期诊断

## 目录结构

- `tutorial/`：**软件使用简单教程  <==**
- `App/`：应用启动逻辑，包括字体选择与 QML 上下文注册
- `Backend/`：后端业务逻辑；`Config` 负责配置解析，`Ethercat` 负责主站、从站与 SDO 控制，`Monitor` 负责在线状态监控，`Flash` 负责 EEPROM/固件烧录，`Models`、`Network`、`Commands` 分别提供数据模型、网卡服务与电机命令封装
- `SOEM_interface/`：对 SOEM 的主站总线、从站基类、错误处理与工具接口进行封装
- `sample_config/`：示例 YAML 配置文件
- `qml/Main.qml`：主窗口与页面入口
- `qml/pages/`：测试与烧录、设备调试、参数配置、总线配置页面
- `qml/components/`：通用控件、导航、网卡选择、日志与烧录进度组件
- `qml/dialogs/`：确认与错误对话框
- `qml/theme/`：设计样式参数与主题切换控件
- `qml/adapters/`：会话状态与日志的界面适配器
- `qml/panels/`：MIT 电机参数面板

## 依赖环境

### Windows 11（x64）

- Git 及仓库锁定的 `SOEM` 子模块
- CMake >= 3.28（`SOEM` 子模块要求）
- Visual Studio 2022 的 MSVC C/C++ 编译工具和 Windows SDK，支持 C++17
- Ninja 或 MSBuild
- Qt6 MSVC x64 开发库，包含 `Core`、`Quick`、`QuickControls2`；当前 Windows CI 使用 Qt 6.8.3
- `yaml-cpp` 开发库，编译器和架构需与 Qt、应用一致
- 运行依赖：Microsoft Visual C++ x64 运行库、启用 WinPcap API-compatible Mode 的 Npcap

### Ubuntu 20.04 及以上

- Git 及仓库锁定的 `SOEM` 子模块
- CMake >= 3.28（`SOEM` 子模块要求）
- 支持 C++17 的 GCC/G++ 编译工具
- Ninja 或 Make
- Qt6 开发库，包含 `Core`、`Quick`、`QuickControls2`
- `yaml-cpp` 开发库
- 系统提供的 POSIX 线程库与实时库（`pthread`、`rt`）

自动测试额外需要 Qt6 的 `Test` 和 `QuickTest` 模块。

## 运行

***运行软件前，***

***给软件提供管理员权限（windows平台）***

***需要Sudo运行（Ubuntu 平台）***

编译成功后运行生成的可执行文件（名称为 `DynamicxEcatTool`）。

界面由三个功能页签组成：

### 1. 测试界面

- **网卡选择与连接控制**：支持刷新网卡列表、选择目标网卡、连接/停止 EtherCAT 测试流程，并通过状态灯显示当前连接状态。
- **运行日志查看**：实时显示后端输出日志，便于观察通信过程和错误信息。
- **EEPROM 烧录**：可选择从站地址与 `.hex` 文件，执行 EEPROM 烧录并查看进度与结果。

### 2. 调试界面

- **主站连接与配置文件加载**：支持刷新网卡、选择网卡、连接/断开主站，并可加载 YAML 配置文件。
- **在线状态监控**：展示电机在线状态列表（含从站分组、CAN Bus、CAN ID、在线/离线状态）。
- **调试日志输出**：集中展示通信与状态变化日志，便于联调定位问题。

### 3. 参数界面

- **从站与功能选择**：支持选择从站类型（当前为 MIT），并加载对应参数面板。
- **MIT电机控制**：测试MIT电机控制，支持原生数据帧发送，参数配置发送。
- **连接状态联动**：根据 EtherCAT 连接状态动态启用/禁用参数下发操作，避免离线误操作。

### DC 测试

在顶部选择测试网卡，打开“DC 测试”，设置周期和偏移后启动。默认周期为 2000 μs、偏移为 0 μs；支持周期 250～1,000,000 μs，偏移绝对值必须小于周期。测试占用独立总线会话，运行时参数锁定，可以停止或取消启动。

测试根据在线 PDO 映射运行，不加载电机配置、不下发电机使能或厂商专用 SDO。它在 PRE-OP 中配置 SYNC0 并回读激活位和周期，再尝试进入 OP。需要额外厂商参数才能进入 DC 应用模式的设备，应先根据设备文档完成对应配置；失败时页面保留从站 AL 状态与原因。

页面显示配置回读、参考时钟、DC 时间、WKC、周期与超期统计，以及各从站的 DC/SYNC0 状态。主机相位误差、周期偏差和遗漏周期反映普通操作系统的调度质量，不代表从站间同步精度或实际 SYNC0 引脚测量结果。

停止后保留历史结果；“重置统计”只清除统计值。停止流程会尝试关闭 SYNC0，无法回读确认时明确显示未确认。实际同步信号可结合设备诊断或示波器验证。

首次使用建议：

- 在测试/调试/参数界面先刷新并选择正确网卡；
- 在调试界面，需先加载 `sample_config/` 下对应 YAML；
- 确认连接状态后再执行测试流程、EEPROM 烧录或参数下发，并观察日志与状态信息。
  
## 一些问题
在Ubuntu24下运行AppImage时，可能会遇到缺少FUSE库报错。此时运行以下命令可安装缺少的库
```bash
sudo apt update
sudo apt install fuse
```
重新运行AppImage,即可
