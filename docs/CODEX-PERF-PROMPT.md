# 给 Codex 的交接说明：寒淋输入法 1.0.0 之后的改动与性能优化任务

> 生成时间：2026-11-21
> 仓库：`D:\Rime`（GitHub `TangSong777/HanLin-IME`）
> 当前 HEAD：`13ac5a5`（snapshot 与 master 都在这里）

## 一、项目当前状态

- 当前版本：**1.0.0**（2026-11-20 发布，tag `1.0.0` → `222611d`）
- 产品定位：**独立输入法软件**，1.0.0 起不再做小狼毫兼容
- 安装包：`HanLin-Setup-1.0.0.exe` 42,279,783 字节，SHA256 `833FBEA8AA703A0EFE9DDEA350F626FED6EC3F960738BDA8A184934BE87BE7FF`

## 二、你上次离开后，我做的改动

### 1. 修复 WordManager 的 vbs 启动器（`src/tools/WordManager.vbs`）

**问题**：`shell.Run(..., 0, False)` 的 `0` 隐藏整个进程窗口 → PowerShell 控制台和 WinForms 窗口一起被隐藏，双击「没反应」+ 任务栏残留黑窗。

**修复**：启动参数加 `-WindowStyle Hidden`：

```vbs
shell.Run Chr(34) & powershell & Chr(34) & " -NoProfile -STA -WindowStyle Hidden -ExecutionPolicy Bypass -File " & Chr(34) & script & Chr(34), 0, False
```

`-WindowStyle Hidden` 只隐藏控制台，WinForms 窗口正常显示；`Run` 的 `0` 保证不闪黑窗。

### 2. 发布 1.0.0

- README 重构（678 → 730 行）：新增 `### HanLin 前端`、`### 图标系统`、`### 词条工具` 三节
- CHANGELOG 插入 `## 1.0.0 — 2026-11-20`
- 版本号：`src/installer/HanLin.iss:2` `MyAppVersion "1.0.0"`
- 提交 `222611d`，tag `1.0.0`，Release 已发布

### 3. 中英提示延长到 3 秒

- `src/HanLin/EngineHost.cpp:504` `ShowWithTimeout(3000)`
- `src/WeaselUI/WeaselUI.cpp:197` 钳制上限 3000
- `rime-config/weasel.custom.yaml:14` `show_notifications_time: 3000`
- 已编译 `weasel.dll`（x64/x86）并部署到 `System32`/`SysWOW64`

### 4. README 移除小狼毫兼容内容

- 删除 `### 小狼毫兼容配置` 整节
- 产品定位改为「独立软件」
- 保留：迁移章节 3 处、致谢 GPL-3.0 归属、代码目录名

## 三、性能优化任务（你的主要工作）

### 当前性能基线（0.2.2 测得）

| 指标 | 数值 |
|---|---|
| 冷启动 | 293 ms |
| 热启动 | 385 ms |
| 内存 | 46 MB |
| 候选延迟 | 5.1 ms |

### 优化方向

1. **启动时间**：`HanLinHost.exe` 启动流程、librime 初始化、词库加载
2. **内存占用**：`rime.dll` 词库缓存、`HanLinHost` 进程内存
3. **候选延迟**：IPC 往返、候选渲染、`WeaselUI` 绘制
4. **IPC 效率**：命名管道消息大小/频率

### 约束

- **版本纪律**：不提交/推送/打 tag/发布，除非用户明确指示
- **账本**：每次改动追加 `PENDING-CHANGES.md`
- **测试**：`test_keypad.py` 100 项、`ssf_tests.exe` 237 项、`host-regression.exe`、`panel-bench.exe`
- **构建**：`pwsh -NoProfile -File src/build-hanlin.ps1`（前端）、`src/build-weasel.ps1 -Both`（Weasel 后端）
- **部署**：`src/deploy-tsf.ps1`（自动提权）

## 四、关键文件

| 文件 | 作用 |
|---|---|
| `src/HanLin/EngineHost.cpp` | 前端主循环、IPC、UI 调用 |
| `src/HanLin/HanLinTip.cpp` | TSF 入口 |
| `src/HanLin/HanLinIPC.cpp` | 命名管道 IPC |
| `src/WeaselUI/WeaselUI.cpp` | 候选窗渲染 |
| `src/WeaselTSF/` | 上游 TSF 实现（参考） |
| `src/tools/WordManager.ps1` | 词条管理 GUI |
| `rime-config/` | 配置源 |
| `D:\RimeUser\` | 用户配置（部署目标） |

## 五、当前部署状态

- 用户当前用 **Weasel 后端**（`weasel.dll` TSF），不是寒淋前端（`HanLinTip.dll`）
- `WeaselServer.exe` PID 40428（23:32:38）
- `System32\weasel.dll` `480670B9`、`SysWOW64\weasel.dll` `CC0A048F`
