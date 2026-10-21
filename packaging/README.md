# 构建安装包与 Windows CI

当前版本由 `VERSION` 维护：`0.4.0`。本目录是受 Git 管理的打包入口，旧构建目录中的 `windows-release` 保留不变。Windows 脚本集中在 `windows/`，后续 Ubuntu 24 的 AppImage/deb 可使用同一个版本文件，另建 Linux 入口。

## 自动构建

向 `master` 创建、重新打开或更新 PR，推送 `master`，推送任意名称的 tag，以及手动运行 **Windows CI** 都会触发 **Windows Build and Package**。

tag 必须推送到 GitHub 才会触发构建，例如 `git tag v0.4.0` 后执行 `git push origin v0.4.0`；tag 指向的提交需包含本工作流。tag 构建同样执行测试、打包并上传附件，安装包版本仍以 `packaging/VERSION` 为准，发布前应使其与 tag 版本一致。

流程：拉取源码和锁定的 SOEM 子模块 → MSVC x64 → Qt 6.8.3 → 构建 yaml-cpp 0.9.0 → Release 编译 → CTest 和安装状态机测试 → Qt 部署 → Inno Setup 6.7.1 → 上传安装包。

使用标准 `windows-2022` runner。Actions 固定 SHA，yaml-cpp 固定提交，Qt 和 yaml-cpp 使用缓存；不需要个人访问令牌或仓库 Secret。`dependencies.json` 记录下载版本和哈希。云端 MSVC/Windows SDK 的补丁版本随 runner 更新，构建清单记录实际版本。

主仓库锁定的 SOEM 提交 `d2b098f` 已发布到 `WiseL00k/SOEM` 的 `codex/windows-ci-dependency` 分支，供全新 checkout 获取；没有改动子模块版本或 SOEM 的 master。该提交在其他远程分支保留前，不应删除这个依赖分支。

在 PR 的 Checks 中打开对应运行，或进入仓库 **Actions → Windows CI → 对应运行 → Artifacts**，下载 `DynamicxEcatTool-v0.4.0-windows-amd64-<提交>-<运行编号>-<尝试次数>`。解压后包含：

- `DynamicxEcatTool-v0.4.0-amd64.exe`：安装器；内部程序仍叫 `DynamicxEcatTool.exe`。
- 同名 `.exe.sha256`：SHA256 校验文件。
- `build-info.json`：源码提交（含 PR head 与测试合并提交）、工具版本、输入依赖和部署文件哈希。

附件保留 14 天；未签名，不自动发布 GitHub Release。失败日志单独作为 diagnostics 附件保存。任何构建、测试、打包或上传失败均使检查失败。

## 安装流程

安装包包含 Qt/QML、SOEM/yaml-cpp 动态库、默认 ESI、示例 YAML、Microsoft VC++ x64 运行库和 **Npcap 1.87 官方安装器**。用户安装时无需联网下载依赖。

安装向导提供“安装 Npcap（首次安装请勾选）”复选框，并提示“若本机未安装 Npcap，可能会导致程序无法正常运行。”首次安装默认勾选，检测到已安装本软件时默认不勾选，用户可自行更改选择；不自动探测本机是否已有 Npcap。

安装程序先安装 VC++ 运行库。勾选“安装 Npcap”时，从临时目录启动内嵌的 Npcap 官方安装程序并等待用户完成交互安装，随后复制应用文件；未勾选时直接安装应用。Npcap 兼容模式使用 `/winpcap_mode=enforced`。

- Npcap 的许可与安装界面由用户确认，不使用静默安装参数。
- 用户取消、启动失败或安装返回失败时，暂停应用安装，允许重试或取消。
- 需要重启时提示用户，不自动重启电脑。
- 没有网络状态检测、下载进度页或首次安装需联网的提示。

CI 在构建阶段从官方 HTTPS 地址下载固定版本并校验 SHA256，打包前再次校验。本地打包通过 `-NpcapInstallerPath` 提供同版本安装器。Npcap 二进制不提交到 Git 仓库；最终 EXE 内嵌安装器，构建清单的 `embeddedPrerequisites` 记录其哈希。测试提取的用户态 DLL 仅放在临时测试目录，不复制到应用目录；CI 不安装捕获驱动。

参考：[Npcap 安装选项及退出码](https://npcap.com/guide/npcap-users-guide.html)。

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
  -VcRedistPath 'C:/path/to/vc_redist.x64.exe' `
  -NpcapInstallerPath 'C:/path/to/npcap-1.87.exe'
```

输出位于指定目录的 `publish/`；其余是本次暂存文件和日志。加 `-CheckOnly` 只验证输入，不创建输出；也可用 `-Version 0.4.1` 临时验证版本。常规发布修改 `VERSION`，不手动更改多个文件。

打包依据 `local-runtime-Release.json` 定位输入，核对所属源码目录、PE 架构和复制哈希。Qt 部署扫描 `qml/`，不扫描历史构建目录。DXC 的 x64 `dxcompiler.dll`/`dxil.dll` 配对从 Windows SDK 补齐；VC++ 安装器需微软有效签名、x64、版本至少 14.40。输入缺失时明确失败，不清空或覆盖旧输出。

## 更新依赖与验收

修改 Npcap 版本时，同时更新 `windows/dependencies.json` 中的版本、官方 URL 和 SHA256；重新验证内嵌安装、取消、失败、重启及兼容模式。不得只改 URL 而关闭校验。Inno Setup 和 yaml-cpp 同样通过该文件维护。

自动测试覆盖已有 CTest 与共享的真实 Inno 安装状态机（使用隔离 IO 模拟内嵌安装器的进程与退出码），不会在开发机安装驱动。构建阶段的下载函数及哈希校验另行测试。**不等于已验证真实驱动安装和升级**。

正式发布前在独立 Windows x64 测试机验证：首次离线安装、已有 Npcap 时的交互安装、取消安装、重启要求、0.3.1 升级、卸载、无 Qt 环境启动、图形显示、网卡枚举和真实 EtherCAT 通信。

## master 合并保护

首次成功运行后，管理员将 **Windows Build and Package** 设为 `master` 必需检查：要求 PR、要求与目标分支保持最新、禁止管理员绕过，禁止强推和删除。无需设置人工批准人数；PR 留给维护者审核且不自动合并。

分支保护是 GitHub 远程设置，并非 YAML 自动配置。检查名必须与实际运行结果一致；以后增加 Linux CI 时再决定是否将其加入必需检查。
