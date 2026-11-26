# 交接任务：把 numeric 词库改成「动态识别数字」

> 交接对象：Codex
> 交接人：DeepSeek Harness（SSF 维护会话）
> 日期：2026-11-18
> 仓库：`D:\Rime`（分支 `snapshot`，HEAD `29a261c`，版本 0.1.7）

---

## 一、项目背景

SSF 是一个 Windows 输入法项目，基于：

- **Weasel 0.17.4**（TSF 前端，源码在 `D:\Rime\src`）
- **官方 librime 1.13.1 预编译 `rime.dll`**（`D:\Rime\src\dist\rime.dll`，3,524,096 字节，含 librime-lua 插件）
- **rime-ice** 方案（配置在 `D:\RimeUser`）
- 自定义皮肤（SSF / 原作者）

关键路径：

| 用途 | 路径 |
|---|---|
| 仓库 | `D:\Rime` |
| 用户配置目录 | `D:\RimeUser` |
| 安装目录 | `C:\Program Files\Rime\weasel-0.17.4` |
| librime 源码参考 | `D:\Rime\reference\librime-reference` |
| Boost | `D:\boost182` |
| 构建脚本 | `D:\Rime\src\build-weasel.ps1`（TSF 前端）、`D:\Rime\src\build-support\Find-Toolchain.ps1`（工具链探测） |
| 部署脚本 | `D:\Rime\src\deploy-tsf.ps1`（TSF DLL）、`WeaselDeployer.exe /deploy`（配置） |

---

## 二、当前问题

`D:\RimeUser\numeric.dict.yaml` 是一个 **1.78 MB、10 万条（0–99999）** 的普通词库，用来让数字能参与组词：

- 输入 `258` → 候选 `258`
- 输入 `keai258shenmi` → 候选 `可爱258神秘`

这是「暴力枚举」：每个数字都占一条词条，导致：

- 词库总体积 48 MB（numeric 占 3.7%）
- 启动加载慢（词典加载约 385 ms）
- 内存占用高（工作集 46 MB）

---

## 三、目标

保留「数字能参与组词」的能力，但**不再靠词库枚举**，改成在 librime 源码里**动态识别纯数字音节**并生成候选。

### 验收标准

| 输入 | 期望 |
|---|---|
| `258` | 候选 `258` |
| `keai258shenmi` | 候选 `可爱258神秘` |
| `ni1`–`ni5` | 数字选词正常（第 1–5 候选） |
| `ni6`–`ni9`、`ni0` | 无操作（page_size 5） |
| 删除 `numeric.dict.yaml` 后 | 上述行为不变 |
| 启动时间 | 不退化（当前 293 ms 到工作集 >20MB） |
| 内存 | 不退化（当前工作集 46 MB） |
| 现有测试 | `test_keypad.py` 100 项 + `ssf_tests.exe` 237 项全过 |

---

## 四、已分析的技术方案

### 4.1 源码链路

```
ScriptTranslator::Query (src/rime/gear/script_translator.cc:193)
  → ScriptTranslation::Evaluate (:365)
    → dict->Lookup(syllable_graph, 0, predict_word)   // src/rime/dict/dictionary.cc:263
      → lookup_table() (:231) 遍历 tables_，每个词条来自 Table 的内存映射
```

关键类型：

- `DictEntryCollector = map<size_t, DictEntryIterator>`（`src/rime/dict/dictionary.h:54`）
- `DictEntryIterator::Peek()`（`src/rime/dict/dictionary.cc:144`）从 `query_result_->chunks[chunk_index_]` 取 `chunk.entries[chunk.cursor]`，构造 `DictEntry`
- `Chunk`（`src/rime/dict/dictionary.cc:21`）字段：`Table* table`、`Code code`、`const table::Entry* entries`、`size`、`cursor`、`remaining_code`、`matching_code_size`、`credibility`
- `exhausted()` = `chunk_index_ >= query_result_->chunks.size()`（`dictionary.cc:212`）
- `Dictionary::Decode(const Code& code, vector<string>* result)`（`dictionary.cc:331`）用 `primary_table()->GetSyllableById(c)` 拿音节文本
- `SyllableGraph`（`src/rime/algo/syllabifier.h:37`）：`input_length`、`interpreted_length`、`vertices`（`map<size_t, SpellingType>`）、`edges`（`map<size_t, EndVertexMap>`，`EndVertexMap = map<size_t, SpellingMap>`，`SpellingMap = map<SyllableId, EdgeProperties>`）、`indices`
- `SyllableId = int32_t`（`src/rime/dict/vocabulary.h:19`）
- `DictEntry`（`src/rime/dict/vocabulary.h:46`）字段：`string text; string comment; string preedit; Code code; string custom_code; double weight; int commit_count; int remaining_code_length; int matching_code_size;`

### 4.2 关键障碍

`DictEntryCollector` 是 `map<size_t, DictEntryIterator>`，而 `DictEntryIterator::Peek()` 只能从 `Chunk` 里取 `table::Entry`——`Chunk.entries` 必须指向真实词库的内存映射。**没有「凭空塞一个词条」的通道**。

### 4.3 改法（两处）

#### 改动 1：`src/rime/dict/dictionary.h` + `dictionary.cc`

给 `DictEntryIterator` 加一个自定义条目通道：

```cpp
// dictionary.h
class DictEntryIterator {
 public:
  ...
  void AddCustomEntry(an<DictEntry> entry);   // 新增
  ...
 private:
  ...
  vector<an<DictEntry>> custom_entries_;      // 新增
  size_t custom_index_ = 0;                   // 新增
};
```

`Peek()` / `Next()` / `exhausted()` 优先处理 `custom_entries_`：

```cpp
bool exhausted() const {
  return custom_index_ >= custom_entries_.size() &&
         chunk_index_ >= query_result_->chunks.size();
}
```

#### 改动 2：`src/rime/gear/script_translator.cc` 的 `ScriptTranslation::Evaluate`

`dict->Lookup` 之后遍历 `syllable_graph.edges`，对每个单音节边：

```cpp
for (auto& [start, ends] : syllable_graph.edges) {
  for (auto& [end, spellings] : ends) {
    for (auto& [syllable_id, props] : spellings) {
      vector<string> spelling;
      if (!dict->Decode({syllable_id}, &spelling) || spelling.empty())
        continue;
      const string& s = spelling[0];
      if (s.empty() || !all_of(s.begin(), s.end(), ::isdigit))
        continue;
      auto entry = New<DictEntry>();
      entry->text = s;
      entry->code = {syllable_id};
      entry->weight = 100.0;   // 高权重，保证排在前面
      (*phrase_)[end].AddCustomEntry(entry);
    }
  }
}
```

注意：`phrase_` 是 `ScriptTranslation` 的成员（`DictEntryCollector`），`dict` 是 `Dictionary*`。

### 4.4 编译需求

| 依赖 | 状态 | 说明 |
|---|---|---|
| Boost 1.82 | ✅ | `D:\boost182`（`boost/version.hpp` 确认 `1_82`） |
| glog / leveldb / marisa-trie / opencc / yaml-cpp / googletest | ✅ | `D:\Rime\reference\librime-reference\deps\` |
| **librime-lua** | ❌ | 需从 GitHub 下载 `hchunhui/librime-lua` → `plugins/lua` |
| **lua 5.4 源码** | ❌ | librime-lua 依赖 |
| MSVC | ✅ | VS 2022（`Find-Toolchain.ps1` 可探测） |
| CMake | ✅ | |

**重要**：`src/dist/rime.dll` 里确实编进了 lua 插件（`LuaFilter`、`LuaTranslator`、`LuaSegmentor` 符号，路径 `D:\a\librime\librime\plugins\lua\src\lua_gears.cc`，官方 CI 产物）。**自己编译必须把 librime-lua 也编进去**，否则 `keypad_input.lua`、`dedupe_filter.lua` 等全部失效。

### 4.5 风险

- 替换官方 `rime.dll`，打破项目「librime 用官方 1.13.1 预编译、不做修改」的约束
- librime-lua 版本要和 1.13.1 匹配，否则 Lua 脚本行为可能变
- 每次上游更新都要重编
- 编译产物可能引入难查的 bug

---

## 五、约束

1. **不要提交、不要发布、不要改版本号**。项目有严格的版本纪律：所有改动先记到 `D:\Rime\PENDING-CHANGES.md`，等用户明确说「发布」才走发布流程。
2. **保留回滚方案**：改之前备份 `src/dist/rime.dll` 和安装目录的 `rime.dll`。
3. **不要动 `D:\RimeUser` 以外的用户数据**。
4. **测试必须全过**：`python src/test/TestSsfSkin/test_keypad.py rime-config/lua/keypad_input.lua`（100 项）、`src/dist/ssf_tests.exe`（237 项）。
5. **部署后验证**：`WeaselDeployer.exe /deploy` + 重启 `WeaselServer.exe`，然后实际输入测试。

---

## 六、建议步骤

1. **确认网络**：能拉到 `hchunhui/librime-lua` 和 lua 5.4 源码
2. **备份**：`src/dist/rime.dll`、`C:\Program Files\Rime\weasel-0.17.4\rime.dll`
3. **下载依赖**：librime-lua → `reference/librime-reference/plugins/lua`；lua 5.4 → `reference/librime-reference/deps/lua`
4. **改源码**：按 4.3 的两处改动
5. **编译**：CMake + MSVC，产出 `rime.dll`
6. **替换 + 部署 + 测试**
7. **如果失败**：回滚 `rime.dll`，恢复 `numeric.dict.yaml`

---

## 七、替代方案（如果改源码不可行）

把 `numeric.dict.yaml` 从 `0–99999` 缩到 `0–9999`（1 万条，178 KB）或 `0–999`（1000 条，18 KB），零风险、组词照常。生成脚本在 `D:\Rime\src\build-support\`（找 `numeric` 相关）。

---

## 八、参考文件

| 文件 | 说明 |
|---|---|
| `D:\Rime\reference\librime-reference\src\rime\dict\dictionary.h` | `DictEntryIterator` 定义 |
| `D:\Rime\reference\librime-reference\src\rime\dict\dictionary.cc` | `Peek`/`Next`/`exhausted`/`Lookup`/`Decode` |
| `D:\Rime\reference\librime-reference\src\rime\gear\script_translator.cc` | `ScriptTranslation::Evaluate` |
| `D:\Rime\reference\librime-reference\src\rime\algo\syllabifier.h` | `SyllableGraph` |
| `D:\Rime\reference\librime-reference\src\rime\dict\vocabulary.h` | `DictEntry`、`SyllableId` |
| `D:\Rime\reference\librime-reference\src\rime\dict\prism.h` | `Prism::QuerySpelling` |
| `D:\Rime\src\build-support\Find-Toolchain.ps1` | 工具链探测 |
| `D:\Rime\PENDING-CHANGES.md` | 改动账本 |
