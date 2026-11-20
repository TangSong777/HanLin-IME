# 自定义字词总表

> 本文件由 `src/build-support/gen-word-table.ps1` 从实际词条文件生成，**请勿手工编辑**。
>
> 生成时间：2026-11-20 23:10

## 概览

> 造句词表 `mixed_tokens` 由 `custom_phrase.txt` 和额外词条生成。

| 类别 | 数量 | 输入方式 | 参与造句 |
|---|---:|---|---|
| 英文白名单 | 92 个词 / 94 个编码 | 小写编码 | 76 个编码 |
| 中文短码 | 22 | 单/双字母 | — |
| 动态数字 | 引擎识别 | 小键盘数字 | ✅ |
| **合计自维护词条** | 116 | — | — |

## 英文白名单（92 个词 / 94 个编码）

按分类排列。词表保存标准词与小写编码，精确匹配和前缀补全都接受混合大小写。

| # | 词条 | 编码 | 分类 | 参与造句 |
|---:|---|---|---|:---:|
| 1 | Bug | `bug` | 其他 | ✅ |
| 2 | CommandCode | `commandcode` | 其他 | ✅ |
| 3 | Transformer | `transformer` | 其他 | ✅ |
| 4 | Anthropic | `anthropic` | AI/模型 | — |
| 5 | ChatGPT | `chatgpt` | AI/模型 | ✅ |
| 6 | Claude | `claude` | AI/模型 | ✅ |
| 7 | Codex | `codex` | AI/模型 | ✅ |
| 8 | Copilot | `copilot` | AI/模型 | ✅ |
| 9 | DeepSeek | `deepseek` | AI/模型 | ✅ |
| 10 | Gemini | `gemini` | AI/模型 | ✅ |
| 11 | Grok | `grok` | AI/模型 | ✅ |
| 12 | Kimi | `kimi` | AI/模型 | ✅ |
| 13 | OpenAI | `openai` | AI/模型 | — |
| 14 | OpenCode | `opencode` | AI/模型 | — |
| 15 | prompt | `prompt` | AI/模型 | ✅ |
| 16 | Qwen | `qwen` | AI/模型 | ✅ |
| 17 | AMD | `amd` | 厂商/平台 | — |
| 18 | Cloudflare | `cloudflare` | 厂商/平台 | ✅ |
| 19 | Debian | `debian` | 厂商/平台 | ✅ |
| 20 | Docker | `docker` | 厂商/平台 | ✅ |
| 21 | GitHub | `github` | 厂商/平台 | ✅ |
| 22 | GitLab | `gitlab` | 厂商/平台 | ✅ |
| 23 | Google | `google` | 厂商/平台 | ✅ |
| 24 | Intel | `intel` | 厂商/平台 | — |
| 25 | Linux | `linux` | 厂商/平台 | ✅ |
| 26 | MicroSoft | `microsoft` | 厂商/平台 | ✅ |
| 27 | NVIDIA | `nvidia` | 厂商/平台 | ✅ |
| 28 | PowerShell | `powershell` | 厂商/平台 | ✅ |
| 29 | Ubuntu | `ubuntu` | 厂商/平台 | — |
| 30 | Vim | `vim` | 厂商/平台 | — |
| 31 | VMware | `vmware` | 厂商/平台 | — |
| 32 | VPN | `vpn` | 厂商/平台 | — |
| 33 | VSCode | `vscode` | 厂商/平台 | — |
| 34 | Windows | `windows` | 厂商/平台 | ✅ |
| 35 | WinScp | `winscp` | 厂商/平台 | ✅ |
| 36 | Chinese | `chinese` | 语言/格式 | ✅ |
| 37 | CPU | `cpu` | 语言/格式 | ✅ |
| 38 | English | `english` | 语言/格式 | — |
| 39 | GPU | `gpu` | 语言/格式 | ✅ |
| 40 | HDMI | `hdmi` | 语言/格式 | ✅ |
| 41 | Java | `java` | 语言/格式 | ✅ |
| 42 | JavaScript | `javascript` | 语言/格式 | ✅ |
| 43 | Markdown | `markdown` | 语言/格式 | ✅ |
| 44 | NVMe | `nvme` | 语言/格式 | ✅ |
| 45 | Python | `python` | 语言/格式 | ✅ |
| 46 | RAM | `ram` | 语言/格式 | ✅ |
| 47 | Rust | `rust` | 语言/格式 | ✅ |
| 48 | SATA | `sata` | 语言/格式 | ✅ |
| 49 | SSD | `ssd` | 语言/格式 | ✅ |
| 50 | Arduino | `arduino` | 软件/游戏 | — |
| 51 | AutoCAD | `autocad` | 软件/游戏 | — |
| 52 | Blender | `blender` | 软件/游戏 | ✅ |
| 53 | CDDA | `cdda` | 软件/游戏 | ✅ |
| 54 | Chrome | `chrome` | 软件/游戏 | ✅ |
| 55 | Clash | `clash` | 软件/游戏 | ✅ |
| 56 | Excel | `excel` | 软件/游戏 | — |
| 57 | Flash | `flash` | 软件/游戏 | ✅ |
| 58 | Geek | `geek` | 软件/游戏 | ✅ |
| 59 | Keil | `keil` | 软件/游戏 | ✅ |
| 60 | MATLAB | `matlab` | 软件/游戏 | ✅ |
| 61 | Minecraft | `minecraft` | 软件/游戏 | ✅ |
| 62 | Multisim | `multisim` | 软件/游戏 | ✅ |
| 63 | Notion | `notion` | 软件/游戏 | ✅ |
| 64 | OBS | `obs` | 软件/游戏 | — |
| 65 | Obsidian | `obsidian` | 软件/游戏 | — |
| 66 | PlatformIO | `platformio` | 软件/游戏 | ✅ |
| 67 | PowerPoint | `powerpoint` | 软件/游戏 | ✅ |
| 68 | PyCharm | `pycharm` | 软件/游戏 | ✅ |
| 69 | SolidWorks | `solidworks` | 软件/游戏 | ✅ |
| 70 | Steam | `steam` | 软件/游戏 | ✅ |
| 71 | STM32CubeIDE | `stmcubeide`、`stm32cubeide` | 软件/游戏 | ✅ |
| 72 | STM32CubeMX | `stmcubemx`、`stm32cubemx` | 软件/游戏 | ✅ |
| 73 | Undertale | `undertale` | 软件/游戏 | — |
| 74 | VisualStudio | `visualstudio` | 软件/游戏 | — |
| 75 | Word | `word` | 软件/游戏 | ✅ |
| 76 | XMind | `xmind` | 软件/游戏 | ✅ |
| 77 | blogs | `blogs` | 工具/其他 | ✅ |
| 78 | Desktop | `desktop` | 工具/其他 | ✅ |
| 79 | ffmpeg | `ffmpeg` | 工具/其他 | ✅ |
| 80 | Listary | `listary` | 工具/其他 | ✅ |
| 81 | manager | `manager` | 工具/其他 | ✅ |
| 82 | Moonlight | `moonlight` | 工具/其他 | ✅ |
| 83 | Patina | `patina` | 工具/其他 | ✅ |
| 84 | PixPin | `pixpin` | 工具/其他 | ✅ |
| 85 | README | `readme` | 工具/其他 | ✅ |
| 86 | server | `server` | 工具/其他 | ✅ |
| 87 | session | `session` | 工具/其他 | ✅ |
| 88 | stream | `stream` | 工具/其他 | ✅ |
| 89 | Sunshine | `sunshine` | 工具/其他 | ✅ |
| 90 | Wallpaper | `wallpaper` | 工具/其他 | ✅ |
| 91 | WorkSpace | `workspace` | 工具/其他 | ✅ |
| 92 | ZeroTier | `zerotier` | 工具/其他 | ✅ |

## 其它参与造句的词条（3 条）

编码中的数字用小键盘输入。

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

## 使用规则

| 规则 | 说明 |
|---|---|
| 英文编码不区分大小写 | `CHAtgpt` 与 `ChAtg` 都可得到 `ChatGPT`、`chatgpt`、`CHATGPT` |
| 编码与词条都不含空格 | `visualstudio`、`platformio` |
| 型号支持数字与部分短码 | `STM32CubeMX` 可用 `stm32cubemx` 或 `stmcubemx` |
| 主键盘数字只选词 | 1–9（0 = 第 10 个）永远是选词，不会进入编码 |
| 小键盘数字才输入数字 | `KP_0`–`KP_9` 参与拼写与造句 |
| 英文白名单可参与造句 | 如 `wozhendehenxihuanshiyongcodexzhezhonggongju` → 「我真的很喜欢使用Codex这种工具」 |

## 如何增删词条

| 想改什么 | 改哪里 | 之后做什么 |
|---|---|---|
| 英文词 / 中文短码 | [自定义词条工具](ADD-WORDS.md) | 点击「部署词条」 |
| 造句词条 | 从 `custom_phrase.txt` 重新生成 `mixed_tokens.dict.yaml` | 重新部署 |

---

返回 [README](../README.md)
