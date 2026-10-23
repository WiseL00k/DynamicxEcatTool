# DynamicxEcatTool

DynamicxEcatTool 是一个基于 **Qt6 + QML + C++** 的 EtherCAT 调试与测试工具，用于在上位机侧完成网卡扫描、从站连接、通信状态查看与参数配置等操作。项目使用 [SOEM](https://github.com/OpenEtherCATsociety/SOEM) 进行 EtherCAT 主站通信，并通过 `yaml-cpp` 解析设备与从站配置文件。

## 测试平台

- Windows 11
- Ubuntu 20.04

Ubuntu 安装包按 20.04 LTS、24.04 LTS 分别构建，提供 AppImage 和 deb。请下载与系统版本对应的包；24.04 包不声明兼容 20.04 / 22.04。下载入口与安装说明见 [打包说明](packaging/README.md)。

## 主要功能

- 网卡扫描与选择
- EtherCAT 从站连接/断开与状态显示
- 测试界面、调试界面、参数配置界面（QML）
- 基于 YAML 的设备/从站配置加载
- 电机在线状态与日志输出
- DC SYNC0 同步测试、从站配置回读及主机周期诊断
- ECAT2CANFD 多从站配置、CAN 单次/周期提交、接收记录与双向 PDO 监视

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

主要功能页面如下：

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

### ECAT2CANFD

入口位于“DC 测试”下方，共用顶部网卡选择。先在“配置”中设置预期从站数量（默认 1），连接后保持 PRE-OP，并核对在线身份和完整 PDO 映射。支持的固件身份为 Vendor `0`、Product `0xAB125`、Revision `3`，每个从站双向 PDO 均为 185 字节。

配置页分别编辑每个从站的 CAN0/CAN1：启用、Classic / FD / FD+BRS、仲裁速率、数据速率及三项 options。通过独立串行工作线程逐项写入 `0x2000` / `0x2001` 的子索引 1–5，再写 Apply（6），等待状态（7）并读取错误（8）和实际配置。PRE-OP 和 OP 均可配置；OP 应用前停止目标通道提交，确认空输出周期后写 SDO，完成后发送保持停止。PDO 线程不会等待整个 SDO 事务。草稿只保留在本次界面会话中，回读不会覆盖未应用的编辑。

默认关闭 DC，总线周期为 2000 μs、偏移为 0。可选周期 250–100000 μs，偏移绝对值小于周期；100 ms 上限为该固件独立 200 ms 输出看门狗留出余量。连接、应用配置后“启动运行”进入 OP，“停止运行”返回 PRE-OP 并保留 SDO 连接，“断开”释放网卡。运行期间锁定总线周期/DC 参数。

发送区指定从站、CAN 通道、11 位标准 ID、DLC（0–8）和对应数量的十六进制字节。该固件即使使用 FD 也只支持每槽 8 字节。“发送一次”表示一次有效 PDO 提交；周期发送间隔向上取整为总线周期的整数倍。首次 WKC 异常即停止用户提交，不自动重试。固件没有帧序号或 CAN 发送确认，DC 下同一有效 PDO 可能在多个 SYNC0 时刻被处理，因此界面不能保证 CAN 线上恰好发送一次。

默认显示每个完整 WKC 采样中 VALID=1 的接收槽，带主机采样相对时间、从站和通道，不按 ID/数据内容去重。列表最多保留 10000 条，支持清空及自动跟随；累计/丢弃计数反映主机采样记录，不能代表 CAN 线上无损抓包。固件在一次输入更新内会合并同 ID，只保留每通道最多 9 个 ID。PDO 监视可选择从站，显示 RxPDO（主站→从站）、TxPDO（从站→主站）的全部槽、控制/状态字、GPIO 及完整字节；停止后的数据标记为历史快照。

### 自动测试与实机探针

CMake 配置时启用 `-DBUILD_TESTING=ON`，构建后运行 `ctest --test-dir <build> --output-on-failure`。测试包含原 DC 回归、CAN 协议/配置/发送状态机、PDO/SDO 共享端口同步，以及四档 DPI 下的 QML 布局。Windows 运行应使用本次构建的 `soem_interface.dll`、匹配的 `yaml-cpp.dll`、Qt DLL/QML 插件及系统 Npcap，部署方法见 [打包说明](packaging/README.md)。

`DynamicxCanfdHardwareProbe` 不加入 CTest；无参数仅列出网卡。显式传入 `--adapter <Npcap名称> --exercise-config --output <结果.json>` 才连接一个从站，回读并重应用原 CAN 配置，检查普通/DC OP 中的并行 SDO 和停止后回读。探针仅提交全零应用 PDO，不生成用户 CAN 帧；固件状态切换自身的安全输出行为仍由固件决定。CAN 线侧收发和多从站硬件行为需要相应外设实测。
  
## AppImage 无法挂载时

系统无法使用 FUSE 时，可以解压 AppImage 后运行：

```bash
./文件名.AppImage --appimage-extract
./squashfs-root/AppRun
```
