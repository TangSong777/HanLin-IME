# 自定义字词总表

> 本文件由 `src/build-support/gen-word-table.ps1` 从实际词条文件生成，**请勿手工编辑**。
>
> 生成时间：2026-11-10 09:13

## 概览

> 2026-11-10 起英文白名单停用造句（`rime_ice.dict.yaml` 未导入 `mixed_tokens`）；下表「参与造句 ✅」只表示词条具备该能力，恢复导入后才实际生效。

| 类别 | 数量 | 输入方式 | 参与造句 |
|---|---:|---|---|
| 英文白名单 | 91 | 全小写编码 | ✅（同时也是词典词条） |
| 中文短码 | 22 | 单/双字母 | — |
| 数字词条 | 100000（0–99999） | **小键盘**数字 | ✅ |
| **合计自维护词条** | 113 | — | — |

> 数字词条由脚本批量生成，不逐条列出；范围可改（见文末）。

## 英文白名单（91 条）

按分类排列。编码一律小写；**输入时必须小写**（大写走方案的「大写字母造词」通道）。

| # | 词条 | 编码 | 分类 | 参与造句 |
|---:|---|---|---|:---:|
| 1 | Bug | `bug` | 其他 | ✅ |
| 2 | CommandCode | `commandcode` | 其他 | ✅ |
| 3 | Anthropic | `anthropic` | AI/模型 | — |
| 4 | ChatGPT | `chatgpt` | AI/模型 | ✅ |
| 5 | Claude | `claude` | AI/模型 | ✅ |
| 6 | Codex | `codex` | AI/模型 | ✅ |
| 7 | Copilot | `copilot` | AI/模型 | ✅ |
| 8 | DeepSeek | `deepseek` | AI/模型 | ✅ |
| 9 | Gemini | `gemini` | AI/模型 | ✅ |
| 10 | Grok | `grok` | AI/模型 | ✅ |
| 11 | Kimi | `kimi` | AI/模型 | ✅ |
| 12 | OpenAI | `openai` | AI/模型 | — |
| 13 | OpenCode | `opencode` | AI/模型 | — |
| 14 | prompt | `prompt` | AI/模型 | ✅ |
| 15 | Qwen | `qwen` | AI/模型 | ✅ |
| 16 | AMD | `amd` | 厂商/平台 | — |
| 17 | Cloudflare | `cloudflare` | 厂商/平台 | ✅ |
| 18 | Debian | `debian` | 厂商/平台 | ✅ |
| 19 | Docker | `docker` | 厂商/平台 | ✅ |
| 20 | GitHub | `github` | 厂商/平台 | ✅ |
| 21 | GitLab | `gitlab` | 厂商/平台 | ✅ |
| 22 | Google | `google` | 厂商/平台 | ✅ |
| 23 | Intel | `intel` | 厂商/平台 | — |
| 24 | Linux | `linux` | 厂商/平台 | ✅ |
| 25 | MicroSoft | `microsoft` | 厂商/平台 | ✅ |
| 26 | NVIDIA | `nvidia` | 厂商/平台 | ✅ |
| 27 | PowerShell | `powershell` | 厂商/平台 | ✅ |
| 28 | Ubuntu | `ubuntu` | 厂商/平台 | — |
| 29 | Vim | `vim` | 厂商/平台 | — |
| 30 | VMware | `vmware` | 厂商/平台 | — |
| 31 | VPN | `vpn` | 厂商/平台 | — |
| 32 | VSCode | `vscode` | 厂商/平台 | — |
| 33 | Windows | `windows` | 厂商/平台 | ✅ |
| 34 | WinScp | `winscp` | 厂商/平台 | ✅ |
| 35 | Chinese | `chinese` | 语言/格式 | ✅ |
| 36 | CPU | `cpu` | 语言/格式 | ✅ |
| 37 | English | `english` | 语言/格式 | — |
| 38 | GPU | `gpu` | 语言/格式 | ✅ |
| 39 | HDMI | `hdmi` | 语言/格式 | ✅ |
| 40 | Java | `java` | 语言/格式 | ✅ |
| 41 | JavaScript | `javascript` | 语言/格式 | ✅ |
| 42 | Markdown | `markdown` | 语言/格式 | ✅ |
| 43 | NVMe | `nvme` | 语言/格式 | ✅ |
| 44 | Python | `python` | 语言/格式 | ✅ |
| 45 | RAM | `ram` | 语言/格式 | ✅ |
| 46 | Rust | `rust` | 语言/格式 | ✅ |
| 47 | SATA | `sata` | 语言/格式 | ✅ |
| 48 | SSD | `ssd` | 语言/格式 | ✅ |
| 49 | Arduino | `arduino` | 软件/游戏 | — |
| 50 | AutoCAD | `autocad` | 软件/游戏 | — |
| 51 | Blender | `blender` | 软件/游戏 | ✅ |
| 52 | CDDA | `cdda` | 软件/游戏 | ✅ |
| 53 | Chrome | `chrome` | 软件/游戏 | ✅ |
| 54 | Clash | `clash` | 软件/游戏 | ✅ |
| 55 | Excel | `excel` | 软件/游戏 | — |
| 56 | Flash | `flash` | 软件/游戏 | ✅ |
| 57 | Geek | `geek` | 软件/游戏 | ✅ |
| 58 | Keil | `keil` | 软件/游戏 | ✅ |
| 59 | MATLAB | `matlab` | 软件/游戏 | ✅ |
| 60 | Minecraft | `minecraft` | 软件/游戏 | ✅ |
| 61 | Multisim | `multisim` | 软件/游戏 | ✅ |
| 62 | Notion | `notion` | 软件/游戏 | ✅ |
| 63 | OBS | `obs` | 软件/游戏 | — |
| 64 | Obsidian | `obsidian` | 软件/游戏 | — |
| 65 | PlatformIO | `platformio` | 软件/游戏 | ✅ |
| 66 | PowerPoint | `powerpoint` | 软件/游戏 | ✅ |
| 67 | PyCharm | `pycharm` | 软件/游戏 | ✅ |
| 68 | SolidWorks | `solidworks` | 软件/游戏 | ✅ |
| 69 | Steam | `steam` | 软件/游戏 | ✅ |
| 70 | STM32CubeIDE | `stmcubeide` | 软件/游戏 | ✅ |
| 71 | STM32CubeMX | `stmcubemx` | 软件/游戏 | ✅ |
| 72 | Undertale | `undertale` | 软件/游戏 | — |
| 73 | VisualStudio | `visualstudio` | 软件/游戏 | — |
| 74 | Word | `word` | 软件/游戏 | ✅ |
| 75 | XMind | `xmind` | 软件/游戏 | ✅ |
| 76 | blogs | `blogs` | 工具/其他 | ✅ |
| 77 | Desktop | `desktop` | 工具/其他 | ✅ |
| 78 | ffmpeg | `ffmpeg` | 工具/其他 | ✅ |
| 79 | Listary | `listary` | 工具/其他 | ✅ |
| 80 | manager | `manager` | 工具/其他 | ✅ |
| 81 | Moonlight | `moonlight` | 工具/其他 | ✅ |
| 82 | Patina | `patina` | 工具/其他 | ✅ |
| 83 | PixPin | `pixpin` | 工具/其他 | ✅ |
| 84 | README | `readme` | 工具/其他 | ✅ |
| 85 | server | `server` | 工具/其他 | ✅ |
| 86 | session | `session` | 工具/其他 | ✅ |
| 87 | stream | `stream` | 工具/其他 | ✅ |
| 88 | Sunshine | `sunshine` | 工具/其他 | ✅ |
| 89 | Wallpaper | `wallpaper` | 工具/其他 | ✅ |
| 90 | WorkSpace | `workspace` | 工具/其他 | ✅ |
| 91 | ZeroTier | `zerotier` | 工具/其他 | ✅ |

## 其它参与造句的词条（3 条）

数字需用**小键盘**输入；它们同样参与造句。

| # | 词条 | 编码 | 权重 |
|---:|---|---|---:|
| 1 | ESP32 | `esp32` | 100000 |
| 2 | STM32 | `stm32` | 100000 |
| 3 | 杨寒淋 | `yanghanlin` | 100000 |

## 中文短码（22 条 / 16 个编码）

同一编码下按候选顺序排列（第一条优先级最高）。

| # | 编码 | 词条（按候选顺序） | 说明 |
|---:|---|---|---|
| 1 | `hm` | 「噷」 | 语气词 |
| 2 | `m` | 「呣」 「呒」 | 语气词 |
| 3 | `u` | 「有」 「又」 「由」 | 高频单字，u+数字 可调序 |
| 4 | `i` | 「一」 「以」 「已」 「亦」 | 高频单字，i+数字 可调序 |
| 5 | `ig` | 「一个」 | 常用简拼 |
| 6 | `ug` | 「有个」 | 常用简拼 |
| 7 | `sig` | 「是一个」 | 常用简拼 |
| 8 | `uig` | 「有一个」 | 常用简拼 |
| 9 | `uyig` | 「有一个」 | 常用简拼 |
| 10 | `uyige` | 「有一个」 | 常用简拼 |
| 11 | `umu` | 「有没有」 | 常用简拼 |
| 12 | `umeiu` | 「有没有」 | 常用简拼 |
| 13 | `uu` | 「又有」 | 常用简拼 |
| 14 | `uuu` | 「又又又」 | 常用简拼 |
| 15 | `uuuu` | 「又双叒叕」 | 网络梗「又双叒叕」 |
| 16 | `yanghanlin` | 「杨寒淋」 | 常用简拼 |

## 数字词条

由 `numeric.dict.yaml` 覆盖 **0–99999**，共 100000 条，权重统一 1000。

它们的作用是让数字能像音节一样参与造句：

```
youyijianshiqingshishuo32meiyou26da  →  有一件事情是说32没有26大
```

需要更大范围时重新生成（会增大词库和部署时间）：

```powershell
pwsh -File .\src\build-support\gen-numeric-dict.ps1 -Max 999999
```

## 使用规则

| 规则 | 说明 |
|---|---|
| 编码一律小写 | 打 `CPU` 得到的是大写字母造词，不是词条 |
| 编码与词条都不含空格 | `visualstudio`、`platformio` |
| 含数字的词编码去数字 | `STM32CubeMX` → `stmcubemx`；数字编码本身要用小键盘 |
| 主键盘数字只选词 | 1–9（0 = 第 10 个）永远是选词，不会进入编码 |
| 小键盘数字才输入数字 | `KP_0`–`KP_9` 参与拼写与造句 |
| 英文词条可参与造句 | 需先恢复 `mixed_tokens` 导入；停用前如 `arduinoqishiyejiunayang` → 「Arduino其实也就那样」 |

## 如何增删词条

| 想改什么 | 改哪里 | 之后做什么 |
|---|---|---|
| 英文词 / 中文短码 | `custom_phrase.txt`（`词条<Tab>编码`） | 重新部署 |
| 恢复英文造句 | 取消 `rime_ice.dict.yaml` 里 `- mixed_tokens` 的注释 | 重新部署 |
| 数字范围 | 重跑 `gen-numeric-dict.ps1` | 重新部署 |
| 本表格 | 重跑 `gen-word-table.ps1` | 提交 |

---

返回 [README](../README.md)
