# Codex 接手说明（2026-11-20）

## 你现在在哪

- 项目：`D:\Rime`（GitHub `TangSong777/HanLin-IME`）
- 当前版本：`0.2.3`（2026-11-19 发布）
- HEAD：`8acd08f`（README 清理）
- 工作区：有未提交改动（见下）

## 你上次做到哪

2026-11-18 晚上，你在做**候选窗 / 皮肤设计方案**（`D:\Rime\assets\SSF\` 的皮肤、`YueHei` 字体、候选窗样式），给了几个方案让用户选，最后被中断（`turn_aborted`）。

## 之后发生了什么（你不知道的）

### 1. 移除 cn_en 中英混排词库（2026-11-19）

- `rime_ice.custom.yaml` 删除 `- table_translator@cn_en`
- 中英混排词条（X光 / T恤 / 红Buff 等 963 条）不再参与候选
- 验证：`xguang` → 星光/闲逛（无 X光）

### 2. 移除 sogou_phrases（2026-11-19）

- `rime_ice.custom.yaml` 删除 `- table_translator@sogou_phrases` 和 `sogou_phrases` 段
- 搜狗风格短语不再参与候选

### 3. 移除 numeric 词库（0.2.0 已删）

- `numeric.dict.yaml`（10 万条 0–99999）已删除
- 数字组词改为**动态识别**（librime 源码改动，`rime.dll` 为改源码重编译版）

### 4. 添加英文词条（2026-11-20）

- `custom_phrase.txt` 添加 `Transformer`、`HanLin`（各 2 行：小写编码 + 标准大小写编码）
- 重新生成 `mixed_tokens.dict.yaml`（80 条）

### 5. 新增工具（2026-11-20）

- `src/tools/WordManager.ps1` + `WordManager.vbs`：自造词管理 GUI（列表 / 添加 / 删除 / 部署）
- `docs/ADD-WORDS.md`：添加英文单词的说明

## 当前词库

| 文件 | 行数 / 条数 | 状态 |
|---|---|---|
| `custom_phrase.txt` | 233 行 | 91 英文白名单 + 19 中文短码 + 变体行 |
| `mixed_tokens.dict.yaml` | 80 条 | 由 `gen-mixed-tokens.ps1` 生成 |
| `cn_en.txt` | 963 行 | **已无配置引用**，文件保留 |
| `sogou_phrases.txt` | 41 行 | **已无配置引用**，文件保留 |

## 当前工作区改动（未提交）

```
M PENDING-CHANGES.md
M rime-config/custom_phrase.txt
M rime-config/mixed_tokens.dict.yaml
M rime-config/rime_ice.custom.yaml
?? docs/ADD-WORDS.md
?? docs/HANDOFF-current-state.md
?? src/tools/
```

## 约束

- **版本纪律**：不提交 / 不推送 / 不 bump 版本 / 不 tag / 不重建 release，除非用户明确要求
- 每次改动追加到 `PENDING-CHANGES.md`
- 测试：`python src/test/TestSsfSkin/test_keypad.py rime-config/lua/keypad_input.lua`（100 项）、`src/dist/ssf_tests.exe`（237 项）
- 构建：`pwsh -File src/build-hanlin.ps1 -Both`
- 部署：`WeaselDeployer.exe /deploy` + 重启 `WeaselServer.exe`

## 用户要你改什么

（用户会告诉你）
