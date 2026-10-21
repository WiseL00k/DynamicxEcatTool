# 构建安装包与 Windows CI

当前版本由 `VERSION` 维护：`0.4.0`。本目录是受 Git 管理的打包入口，旧构建目录中的 `windows-release` 保留不变。Windows 脚本集中在 `windows/`，后续 Ubuntu 24 的 AppImage/deb 可使用同一个版本文件，另建 Linux 入口。

## 自动构建

向 `master` 创建、重新打开或更新 PR，合并后推送 `master`，以及手动运行 **Windows CI** 都会触发 **Windows Build and Package**。

流程：拉取源码和锁定的 SOEM 子模块 → MSVC x64 → Qt 6.8.3 → 构建 yaml-cpp 0.9.0 → Release 编译 → CTest 和安装状态机测试 → Qt 部署 → Inno Setup 6.7.1 → 上传安装包。

使用标准 `windows-2022` runner。Actions 固定 SHA，yaml-cpp 固定提交，Qt 和 yaml-cpp 使用缓存；不需要个人访问令牌或仓库 Secret。`dependencies.json` 记录下载版本和哈希。云端 MSVC/Windows SDK 的补丁版本随 runner 更新，构建清单记录实际版本。

在 PR 的 Checks 中打开对应运行，或进入仓库 **Actions → Windows CI → 对应运行 → Artifacts**，下载 `DynamicxEcatTool-v0.4.0-windows-amd64-<提交>-<运行编号>-<尝试次数>`。解压后包含：

- `DynamicxEcatTool-v0.4.0-amd64.exe`：安装器；内部程序仍叫 `DynamicxEcatTool.exe`。
- 同名 `.exe.sha256`：SHA256 校验文件。
- `build-info.json`：源码提交（含 PR head 与测试合并提交）、工具版本、输入依赖和部署文件哈希。

附件保留 14 天；未签名，不自动发布 GitHub Release。失败日志单独作为 diagnostics 附件保存。任何构建、测试、打包或上传失败均使检查失败。

## 安装与联网要求

安装包包含 Qt/QML、SOEM/yaml-cpp 动态库、默认 ESI、示例 YAML 和 Microsoft VC++ x64 运行库。**不包含 Npcap 安装器或其 DLL**。

安装程序先安装 VC++ 运行库，再检查 Npcap 服务登记、WinPcapCompatible 配置和原生 64 位系统目录中两个 Npcap DLL 的版本身份：

- 已有兼容 Npcap：不访问网络、不重装或降级 Npcap，可离线安装本软件。
- 没有兼容 Npcap：提前提示首次安装需要联网，从官方 HTTPS 地址下载锁定的 Npcap 1.87，校验 SHA256 后自动打开交互安装程序，要求 WinPcap API-compatible Mode。
- 无法下载：显示联网提示，保留在安装准备阶段，可重试或取消。
- 文件校验不通过：禁止执行并显示独立的校验错误。
- 用户取消、安装失败或安装后检查仍不通过：不继续复制应用文件，不误报成功。
- 需要重启：显示重启提示，不主动重启电脑。

“自动打开”不等同于静默安装。Npcap 的许可与选项仍由用户在官方安装界面确认。CI 只下载并提取用户态 DLL 到测试目录，不安装捕获驱动，也不把它们上传到发布附件。

参考：[Npcap 安装及退出码](https://npcap.com/guide/npcap-users-guide.html)、[Npcap 检测](https://npcap.com/guide/npcap-devguide.html)、[Inno 下载页面](https://jrsoftware.org/ishelp/topic_isxfunc_createdownloadpage.htm)。

## 本地复现

先按仓库现有 `scripts/build-and-run.ps1` 完成 Release 构建，或在 MSVC x64 开发者终端配置 CMake。Qt、yaml-cpp、源码必须来自同一构建配置。

```powershell
# 例如：已有本地工具环境的独立构建（原有脚本可传工具路径）
./scripts/build-and-run.ps1 -BuildDirectory build/ci-local-windows -RunTests -BuildOnly

# 不安装驱动的打包脚本与状态机测试
./packaging/windows/tests/verify.ps1 `
  -IsccPath 'D:/Program Files/Inno Setup 6/ISCC.exe' `
  -WorkDirectory build/packaging-tests

# 指定实际工具和官方 VC++ x64 安装器；目标目录必须尚不存在
./packaging/windows/package.ps1 `
  -BuildDirectory build/ci-local-windows `
  -OutputDirectory build/package-local `
  -IsccPath 'D:/Program Files/Inno Setup 6/ISCC.exe' `
  -VcRedistPath 'C:/path/to/vc_redist.x64.exe'
```

输出位于指定目录的 `publish/`；其余是本次暂存文件和日志。加 `-CheckOnly` 只验证输入，不创建输出；也可用 `-Version 0.4.1` 临时验证版本。常规发布修改 `VERSION`，不手动更改多个文件。

打包依据 `local-runtime-Release.json` 定位输入，核对所属源码目录、PE 架构和复制哈希。Qt 部署扫描 `qml/`，不扫描历史构建目录。DXC 的 x64 `dxcompiler.dll`/`dxil.dll` 配对从 Windows SDK 补齐；VC++ 安装器需微软有效签名、x64、版本至少 14.40。输入缺失时明确失败，不清空或覆盖旧输出。

## 更新依赖与验收

修改 Npcap 版本时，同时更新 `windows/dependencies.json` 中的版本、官方 URL 和 SHA256；重新验证联网、离线、取消、损坏下载及兼容模式。不得只改 URL 而关闭校验。Inno Setup 和 yaml-cpp 同样通过该文件维护。

自动测试覆盖已有 CTest 与共享的真实 Inno 安装状态机（使用隔离 IO 模拟下载/进程/系统状态），不会在开发机安装驱动。下载函数及哈希校验另行测试。**不等于已验证真实驱动安装和升级**。

正式发布前在独立 Windows x64 测试机验证：首次联网安装、首次离线提示及重试、已有 Npcap 离线安装、取消安装、重启要求、0.3.1 升级、卸载、无 Qt 环境启动、图形显示、网卡枚举和真实 EtherCAT 通信。

## master 合并保护

首次成功运行后，管理员将 **Windows Build and Package** 设为 `master` 必需检查：要求 PR、要求与目标分支保持最新、禁止管理员绕过，禁止强推和删除。无需设置人工批准人数；PR 留给维护者审核且不自动合并。

分支保护是 GitHub 远程设置，并非 YAML 自动配置。检查名必须与实际运行结果一致；以后增加 Linux CI 时再决定是否将其加入必需检查。
