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

## 目录结构

- `tutorial/`：**软件使用简单教程**
- `Backend/`：后端业务逻辑与配置解析
- `Frontend/`：前端数据适配层
- `SOEM_interface/`：对 SOEM 的封装接口
- `sample_config/`：示例 YAML 配置文件
- `*.qml`：界面页面与组件、

## 依赖环境

- CMake >= 3.16
- C++17 编译器
- Qt6（至少包含 `Core`、`Quick`、`QuickControls2`、`Concurrent`）
- `yaml-cpp`

> 注意： 仓库包含 `SOEM` 子模块，首次拉取后需要初始化子模块。

## 构建步骤

```bash
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j
```

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

首次使用建议：

1. 在测试/调试/参数界面先刷新并选择正确网卡；
2. 在调试界面，需先加载 `sample_config/` 下对应 YAML；
3. 确认连接状态后再执行测试流程、EEPROM 烧录或参数下发，并观察日志与状态信息。
