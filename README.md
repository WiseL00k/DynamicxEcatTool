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

- CMake >= 3.16
- C++17 编译器
- Qt6（至少包含 `Core`、`Quick`、`QuickControls2`）
- `yaml-cpp`

> 注意： 仓库包含 `SOEM` 子模块，首次拉取后需要初始化子模块。

## 构建步骤

```bash
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j
```

构建时，后端模块会先编译为 `dynamicx_backend` 静态库，再与 QML 应用和 `SOEM_interface` 链接生成 `DynamicxEcatTool`。

## 运行

Windows 本地开发可使用 PowerShell 入口，自动构建、部署依赖并启动本次构建的程序：

```powershell
.\scripts\build-and-run.ps1 -RunTests
```

默认使用 Qt 6.8.3 MSVC x64、Visual Studio 2022 和本机 vcpkg 的 yaml-cpp；安装位置不同时可通过脚本参数 `QtRoot`、`VsEnvironment`、`YamlPackageDirectory` 和 `Ninja` 指定。构建目录固定在本仓库的 `build/codex-dc-release`，也可以用 `BuildDirectory` 覆盖。`-BuildOnly` 只构建，`-NoLaunch` 构建部署后不启动。

脚本使用同一 Qt 安装中的 `windeployqt` 部署 QML/平台插件与编译器运行库，并校验 `soem_interface.dll`、`yaml-cpp.dll` 和 Npcap 依赖。启动后验证进程 EXE 与自有 DLL 的实际路径，在构建目录生成 `runtime-verification.json`。不要通过旧安装目录或仓库根目录中的历史 EXE 验证新功能。

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

无硬件自动测试可运行：

```powershell
.\scripts\build-and-run.ps1 -RunTests -BuildOnly
```

Linux 可使用 `cmake -S . -B build -DBUILD_TESTING=ON`、`cmake --build build` 和 `ctest --test-dir build --output-on-failure`。QML 测试使用 offscreen 平台；自动测试不会连接真实 EtherCAT 从站。

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
