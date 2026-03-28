# DynamicxEcatTool

DynamicxEcatTool 是一个基于 **Qt6 + QML + C++** 的 EtherCAT 调试与测试工具，用于在上位机侧完成网卡扫描、从站连接、通信状态查看与参数配置等操作。项目使用 [SOEM](https://github.com/OpenEtherCATsociety/SOEM) 进行 EtherCAT 主站通信，并通过 `yaml-cpp` 解析设备与从站配置文件。

## 主要功能

- 网卡扫描与选择
- EtherCAT 从站连接/断开与状态显示
- 测试界面、调试界面、参数配置界面（QML）
- 基于 YAML 的设备/从站配置加载
- 电机在线状态与日志输出

## 目录结构

- `Backend/`：后端业务逻辑与配置解析
- `Frontend/`：前端数据适配层
- `SOEM_interface/`：对 SOEM 的封装接口
- `sample_config/`：示例 YAML 配置文件
- `*.qml`：界面页面与组件

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

编译成功后运行生成的可执行文件（名称为 `DynamicxEcatTool`）。

首次使用建议：

1. 在界面中刷新并选择正确网卡；
2. 载入 `sample_config/` 下对应设备配置；
3. 启动通信或测试流程，观察日志与状态信息。
