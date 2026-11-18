# 交接任务：Color-P 大重构（自研前端 + librime 升级 + 全量优化 + 仓库改名）

> 交接对象：Codex
> 交接人：DeepSeek Harness（Color-P 维护会话）
> 日期：2026-11-18
> 仓库：`D:\Rime`（分支 `snapshot`，HEAD `29a261c`，版本 0.1.7）
> GitHub：`TangSong777/Color-P`

---

## 零、一句话目标

把 Color-P 从「Weasel 前端 + librime 引擎」改造成「**自研 TSF 前端 + 最新 librime 引擎**」，同时做全量性能优化，并把仓库改名为「寒淋输入法」（`HanLin-xxxx`）。

---

## 一、项目背景

### 1.1 当前架构

```
用户按键
  → Windows TSF 框架
    → weasel.dll（Weasel 0.17.4 的 TSF 文本服务，源码在 D:\Rime\src\WeaselTSF）
      → WeaselIPC（命名管道）
        → WeaselServer.exe（D:\Rime\weasel-0.17.4\WeaselServer.exe）
          → rime.dll（librime 1.13.1，官方预编译）
            → rime-ice 方案（D:\RimeUser）
```

候选窗口由 `src/WeaselUI/` 渲染（GDI+ / DirectWrite），皮肤是 Color-P（SSF 兼容层）。

### 1.2 关键路径

| 用途 | 路径 |
|---|---|
| 仓库 | `D:\Rime` |
| 用户配置目录 | `D:\RimeUser` |
| 安装目录 | `D:\Rime\weasel-0.17.4`（**不是** `C:\Program Files`） |
| librime 源码参考 | `D:\Rime\reference\librime-reference` |
| Boost | `D:\boost182` |
| 构建脚本 | `D:\Rime\src\build-weasel.ps1`、`D:\Rime\src\build-support\Find-Toolchain.ps1` |
| 部署脚本 | `D:\Rime\src\deploy-tsf.ps1`（TSF DLL）、`WeaselDeployer.exe /deploy`（配置） |
| 账本 | `D:\Rime\PENDING-CHANGES.md` |
| 版本号 | `src/installer/Color-P.iss:23/25`、`src/installer/build-installer.ps1:42` |

### 1.3 当前状态（2026-11-18）

- 版本 `0.1.7`（GitHub Latest），tag → `29a261c`
- 工作区有 **29 个未提交文件**（codex 刚做的改动，见下）
- `rime.dll` 已被 codex 重新编译并替换（`D:\Rime\weasel-0.17.4\rime.dll`，3,572,736 字节，哈希 `c6309463d5ab3192188f93d4e3189efd85c9ea3eae5b69a5ebf819fb108f1f88`）
- TSF DLL 已部署（`C:\Windows\System32\weasel.dll` 1,226,752 字节、`C:\Windows\SysWOW64\weasel.dll` 1,067,008 字节，都是 Nov 17 20:41）

### 1.4 codex 刚做的改动（未提交）

**librime 源码**（`reference/librime-reference/`）：

- `dictionary.h`：`DictEntryIterator::AddCustomEntry(an<DictEntry>)`、`Dictionary::Decode(code, result, syllable_graph)` 重载
- `dictionary.cc`：+83 行
- `script_translator.cc`：+167 行（数字音节处理）
- `syllabifier.cc`：+35 行
- `strings.cc`：+21 行
- `abc_segmentor.cc`：+13 行
- `user_dictionary.cc`：+43 行
- `test/segmentor_test.cc`：+28 行、`test/syllabifier_test.cc`：+50 行

**配置**（`rime-config/rime_ice.custom.yaml`）：

- `translator/enable_dynamic_numeric: true`（数字动态音节）
- `translator/enable_auto_phrase: true`（自动造词）
- `speller/enable_dynamic_symbols: true`（标点参与造句）
- `translator/dynamic_punctuation: "， 。 -"`
- `speller/initials` 加数字
- `speller/alphabet` 加 `，。`
- `engine/filters/@9: lua_filter@*keypad_input*literal_search@radical_pinyin`
- `speller/algebra/+` 加 5 条纠错（`gn→ng`、`mg→ng`、`iou→iu`、`uei→ui`、`uen→un`）

**词库**：`rime-config/rime_ice.dict.yaml` 注释掉 `- mixed_tokens` 与 `- numeric`（numeric 词库移除）

**账本**：`PENDING-CHANGES.md` +69 行，追加 3 条

**其他**：`custom_phrase.txt` +85 行、`keypad_input.lua` +129 行、`mixed_tokens.dict.yaml` +1、`gen-word-table.ps1` +10、`deploy-tsf.ps1` +6、`src/WeaselTSF/` 3 个文件、`src/WeaselUI/WeaselUI.cpp` +4

### 1.5 已知问题（需要先修）

**用户目录 `D:\RimeUser\rime_ice.custom.yaml` 缺 3 项**（仓库版有，用户目录没有）：

- `translator/enable_auto_phrase: true`
- `speller/enable_dynamic_symbols: true`
- `engine/filters/@9: lua_filter@*keypad_input*literal_search@radical_pinyin`

导致部署后 `D:\RimeUser\build\rime_ice.schema.yaml` 只有 `enable_dynamic_numeric` 和 `dynamic_punctuation` 两项生效。

---

## 二、任务 A：自研 TSF 前端（替代 Weasel 的 `weasel.dll`）

### 2.1 目标

不再使用 Weasel 的 TSF 文本服务，**自己写一个 TSF 文本服务**，只调用 librime 引擎。

### 2.2 需要实现的 TSF 接口

参考 `D:\Rime\src\WeaselTSF\`（28 个文件，可以借鉴但不能直接复制）：

| 接口 | 作用 | 参考文件 |
|---|---|---|
| `ITfTextInputProcessor` | 激活/停用 | `WeaselTSF.cpp` |
| `ITfThreadMgrEventSink` | 焦点变化 | `ThreadMgrEventSink.cpp` |
| `ITfKeyEventSink` | 按键 | `KeyEventSink.cpp` |
| `ITfCompositionSink` | 组合结束 | `Composition.cpp` |
| `ITfDisplayAttributeProvider` | 显示属性 | `DisplayAttributeProvider.cpp` |
| `ITfTextEditSink` | 文本编辑 | `TextEditSink.cpp` |
| `ITfCompartmentEventSink` | 输入法状态 | `Compartment.cpp` |
| `ITfLanguageBarItemProvider` | 语言栏 | `LanguageBar.cpp` |

### 2.3 候选窗口

当前 `src/WeaselUI/` 用 GDI+ / DirectWrite 渲染，皮肤是 Color-P（SSF 兼容层）。自研前端可以：

- **方案 1**：复用 `src/WeaselUI/` 的渲染代码（`WeaselPanel.cpp`、`Layout.cpp`、`DirectWriteResources.cpp`）
- **方案 2**：用 Direct2D 重写（更现代，但工作量大）
- **方案 3**：用 WebView2 / 独立窗口（不推荐，性能差）

**建议**：先复用 `src/WeaselUI/`，把 TSF 层换掉，跑通后再考虑重写渲染。

### 2.4 librime 引擎调用

通过 `RimeApi`（`reference/librime-reference/src/rime_api.h`）：

```c
RimeApi* rime_get_api();
// 初始化
RimeTraits traits = {0};
traits.shared_data_dir = "...";
traits.user_data_dir = "...";
rime->setup(&traits);
rime->initialize(&traits);
// 会话
RimeSessionId session = rime->create_session();
// 按键
rime->process_key(session, keycode, mask);
// 取上下文
RimeContext ctx = {0};
rime->get_context(session, &ctx);
// 提交
rime->commit_composition(session);
```

### 2.5 验收标准

- 能在记事本、浏览器、VS Code、UWP 应用（便笺）里正常输入
- 候选窗口位置跟随光标
- 中英文切换、标点、数字、符号行为与当前一致
- 不依赖 `weasel.dll`（可以卸载 Weasel 前端）

---

## 三、任务 B：librime 升级到 1.17.0

### 3.1 当前版本

- librime **1.13.1**（官方预编译 `rime.dll`，3,524,096 字节）
- codex 已改源码并重新编译（3,572,736 字节）

### 3.2 目标版本

- librime **1.17.0**（GitHub `rime/librime` 最新 release，2026-11 查得）

### 3.3 关键：保留 codex 的改动

codex 在 1.13.1 源码上做了这些改动，**必须移植到 1.17.0**：

1. `dictionary.h` / `dictionary.cc`：`DictEntryIterator::AddCustomEntry` + `custom_entries_` 通道
2. `dictionary.h` / `dictionary.cc`：`Dictionary::Decode(code, result, syllable_graph)` 重载
3. `script_translator.cc`：`ScriptTranslation::Evaluate` 里数字音节处理（+167 行）
4. `syllabifier.cc`：+35 行
5. `strings.cc`：+21 行
6. `abc_segmentor.cc`：+13 行
7. `user_dictionary.cc`：+43 行
8. `test/segmentor_test.cc`、`test/syllabifier_test.cc`：测试

### 3.4 编译依赖

| 依赖 | 状态 | 说明 |
|---|---|---|
| Boost 1.82 | ✅ | `D:\boost182` |
| glog / leveldb / marisa-trie / opencc / yaml-cpp / googletest | ✅ | `reference/librime-reference/deps/` |
| **librime-lua** | ❌ | 需从 GitHub 下载 `hchunhui/librime-lua` → `plugins/lua` |
| **lua 5.4 源码** | ❌ | librime-lua 依赖 |
| MSVC | ✅ | VS 2022 |
| CMake | ✅ | |

**重要**：`rime.dll` 必须编进 librime-lua，否则 `keypad_input.lua`、`dedupe_filter.lua` 等全部失效。

### 3.5 验收标准

- `rime.dll` 版本 1.17.0
- 所有现有功能正常（数字动态音节、标点造句、五组纠错、自动造词）
- `test_keypad.py` 100 项、`ssf_tests.exe` 237 项全过
- 启动时间、内存不退化

---

## 四、任务 C：全量代码优化

### 4.1 当前基线

- 启动到工作集 >20MB：**293 ms**
- 词典加载完成（48.7 MB）：**385 ms**
- 稳定内存：工作集 **46 MB** / 私有 **52.6 MB**
- 窗口重建：**5.1 ms**

### 4.2 优化方向

1. **启动优化**：词库懒加载、词典索引预编译
2. **内存优化**：词库压缩、缓存策略
3. **渲染优化**：候选窗口双缓冲、DirectWrite 缓存
4. **Lua 优化**：`keypad_input.lua` 的配置查询缓存（已做）、过滤器开销
5. **IPC 优化**：WeaselIPC 命名管道往返延迟

### 4.3 验收标准

- 启动时间 ≤ 当前
- 内存 ≤ 当前
- 所有测试通过

---

## 五、任务 D：仓库改名「寒淋输入法」

### 5.1 目标

- GitHub 仓库：`TangSong777/Color-P` → `TangSong777/HanLin-xxxx`（具体后缀由用户定）
- 项目名：Color-P → **寒淋输入法**
- 安装包名：`Color-P-Setup-x.y.z.exe` → `HanLin-Setup-x.y.z.exe`

### 5.2 需要改的地方

| 位置 | 内容 |
|---|---|
| GitHub 仓库设置 | 改名（GitHub 会自动重定向旧 URL） |
| 本地 remote | `git remote set-url origin https://github.com/TangSong777/HanLin-xxxx.git` |
| `README.md` | 标题、徽章、链接、安装包名 |
| `CHANGELOG.md` | 项目名 |
| `src/installer/Color-P.iss` | `MyAppName`、`OutputBaseFilename`、`AppId` |
| `src/installer/build-installer.ps1` | `$PackageName`、`$PackageVersion` |
| `src/WeaselTSF/WeaselTSF.rc` | 产品名、公司名 |
| `src/WeaselUI/` | 窗口标题 |
| 皮肤目录 | `C:\ProgramData\ColorPWeasel\Color-P` → 新名字 |
| 注册表 | `HKLM:\SOFTWARE\WOW6432Node\Rime\Weasel` 的 `WeaselRoot` |

### 5.3 注意

- **改名是破坏性变更**：旧版本用户升级会变成两个输入法
- 建议**先改 GitHub 仓库名**（自动重定向），**再改代码里的名字**
- 安装包 `AppId` 不能变（否则升级会装两份）

---

## 六、约束

1. **不要提交、不要发布、不要改版本号**。所有改动先记到 `D:\Rime\PENDING-CHANGES.md`，等用户明确说「发布」才走发布流程。
2. **保留回滚方案**：改之前备份 `rime.dll`、`weasel.dll`、`D:\RimeUser` 配置。
3. **测试必须全过**：`python src/test/TestSsfSkin/test_keypad.py rime-config/lua/keypad_input.lua`（100 项）、`src/dist/ssf_tests.exe`（237 项）。
4. **部署后验证**：`WeaselDeployer.exe /deploy` + 重启 `WeaselServer.exe`，然后实际输入测试。
5. **不要动 `D:\RimeUser` 以外的用户数据**。
6. **先修 1.5 的已知问题**（用户目录缺 3 项配置）。

---

## 七、建议顺序

1. **先修 1.5 的已知问题**（用户目录补 3 项配置 + 部署 + 验证）
2. **任务 B（librime 升级）**：先把引擎升到 1.17.0，保留 codex 的改动
3. **任务 A（自研前端）**：引擎稳定后再换前端
4. **任务 C（全量优化）**：前端稳定后做
5. **任务 D（仓库改名）**：最后做（改名影响面大）

---

## 八、参考文件

| 文件 | 说明 |
|---|---|
| `D:\Rime\src\WeaselTSF\` | 当前 TSF 前端（28 个文件） |
| `D:\Rime\src\WeaselUI\` | 当前候选窗口渲染（20 个文件） |
| `D:\Rime\reference\librime-reference\src\rime_api.h` | librime C API |
| `D:\Rime\reference\librime-reference\src\rime\dict\dictionary.h` | `DictEntryIterator` |
| `D:\Rime\reference\librime-reference\src\rime\gear\script_translator.cc` | `ScriptTranslation::Evaluate` |
| `D:\Rime\docs\HANDOFF-numeric-dynamic.md` | 之前的 numeric 交接文档 |
| `D:\Rime\PENDING-CHANGES.md` | 改动账本 |
| `D:\Rime\README.md` | 项目说明 |
