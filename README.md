# Win顺 多网卡出口切换插件（`libo0118.netrouteswitcher`）

基于 **C++20 / Qt 6.12 QML** 构建的 Win顺 原生多网卡出口路由切换与跃点数管理插件。无需手动打开繁琐的 Windows 网络适配器属性页，即可在搜索栏、系统托盘或独立管理面板中查看当前 IPv4/IPv6 默认出口、调整首选/备选网卡优先级、自定义接口跃点数并探测出口连通性。

## 核心功能

- **实时识别默认出口**：读取 Windows IP Helper 路由与适配器状态，展示当前首选出口网卡、网关地址、IPv4/IPv6 地址、接口索引（IfIndex）与当前跃点数（Metric）。
- **一键切换主/备网卡**：快速将任意有线、无线或虚拟网卡设为首选出口（低跃点数）或备选出口，并支持按设置决定是否同步修改 IPv6 接口跃点数。
- **自定义跃点数与恢复自动**：支持为单个适配器设置精确跃点数，或一键将所有受管理网卡恢复为 Windows 自动跃点数（Automatic Metric）。
- **出口连通性与测速**：支持快速探测当前出口网络延迟与连通性，排查双网共存（如内网有线 + 外网 Wi-Fi/热点）时的实际出口归属。
- **托盘与设置面板联动**：提供常驻系统托盘快捷菜单与高 DPI 自适应明暗主题 QML 管理界面，并可通过 Win顺 快捷打开系统网络连接面板（`ncpa.cpl`）。

## 关键字与快捷操作

在 Win顺 搜索框中输入 `route`、`net`、`网卡`、`路由` 或 `出口` 加空格：

- **留空**：展示当前默认出口网卡、可用适配器列表以及常用操作（打开管理界面、连通性测速、恢复自动跃点数、打开网络连接 `ncpa.cpl`）。
- **输入筛选词**：如 `route 测速`、`route Wi-Fi`、`route 192.168`，即时过滤网卡或操作项。
- **快捷键**：
  - `Enter`：执行当前选中条目（切换首选出口、运行测速等）
  - `Shift+Enter`：打开独立 QML 多网卡管理界面

## 权限与安全说明

本插件仅申请完成多网卡管理所需的最小权限集，且运行在 `"sandbox": "normal"` 模式下（不启用与 `"elevated"` 冲突的 `"strict"` 沙箱）：

1. **`ui`**：呈现独立 QML 管理面板与跃点数编辑弹窗，响应宿主 `appearance` 明暗圆角主题同步。
2. **`elevated`**：调用 Windows `SetIpInterfaceEntry` / 管理员提权通道修改网络接口跃点数（只读查询与测速无需提权）。
3. **`processes`**：按用户操作拉起 `ncpa.cpl` 或提权辅助操作。
4. **`network`**：读取本机网络接口/默认路由表与执行出口连通性探测。

## 插件设置

可在 Win顺 插件设置中配置以下选项：

- `syncIpv6`（默认 `true`）：同步调整 IPv6 接口跃点数。
- `showDisconnected`（默认 `false`）：显示未连接或已禁用的网络适配器。

## 运行依赖与共享 Qt 6.12 契约

- 本插件声明 `"qt": "host"` 与 `"qtVersion": "6.12.0"`，直接复用 Win顺 宿主自带的 Qt 6.12 运行时（`Qt6Core`、`Qt6Gui`、`Qt6Qml`、`Qt6Quick`）。
- **`.wsplugin` 安装包内严格不包含任何 `Qt6*.dll`、`qt.conf`、`platforms/` 或外部 `qml/` 文件**，所有 QML 与图标均通过 Qt 资源系统内嵌于 `NetRouteSwitcher.exe`。
- 最低兼容 Win顺版本：`0.25.0`。

## GitHub Actions CI 构建与安装

推送到 GitHub 后，`.github/workflows/build.yml` 会在 `windows-2022` 上自动完成：

1. 使用 `ci/validate_manifest.py` 校验 `plugin.json` 与 `icon.png`；
2. 下载并校验固定 SHA-256 的 Qt `6.12.0` MSVC2022 64-bit SDK；
3. 执行 `ci/build.ps1` 编译 `NetRouteSwitcher.exe` 与 `netrouteswitcher_tests.exe`，并运行 C++ 单元测试；
4. 执行 `ci/test_e2e.py` 在 `QT_QPA_PLATFORM=offscreen` 下通过真实 stdin/stdout NDJSON 校验 `initialize`、`query`、`trayMenu`、`showSettings`（含 QML 加载检查）、`appearance` 与 `shutdown`；
5. 执行 `ci/package.py` 生成纯净的 `libo0118.netrouteswitcher-<version>.wsplugin` 与 `.sha256` 校验文件。

本地安装已构建的插件包：

```powershell
ws plugin install .\dist\libo0118.netrouteswitcher-0.1.0.wsplugin
ws plugin enable libo0118.netrouteswitcher
```

## 开源协议

基于 **MIT License** 开源，详见 [LICENSE](LICENSE)。
