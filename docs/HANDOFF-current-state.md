# 交接任务：寒淋输入法当前状态与后续任务

> 交接对象：Codex
> 交接人：DeepSeek Harness（寒淋输入法维护会话）
> 日期：2026-11-19
> 仓库：`D:\Rime`（分支 `snapshot`，HEAD `8acd08f`，版本 0.2.3）
> GitHub：`TangSong777/HanLin-IME`

---

## 零、一句话目标

寒淋输入法已经完成「自研 TSF 前端 + librime 1.17.0 引擎」的改造，当前版本 **0.2.3**。接下来要做的是：**前端 UI 与输入框融洽、性能优化、bug 修复**。

---

## 一、项目当前状态（2026-11-19）

### 1.1 架构

```
用户按键
  → Windows TSF 框架
    → HanLinTip.dll（自研 TSF 文本服务，源码在 D:\Rime\src\HanLin）
      → HanLinIPC（命名管道）
        → HanLinHost.exe（D:\Rime\src\output\hanlin-tip-x64\HanLinHost.exe）
          → rime.dll（librime 1.17.0，改源码重编译）
            → rime-ice 方案（D:\RimeUser）
```

候选窗口由 `src/WeaselUI/` 渲染（GDI+ / DirectWrite），皮肤是 Color-P（SSF 兼容层）。

### 1.2 关键路径

| 用途 | 路径 |
|---|---|
| 仓库 | `D:\Rime` |
| 用户配置目录 | `D:\RimeUser` |
| 安装目录 | `D:\Rime\weasel-0.17.4`（**不是** `C:\Program Files`） |
| 自研前端源码 | `D:\Rime\src\HanLin` |
| librime 源码 | `D:\Rime\reference\librime-reference` |
| Boost | `D:\boost182` |
| 构建脚本 | `D:\Rime\src\build-hanlin.ps1` |
| 打包脚本 | `D:\Rime\src\installer\build-installer.ps1` |
| 部署脚本 | `D:\Rime\src\deploy-tsf.ps1`（TSF DLL）、`HanLinHost.exe --deploy`（配置） |
| 账本 | `D:\Rime\PENDING-CHANGES.md` |
| 版本号 | `src/installer/HanLin.iss:2`（`MyAppVersion`） |

### 1.3 当前状态

- 版本 `0.2.3`（GitHub Latest），tag → `9fedebf`
- 工作区干净（只有 `PENDING-CHANGES.md` 一条账本改动）
- `rime.dll` 是 librime 1.17.0 改源码重编译版（`D:\Rime\weasel-0.17.4\rime.dll`）
- TSF DLL 已部署（`C:\Windows\System32\HanLinTip.dll`、`C:\Windows\SysWOW64\HanLinTip.dll`）

### 1.4 已完成的任务

| 任务 | 状态 | 版本 |
|---|---|---|
| 自研 TSF 前端（`src/HanLin/`） | ✅ 完成 | 0.2.0 |
| librime 升级到 1.17.0 | ✅ 完成 | 0.2.0 |
| 动态数字（`enable_dynamic_numeric`） | ✅ 完成 | 0.2.0 |
| 仓库改名 `HanLin-IME` | ✅ 完成 | 0.2.0 |
| 缓存指纹刷新 | ✅ 完成 | 0.2.2 |
| 版本号单一来源 | ✅ 完成 | 0.2.2 |
| 打包校验 | ✅ 完成 | 0.2.2 |
| 前端稳定性修复 | ✅ 完成 | 0.2.3 |
| 移除 `cn_en` / `sogou_phrases` / `numeric` | ✅ 完成 | 0.2.3 |

### 1.5 词库现状

- `custom_phrase.txt`：91 个英文白名单 + 19 个中文短码
- `mixed_tokens.dict.yaml`：由 `custom_phrase.txt` 生成（91 词 + ESP32/STM32/杨寒淋）
- `cn_en`、`sogou_phrases`、`numeric` 已移除
- rime-ice 自带中文词库（`cn_dicts/8105`、`cn_dicts/41448`）

---

## 二、后续任务

### 任务 A：前端 UI 与输入框融洽

**目标**：候选窗的视觉风格与宿主输入框更协调（圆角、阴影、间距、字体、颜色）。

**现状**：
- 候选窗由 `src/WeaselUI/` 渲染（GDI+ / DirectWrite）
- 皮肤是 Color-P（SSF 兼容层，`C:\ProgramData\ColorPWeasel\Color-P`）
- `skin.ini` 控制字体、颜色、间距

**建议方向**：
1. 候选窗圆角/阴影（`skin.ini` 的 `corner_radius`、`shadow`）
2. 跟随系统深色模式（`weasel.custom.yaml` 的 `style/color_scheme_dark`）
3. 字体渲染（DirectWrite 的 `font_ch`/`font_en`）
4. 高 DPI 适配

### 任务 B：性能优化

**目标**：启动速度、内存占用、按键延迟。

**基线**（0.2.2 测量）：
- 启动到工作集 >20MB：293 ms
- 词典加载完成：385 ms
- 稳定内存：工作集 46 MB / 私有 52.6 MB
- 窗口重建：5.1 ms

**建议方向**：
1. 启动：延迟加载词库、缓存编译产物
2. 内存：词库 mmap、候选池复用
3. 渲染：候选窗双缓冲、避免重绘
4. Lua：配置缓存（已做 `page_size`）

### 任务 C：bug 修复

**已知问题**：无（0.2.3 已修复前端稳定性）。

**待发现**：用户实测反馈。

---

## 三、约束

1. **版本纪律**：不提交/不发布/不改版本号，除非用户明确指示
2. **账本**：每次改动追加 `PENDING-CHANGES.md`
3. **测试**：`src/test/TestHanLin/HanLinFrontendRegression.cpp`（10 项）、`src/test/TestSsfSkin/test_keypad.py`（100 项）、`src/dist/ssf_tests.exe`（237 项）
4. **构建**：`pwsh -File src/build-hanlin.ps1`
5. **打包**：`pwsh -File src/installer/build-installer.ps1 -SharedDataDir "D:\Rime\weasel-0.17.4\data" -RimeSourceDir "D:\RimeUser"`
6. **部署**：`pwsh -File src/deploy-tsf.ps1`（TSF DLL）、`HanLinHost.exe --deploy --user-data D:\RimeUser`（配置）

---

## 四、参考文件

| 文件 | 说明 |
|---|---|
| `docs/HANDOFF-major-refactor.md` | 大重构交接（0.1.7 → 0.2.0） |
| `docs/HANDOFF-numeric-dynamic.md` | 动态数字交接 |
| `CHANGELOG.md` | 版本历史 |
| `PENDING-CHANGES.md` | 账本 |
| `README.md` | 项目说明 |
