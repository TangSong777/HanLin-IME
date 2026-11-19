# 寒淋输入法 · HanLin-IME

寒淋输入法使用自研 Windows TSF 文本服务，以 librime 1.17.0 为引擎，默认启用雾凇拼音。支持数字和符号混合造句、拼音纠错与逐字学词，候选窗沿用项目的 SSF 渲染层。

| | |
|---|---|
| 版本 | `0.2.3` |
| 前端与引擎 | HanLinTip（x64、x86） / librime `1.17.0`（x64） |
| 安装包 | `HanLin-Setup-0.2.3.exe`，适用于 x64 Windows |
| 许可证 | GPL-3.0（代码）· 皮肤素材版权归原作者 |

## 目录

**按意图查找**

| 我想… | 看这里 |
|---|---|
| 装上并开始打字 | [快速开始](#快速开始) → [安装](#安装) |
| 和原版小狼毫有什么区别 | [这个项目改了什么](#这个项目改了什么) |
| 查某个按键干什么 | [使用 · 按键总表](#按键总表) |
| 输入数字、型号、英文、符号 | [功能](#功能) |
| 用计算器 | [功能 · 计算器](#计算器) |
| 加自己的词 | [配置与词条](#配置与词条) → [扩展 · 词条](#词条) |
| 换台机器 / 搬到别的输入法 | [迁移整套配置](#迁移整套配置) |
| 调整候选排序（权重） | [权重分层](#权重分层) |
| 排查「为什么不出候选」 | [已知限制](#已知限制) |
| 自己编译、跑测试 | [构建与测试](#构建与测试) |
| 了解实现原理 | [实现要点](#实现要点) |
| 了解分支模型 / 发一个版本 | [版本与发布](#版本与发布) |

**全部章节**

使用向：[快速开始](#快速开始) · [这个项目改了什么](#这个项目改了什么) · [功能](#功能) · [安装](#安装) · [使用](#使用) · [配置与词条](#配置与词条) · [已知限制](#已知限制)

开发向：[实现要点](#实现要点) · [构建与测试](#构建与测试) · [项目结构](#项目结构) · [扩展](#扩展) · [版本与发布](#版本与发布)

其它：[致谢与许可](#致谢与许可)

## 快速开始

1. 按[构建与测试](#构建与测试)生成本地安装包，运行 `HanLin-Setup-0.2.3.exe`。
2. 在 Windows 输入法列表选择「寒淋输入法」。
3. 打一段拼音验证候选。

   ```
   nihao        →  你好
   keai，sunxiaochuan258。nihao  →  可爱，孙笑川258。你好（数字用小键盘打）
   chat         →  ChatGPT
   ```

更细的安装选项见 [安装](#安装)，全部按键见 [按键总表](#按键总表)。

## 这个项目改了什么

读懂本项目，先分清哪些是上游的、哪些是本项目的：

| 层次 | 内容 |
|---|---|
| **本项目独有** | 搜狗 SSF 皮肤兼容层、小键盘原义输入与混合输入、英文白名单与分层补全、退格三档、候选窗生命周期修补、时间戳触发码禁用 |
| **高度定制** | HanLinTip 文本服务、librime 动态音节与学词、雾凇方案、构建与安装器 |
| **复用** | Weasel 候选窗渲染代码、雾凇基础词库与 Lua 脚本、Color-P 皮肤素材 |

## 功能

### 皮肤原生渲染

不做「照着配色调一调」，而是实现了一套搜狗 SSF 皮肤的解析与绘制层：直接读取 `skin.ini` 与原始 PNG，支持九宫格拉伸、逐像素 alpha 合成、皮肤自带字体与配色。状态栏关闭，中英提示以贴光标的皮肤图标呈现，2 秒后自动消失。

关掉 SSF 后端（`style/ssf_enabled: false`）即完整回落到 Weasel 原生渲染，不影响其他行为。

### 数字与符号

物理小键盘的数字与运算符**不选词、不翻页**，而是作为输入内容进入候选框，并能与拼音共存：

```
woshi        →  选「我是」
小键盘 4     →  候选框变为「我是4」
继续打 yang  →  「我是4yang」，yang 仍是可选的拼音段
```

- 主键盘顶排数字始终是选词键；**只认当前页可见的编号**（每页 5 个 → `1`–`5`，`6`–`9` 与 `0` 不参与）
- `=` 有候选时翻下一页；`-` 已翻页时翻回上一页
- 混合态按 `Enter` 提交原始拼写（字母 + 数字 + 符号）
- 无候选框时小键盘数字与标点直接上屏；有候选框时标点作为字面量参与混合输入
- 小数点使用小键盘 `KP_Decimal`，始终输出 ASCII `.`，因此 `192.168.1.1` 不受中文标点影响

#### `-` 与 `_`：仅限候选框内的 ASCII 例外

中文模式下普通标点一律输出中文形态（`,`→`，`、`.`→`。`），但**在候选框内**这两个键例外：

| 按键 | 状态 | 行为 |
|---|---|---|
| `-` | 组合中、仍在第 1 页 | 写进输入串 `-`（1 字符） |
| `-` | 组合中、已翻页 | 翻回上一页 |
| `Shift+-` | **组合中** | 写进输入串 `_`（1 字符） |
| `Shift+-` | **未组合** | 按中文标点输出 `——`（与普通标点一致） |

```
候选框内：nihao-  →  输入串 nihao-
候选框内：nihao_  →  输入串 nihao_
候选框外：Shift+-  →  ——
```

`-` 与 `_` 在组合中按字面量参与造句。独立输入时沿用中文标点规则。

### 混合造句

拼音、数字、符号和支持造句的白名单词可以组成同一个候选。

```
wozhendehenxihuanshiyongcodexzhezhonggongju  →  我真的很喜欢使用Codex这种工具
esp32bistm32haoyong                          →  ESP32比STM32好用
youyijianshiqingshishuo32meiyou26da          →  有一件事情是说32没有26大
keai，sunxiaochuan258。nihao                  →  可爱，孙笑川258。你好
```

> 上面例子里的数字都来自**小键盘**。真实打字时主键盘数字会被当作选词键，
> 只有小键盘数字才会进入输入串。

| 组成 | 状态 | 作用 |
|---|---|---|
| `speller/alphabet` 含 `0-9` | 启用 | 数字可作为拼写字符进入输入串 |
| 引擎动态数字识别 | 启用 | 连续数字按原样参与造句，保留前导零 |
| 引擎符号识别 | 启用 | 保留输入时的符号形态并参与造句 |
| `mixed_tokens.dict.yaml` | 启用 | 符合条件的英文白名单、型号和专名参与造句 |

`mixed_tokens` 从 `custom_phrase.txt` 生成。编码以 `a`、`e`、`i`、`o`、`u`、`v` 开头的英文词通过精确码和前缀补全调出。

数字的来源按按键区分：**小键盘数字参与拼写，主键盘数字只用于选词**。

### 英文词条

上游把英文作为次翻译器，打 `hello` / `world` 会出英文单词。本项目改为三层：

| 层 | 机制 | 行为 |
|---|---|---|
| 纯英文单词 | `table_translator@melt_eng` | 已移除。`hello` → 合理了哦，不出英文 |
| 中英混排词 | `table_translator@cn_en` | 保留。词库 1001 条全部含中文（`X光`、`T恤`、`红Buff`） |
| 指定名词 | `custom_phrase` + `custom_phrase_complete` | 91 个白名单词，精确命中 + 前缀补全 |

白名单为科技厂商、开发工具、语言与常用软件，按规范大小写写入，**用小写编码输入**。完整清单（编码、分类、是否参与造句）见 **[自定义字词总表](docs/CUSTOM-WORDS.md)**。

编码连续输入，例如 `visualstudio`、`platformio`。`STM32CubeMX` 可用小键盘数字输入 `stm32cubemx`，也可以输入短码 `stmcubemx`。

前缀补全拆成两个翻译器，是为了避免高权重补全挤占单字母输入：`custom_phrase` 权重 99、关闭补全，负责精确码；`custom_phrase_complete` 权重 0.9、开启补全，负责 `chat` → `ChatGPT`。

#### 大小写双份

每个纯字母的英文候选，后面会紧跟一个**全小写**版本，方便写代码或填表单时直接选：

```
github  →  GitHub | github
vscode  →  VSCode | vscode
chatgpt →  ChatGPT | chatgpt
githu   →  GitHub（补全）| github（补全）
```

由 `lua/lowercase_candidate_filter.lua` 实现，只处理**纯 ASCII 字母**：

| 候选 | 是否生成小写版本 |
|---|---|
| `GitHub`、`VSCode`、`ChatGPT` | ✅ |
| `ESP32`、`STM32`（含数字） | ❌ 型号不生成影子候选 |
| `X光`（含中文） | ❌ |
| `ffmpeg`（本来就全小写） | ❌ 不重复 |

### 计算器

`cC` 后接**小键盘**数字与运算符，结果作为候选直接上屏：

```
cC 1 2 * 3        →  12*3=36
cC 2 + 2          →  2+2=4
cC 1 . 5 * 4      →  1.5*4=6.0
cC 1 0 0 / 4      →  100/4=25.0
```

| 要点 | 说明 |
|---|---|
| 只认小键盘 | `KP_0`–`KP_9`、`KP_Add`、`KP_Subtract`、`KP_Multiply`、`KP_Divide`、`KP_Decimal` |
| 主键盘数字仍是选词键 | 计算器模式下也不改变，`cC12` 之后按 `1`/`2` 仍是在选候选 |
| 主键盘运算符不参与 | 仍按字面量/标点处理，不会进入算式 |
| 支持括号与优先级 | `12*3+4` 按先乘后加求值 |

### 退格三档

| 按键 | 行为 |
|---|---|
| `BackSpace` | 删除一个字符 |
| `Shift+BackSpace` | 删除一个完整拼音音节 |
| `Ctrl+BackSpace` | 清空整个候选框，并回收候选窗与会话；无组合时交还宿主 |

### 候选窗

| 行为 | 原因 |
|---|---|
| 切走输入法时保留面板，切回只重建窗口 | 避免每次切回都重新解码皮肤与字体（实测 220 ms，换页后可达 0.5–2 s） |
| 未取得光标锚点前不显示 | 避免窗口先在屏幕原点闪一下 |
| 切回小狼毫必定是中文 | 避免残留英文状态 |
| 光标移开即终止会话 | 避免出现「Rime 仍在组合、TSF 已无组合」的中间态，也避免切回输入框时旧候选框复现 |

## 安装

### 安装包

本地安装包随附 x64 和 x86 文本服务、寒淋后台与预编译雾凇词库。安装程序注册文本服务，并将寒淋后台加入登录启动项。另可选择附带的 Color-P 皮肤。[历史发布](https://github.com/TangSong777/HanLin-IME/releases)

静默安装（需在已提权终端中运行）：

```powershell
HanLin-Setup-0.2.3.exe /SILENT /NORESTART /SUPPRESSMSGBOXES
```

### 安装布局

| 内容 | 位置 |
|---|---|
| 输入法本体与共享词库 | `C:\Program Files\HanLin` |
| 皮肤 | `C:\ProgramData\HanLin\Skin` |
| Rime 用户目录 | `%APPDATA%\HanLin` |
| 安装目录登记 | `HKLM\SOFTWARE\HanLin\InstallRoot`（x64 和 x86 注册表视图） |

寒淋与旧的小狼毫输入法各自使用独立目录。小狼毫旧用户目录里的个人词库留在原位。寒淋用户目录可通过 `HKCU\Software\HanLin\UserDataDir` 指定，修改配置后可在后台退出时运行 `HanLinHost.exe --deploy`。

卸载从 Windows「设置 → 应用」开始，注销寒淋文本服务并保留个人词库。

### 小狼毫兼容配置

已有小狼毫、只需更换皮肤与配置时：

1. 复制 `assets/Color-P/` 到固定目录，例如 `C:\ProgramData\ColorPWeasel\Color-P`
2. 复制 `rime-config/` 下的文件到 Rime 用户目录（托盘菜单「用户文件夹」）
3. 复制 `rime-config/lua/keypad_input.lua` 到用户目录的 `lua\` 子目录
4. 确认 `weasel.custom.yaml` 的 `style/ssf_skin` 指向第 1 步目录
5. 托盘菜单 →「重新部署」

> 词库数据（`cn_dicts/`、`en_dicts/`）与 `rime_ice.dict.yaml` 来自 rime-ice，随安装包分发，不在 `rime-config/` 内。手动部署时需要自行准备；若要大字表，需在 `import_tables` 中启用 `cn_dicts/41448`。

## 使用

### 按键总表

| 操作 | 效果 |
|---|---|
| `Shift`（输入框内短按） | 切换中英文（引擎内置行为） |
| 主键盘 `1`–`5`（当前页可见编号） | 选词；`6`–`9`、`0` 不参与选词 |
| `=` 有候选时 / `-` 已翻页时 | 下一页 / 上一页 |
| `-` 仍在第 1 页 | 写进输入串 `-`（不参与组词） |
| `Shift+-`（组合中） | 写进输入串 `_`（不参与组词） |
| `Shift+-`（未组合） | 输出中文标点 `——` |
| `Enter` | 提交原始拼写（混合态为字母 + 数字 + 符号） |
| `Shift+Enter` | 与 `Enter` 相同，上屏原始输入（不含 Rime 自动插入的 `'` 分隔符） |
| `BackSpace` / `Shift+BackSpace` / `Ctrl+BackSpace` | 删字符 / 删音节 / 清空候选框 |
| 小键盘 `KP_0`–`KP_9` | 输入数字，参与拼写与造句 |
| 小键盘 `+ - * /` | 按原义进入输入串 |
| 小键盘 `KP_Decimal` | 输出 ASCII `.` |
| `cC` + 小键盘数字/运算符 | 计算器，结果作为候选 |
| 英文白名单（小写） | `chatgpt`、`cpu`、`vscode`；支持前缀 `chat` → `ChatGPT` |
| 语言栏左键 / 右键 | 状态指示 / 打开菜单 |

英文词条**大小写都能出**：每条英文词条同时注册了小写编码和标准大小写编码，所以 `Desk` / `GitH` / `ChatG` / `Mark` 都能补全出 `Desktop` / `GitHub` / `ChatGPT` / `Markdown`。全大写输入（`DNA`）仍走方案的「大写字母造词」通道（`zuogeDNAjiance` → 做个DNA检测）。

### 颠倒输入识别

打反了声母韵母的顺序时，**输入框保持原样**，候选按正确音节给出：

```
uq   →  去 | 取 | 区        （识别为 qu）
uj   →  巨 | 举 | 居        （识别为 ju）
vl   →  绿 | 律 | 率        （识别为 lv）
ib   →  比 | 必 | 毕        （识别为 bi）
```

覆盖 **40 种**组合，全部是 `i` / `u` / `v` 开头的两字母颠倒：

```
ia ib ic id ie ij il im in ip iq ir is it ix iy iz
ub uc ud uf ug uh uj uk ul um un uo up uq ur us ut uw ux uy uz
vl vn
```

**为什么只有 40 种**：这些变体既不是合法音节、也不是任何合法输入的前缀，
所以派生拼写不会污染分词器 —— `kaol`（考虑）、`zia`（子啊）、`zai`（在）全部不受影响。

`a` / `e` / `o` 开头的 34 种没做：它们本身是独立音节（啊 / 饿 / 哦），
`ab` 会被切成 `a` + `b`，派生后会干扰正常输入。

> 输入框里不会把 `uq` 改成 `qu`，只是候选按 `qu` 给 —— 继续输入也不受影响。

### 输入示例

```
nihao                        →  你好
nihao-                       →  输入串 nihao-（- 是字面量，不参与组词）
nihao_                       →  输入串 nihao_（_ 是字面量，不参与组词）
esp32bistm32haoyong          →  ESP32比STM32好用（数字用小键盘）
chat                         →  ChatGPT（前缀补全）
Desk                         →  Desktop（大写也能补全）
markdownqishiyejiunayang     →  Markdown其实也就那样
cC 1 0 0 / 4                 →  100/4=25.0（小键盘）
zuogeDNAjiance               →  做个DNA检测（大写造词）
nihaowoshiyanghanlin         →  Enter 或 Shift+Enter 都上屏 nihaowoshiyanghanlin（不含 ' 分隔符）
```

## 配置与词条

### 配置职责

| 文件 | 职责 |
|---|---|
| `weasel.custom.yaml` | 启用 SSF 后端、配色、布局、字体 |
| `default.custom.yaml` | 方案列表与全局快捷键 |
| `rime_ice.custom.yaml` | 处理器/翻译器覆盖、按键绑定、英文分层、时间戳禁用、`speller/alphabet` |
| `custom_phrase.txt` | 英文白名单与中文短码（`词条<Tab>编码`） |
| `mixed_tokens.dict.yaml` | 可参与造句的词条，由白名单脚本生成 |
| `rime_ice.dict.yaml` | 词库挂载表，包含大字表和 `mixed_tokens` |
| `lua/keypad_input.lua` | 小键盘与数字混合输入处理器 |
| `lua/dedupe_filter.lua` | 合并「文本与注释都相同」的重复候选 |
| `lua/lowercase_candidate_filter.lua` | 英文候选后追加全小写版本 |
| `lua/short_prefix_filter.lua` | 抑制单字母前缀补全误触 |

### 白名单与词条

修改 `custom_phrase.txt` 后，重新生成 `mixed_tokens.dict.yaml`，词条才能参与造句。打包时会检查生成结果。

```powershell
pwsh -File .\src\build-support\gen-mixed-tokens.ps1 `
    -CustomPhrase .\rime-config\custom_phrase.txt -Out .\rime-config\mixed_tokens.dict.yaml
```

完整词条清单见 [自定义字词总表](docs/CUSTOM-WORDS.md)（由 `gen-word-table.ps1` 生成，勿手改）。

### 权重分层

候选的先后不是靠调一个开关，而是**四处权重叠加**的结果：

| 来源 | 权重 | 写在哪 | 作用 |
|---|---|---|---|
| 精确词条 | `initial_quality: 99` | `rime_ice.custom.yaml` → `custom_phrase` | 精确码（`chatgpt`）稳定首位 |
| 前缀补全 | `initial_quality: 0.9` | `rime_ice.custom.yaml` → `custom_phrase_complete` | 补全（`chat`）能选到，但排在拼音之后 |
| 造句词条 | 第三列 `1000` / `100000` | `mixed_tokens.dict.yaml` | 一般词条 1000，ESP32、STM32 和专名 100000 |
| 词库整体 | 列表顺序 | `rime_ice.dict.yaml` → `import_tables` | 越靠前权重越高 |

拼音译文器的默认权重约 `1.2`，高于补全的 `0.9`，所以单字母输入仍是中文优先：

```
g  →  「个」等中文（不是 Gemini / GitHub）
chat → ChatGPT 出现在中文之后，但可以选到
```

调权重的四个常用动作：

| 想要 | 改哪里 | 说明 |
|---|---|---|
| 英文精确码压过中文 | `custom_phrase/initial_quality`（99 → 更高） | 单字母输入会开始出现英文，慎调 |
| 前缀补全更靠前 | `custom_phrase_complete/initial_quality`（0.9 → 1.1~1.3） | 超过 1.2 后补全会挤到中文前 |
| 某个词造句时更靠前 | `mixed_tokens.dict.yaml` 该行第三列 | 改完重新部署 |
| 整个词库优先级 | `rime_ice.dict.yaml` 的 `import_tables` 顺序 | 越靠前越优先 |

### 迁移整套配置

可以把小狼毫用户目录或其备份里的自定义配置、短语和学习词频迁入寒淋。之前导入小狼毫的搜狗短语随 `sogou_phrases.txt` 和对应的方案配置一起迁移。

先退出正在使用来源目录的小狼毫，再运行随寒淋安装的脚本。这里以 `D:\RimeUser` 为例。

```powershell
$install = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\HanLin' -Name InstallRoot).InstallRoot
Start-Process -FilePath (Join-Path $install 'HanLinHost.exe') -ArgumentList '--stop' -Wait
pwsh -NoProfile -File (Join-Path $install 'Migrate-HanLinUserData.ps1') -Source 'D:\RimeUser'
```

省略 `-Source` 时，脚本读取小狼毫设置中的用户目录。可用 `-Destination` 指定一个新目录，或加 `-NoActivate` 仅生成迁移副本。寒淋已有用户目录时，默认在旁边建立一个新目录。

脚本保留本地配置、词表和用户词典的优先级，并生成新的部署缓存与安装标识。来源目录和原寒淋用户目录均可继续使用。脚本打印原寒淋用户目录。要切回时，先退出寒淋后端，再将 `HKCU:\Software\HanLin` 的 `UserDataDir` 改回该路径。


## 已知限制

以下为有意保留的行为，不是待修缺陷：

| 现象 | 原因 |
|---|---|
| 输入框外无法切换中英文 | 无 TSF 会话；本项目不含键鼠钩子 |
| 裸 `Shift` 仅在输入框内短按生效 | librime `ascii_composer` 的既有行为，本项目未改动引擎 |
| 组合中输入 `-` 或 `_` | 符号以原样参与造句。候选翻页后按 `-` 返回上一页 |
| `Tab` 在拼音组合中用于移动光标 | 有意映射 |
| 方向键在输入串两端环绕 | librime 1.13.1 限制（`_no_loop` 需 ≥ 1.16） |
| 中文模式下主键盘 `.` 输出 `。` | 标点形态由中英模式决定；小数点请用小键盘 `KP_Decimal` |
| 仅白名单内的英文词能出英文 | 有意设计，见[英文词条](#英文词条) |
| 英文大小写都能补全 | 小写编码 + 标准大小写编码各一份；全大写仍走「大写字母造词」 |
| 以词定字（`[` `]`）不可用 | 未保留 `select_character`，`[` `]` 归标点 |
| 计算器只认小键盘 | 主键盘数字始终是选词键；只在 `cC` 模式下放行小键盘数字与运算符 |
| 不做错音纠正（平翘舌、前后鼻音） | 模糊音会让 `zong` 也出「中/种」、`sen` 也出「神/深」，干扰大于收益；按标准拼音输入反而更顺手 |
| 元音开头的英文词不参与造句（Ubuntu / Intel / VSCode…） | `u` 是 `ubuntu` 的合法首音节，参与造句会让误触的 `u` 把整句接成「Ubuntu去哪里」；**精确码与前缀补全不受影响** |
| `Enter` / `Shift+Enter` 上屏时去掉 Rime 自动插入的 `'` | 手动输入的 `'`（如 `xi'an`）会保留，因为那属于原始输入 |

### 候选位占用

部分输入会占据候选首位，属方案触发码设计：

| 输入 | 产出 | 占用 |
|---|---|---|
| `sj` / `xq` / `rq` | 时间 / 星期 / 日期 | 1–5 |
| `dt` / `rqzh` / `rqen` | ISO 8601 / 中文日期 / 英文日期 | 1–3 |
| `nl` / `uuid` | 农历 / UUID | 1 |
| `d` / `m` / `hm` | 的 / 吗 / 噷（`pin_cand_filter` 置顶） | 1 |
| `u` / `i` / `ig` / `uu` / `uuuu` / `sig` / `umu` | 短码词条 | 1 |
| `ts` | 时间戳 | 已关闭 |

`pin_cand_filter` 中 `hm → 后面` 一条实际无效（首位仍是「噷」），属上游配置与实现的偏差。

## 实现要点

### SSF 兼容层

位于 `src/WeaselUI/ssf/`。解析 `skin.ini` 的布局与配色，按九宫格规则拉伸位图，逐像素合成 alpha，并把候选、编码、提示绘制到面板。皮肤素材原样保留在 `assets/Color-P/`（27 个文件）。

### TSF 前端

`src/WeaselTSF/` 负责按键、焦点、候选列表与语言栏。本项目在此层加入了退格拦截、候选窗生命周期管理与 SSF 渲染接入。**代码中不含任何键鼠钩子** —— 早期为「输入框外切换中英文」实现的全局钩子实测从未生效，已连同状态机一并移除。

### 方案覆盖

`rime_ice.custom.yaml` 完整覆盖处理器与翻译器顺序：

```yaml
engine/processors:
  - lua_processor@*keypad_input      # 本项目：拦主键盘数字、放行小键盘
  - ascii_composer / recognizer / key_binder / speller
  - punctuator / selector / navigator / express_editor

engine/translators:
  # 去掉 melt_eng，加入 custom_phrase_complete
```

其余覆盖包括 `key_binder` 绑定、`date_translator/timestamp` 置空、`custom_phrase` 双翻译器分层。

覆盖处理器列表时未保留上游的 `lua_processor@*select_character`，因此以词定字不可用、`[` `]` 归标点 —— 这是有意取舍。

### 词库范围

`rime_ice.dict.yaml` 的 `import_tables` 中启用大字表：

```yaml
- cn_dicts/8105     # 8,757 条
- cn_dicts/41448    # 本项目启用：46,019 条（上游默认注释）
```

实测对照（同一用户目录只切换该开关）：`龘 靐 麤 鱻 猋` 关闭时全部缺失，启用后全部命中；`鑫` 两种配置都有。

### 时间戳触发码

rime-ice 默认把 `ts` 作为 Unix 时间戳触发码，而它同时是「唐宋」「同时」的简拼。本项目将 `date_translator/timestamp` 置为**空字符串**关闭该触发码 —— 不能只删除该项，脚本取不到值会回退为 `ts`。

### 构建与安装器

- `weasel.props` 由脚本自动生成：`vswhere` 探测 Visual Studio，按目录名探测 MSVC 与 Windows SDK
- 所有脚本经 `build-support/Find-Toolchain.ps1` 探测工具链，不写死本机路径
- Inno Setup 安装寒淋自己的 x64 和 x86 文本服务、引擎进程与共享数据

## 构建与测试

### 构建

使用 PowerShell 7、Visual Studio 2022、Windows SDK、Boost 1.82、CMake 和 Ninja。构建寒淋前端需要 x64 的 `WeaselUI.lib` 和寒淋定制的 librime 1.17.0 `rime.dll`。全新克隆请从同系列已安装的寒淋中取出定制 DLL，或用本项目引擎源码及 librime-lua 构建后通过 `-EngineDll` 提供。渲染库的编译依赖见 [构建小狼毫](docs/build-weasel.md)。

```powershell
git clone https://github.com/TangSong777/HanLin-IME.git
cd HanLin-IME
pwsh -NoProfile -File .\src\build-weasel.ps1
pwsh -NoProfile -File .\src\build-hanlin.ps1 -EngineDll 'C:\Program Files\HanLin\rime.dll'
```

寒淋产物位于 `src\output\hanlin-tip-x64` 和 `src\output\hanlin-tip-x86`。x64 目录也包含 `HanLinHost.exe` 与 `rime.dll`。`src\output\HanLinBuild.json` 记录前端源码指纹、版本和产物哈希。

`weasel.props` 由脚本自动生成。环境特殊时可用下列变量覆盖，并加 `-RegenerateProps` 重新生成：

| 变量 | 含义 | 默认 |
|---|---|---|
| `WEASEL_BOOST_ROOT` | Boost 源码目录 | `D:\boost182` |
| `WEASEL_VS_ROOT` | Visual Studio 目录 | `vswhere` 探测 |
| `WEASEL_SDK_ROOT` | Windows SDK 根 | `D:\Windows Kits\10` |
| `WEASEL_MSVC_ROOT` | MSVC 工具集 | 目录名探测 |
| `WEASEL_SDK_VER` | SDK 版本 | 目录名探测 |

### 打包安装器

```powershell
# 需要 Inno Setup 6.3 或更新版本
pwsh -File .\src\installer\build-installer.ps1 -SharedDataDir <Rime共享数据目录> -RimeSourceDir <雾凇拼音源目录>
```

打包程序先校验构建记录和白名单，再从共享数据与雾凇源目录预编译随包词库。编译时需退出寒淋后端。用 `-BuildRoot` 指定前端构建目录，用 `-OutputDir` 指定安装包输出目录。默认输出到 `src\installer\output`，文件名由版本号生成。

寒淋启动时用随包数据指纹检查个人编译缓存。数据变化时按当前用户配置重编缓存，成功后替换。重编失败时沿用旧缓存，下次启动重试。全新用户目录直接使用安装包中的预编译词库。

### 测试

构建时加 `-BuildRegression` 可以在 x64 和 x86 输出目录生成 `HanLinFrontendRegression.exe`。用同批构建的 `HanLinHost.exe` 启动隔离后端，`--shared-data` 指向安装包的 `data` 目录，`--user-data` 指向新建的临时目录。执行探针时传入同架构的 `HanLinTip.dll`。它会用合成文本测试写锁失败、只读输入框、候选操作和重连。

全部测试可独立运行，无需管理员权限：

```powershell
# SSF 皮肤解析与布局（237 项）
pwsh -NoProfile -File .\src\build-ssf-tests.ps1 -Run

# 小键盘与数字混合输入（100 项，需要 Python + lupa）
python .\src\test\TestSsfSkin\test_keypad.py .\rime-config\lua\keypad_input.lua

# 候选窗生命周期与输入法切换（11 项）
pwsh -NoProfile -File .\src\build-panel-bench.ps1 -Run

# 宿主兼容与 TSF 身份判定
cmd /c .\src\build-support\test-host.cmd && .\src\dist\host-regression.exe

# 安装器逻辑（配置改写、边界输入、临时文件提取）
pwsh -File .\src\installer\harness\run-harness.ps1
```

### 字典覆盖度探针

`.table.bin` 是 Marisa 压缩结构，明文搜索不可靠，判断某字能否打出必须走真实输入链路：

```powershell
# 构建
cmd /c .\src\build-support\build-dict-probe.cmd

# 内置 6 项断言（龘 靐 麤 鱻 猋 + 对照 鑫）
.\src\dist\dict-probe.exe <用户目录副本> <安装目录\data>

# 查询任意输入的候选
.\src\dist\dict-probe.exe <用户目录副本> <安装目录\data> --query chat --limit 5

# 在指定目录重新部署后测试
.\src\dist\dict-probe.exe <用户目录副本> <安装目录\data> --deploy
```

探针操作**用户目录副本**（复制时排除 `*.userdb` 与 `sync`，避免与运行中的服务抢锁）。

## 项目结构

仓库直接位于小狼毫安装目录下，源码在同级 `src/`：

```
D:\Rime\                     # 仓库根 = 小狼毫安装目录
├── src/                     # 定制版 Weasel 源码（上游 0.17.4 + 本项目改动）
│   ├── WeaselTSF/           #   TSF 输入服务：按键、焦点、候选列表、语言栏
│   ├── WeaselUI/            #   候选窗渲染
│   │   └── ssf/             #   搜狗 SSF 皮肤兼容层
│   ├── WeaselServer/        #   服务进程与托盘
│   ├── WeaselDeployer/      #   部署程序（重新部署 / 配置）
│   ├── WeaselSetup/         #   安装程序
│   ├── RimeWithWeasel/      #   引擎与 UI 的粘合
│   ├── include/             #   公共头
│   ├── build-libs/          #   librime 导入库
│   ├── build-support/       #   构建/测试辅助脚本
│   ├── installer/           #   安装包构建（Inno Setup + payload）
│   └── test/TestSsfSkin/    #   回归测试
├── assets/Color-P/          # 皮肤原始资源（skin.ini + 27 个图片）
├── rime-config/             # 运行时配置与词条
├── docs/                    # 文档（CUSTOM-WORDS.md 等）
├── reference/               # 第三方快照，仅作查阅
├── weasel-0.17.4/           # 已安装的小狼毫本体（不入库）
├── CHANGELOG.md             # 版本判定与发布记录
├── PENDING-CHANGES.md       # 待发布改动账本
└── README.md
```

`weasel-0.17.4/` 被 `.gitignore` 排除；`src/build-libs/` 的导入库约 0.3 MB/架构，随仓库提供以便直接编译。`reference/` 下为第三方项目快照，版权归各自作者。

## 扩展

扩展点都在配置与数据层，无需改动 C++。

### 词条

| 目标 | 修改 | 之后 |
|---|---|---|
| 英文词 / 中文短码 | `rime-config/custom_phrase.txt`（`词条<Tab>编码`） | 重新部署 |
| 更新参与造句的白名单词 | 从 `custom_phrase.txt` 重新生成 `mixed_tokens.dict.yaml` | 重新部署 |
| 更新词表文档 | 重跑 `gen-word-table.ps1` | 提交 `docs/CUSTOM-WORDS.md` |

### 词库

编辑 `rime_ice.dict.yaml` 的 `import_tables`，越靠前权重越高：

```yaml
import_tables:
  - mixed_tokens      # 参与造句的英文、型号和专名
  - cn_dicts/8105       # 字表
  - cn_dicts/41448      # 大字表
  - cn_dicts/base       # 基础词库
```

### 处理器与翻译器

1. Lua 脚本放入 `rime-config/lua/`
2. 在 `rime_ice.custom.yaml` 挂载：`lua_processor@*my_processor` / `lua_translator@*my_translator`
3. 处理器顺序即优先级，要拦截按键必须排在 `speller` / `selector` 之前

`keypad_input.lua` 是最完整的示例。

### 皮肤

| 内容 | 位置 |
|---|---|
| 原始素材 | `assets/Color-P/` |
| 启用与参数 | `weasel.custom.yaml` 的 `style/ssf_skin`、`ssf_enabled` |
| 已部署副本 | `C:\ProgramData\ColorPWeasel\Color-P` |

改素材后重新部署即可，无需重编译。

### 可复现约定

1. 脚本不写死机器路径，位置一律探测或由参数传入
2. 同步版本号到 `HanLin.iss` 和 `HanLinVersion.rc`。打包脚本读取 `HanLin.iss`。
3. 发布前重跑生成脚本，保证文档与数据一致

## 版本与发布

寒淋版本号与 librime、雾凇拼音和候选渲染代码的上游版本各自维护。

### 分支模型

仓库用两个分支，把**开发中的代码**和**稳定版**分开：

```
snapshot ──► master ──► tag + Release
（工作分支）  （只做合并）    （发版产物）
```

| 分支 | 角色 | 更新方式 |
|---|---|---|
| `snapshot` | 跟随本地的工作分支，日常改动都提交到这里 | 每完成一件事提交一次，自动推送 |
| `master` | 稳定版，内容永远等于某个 `snapshot` 提交 | 只在发版时 `git merge --ff-only snapshot` |

**`master` 永不直接提交**，只接受快进合并。默认分支保持 `master`，所以访客 clone 或浏览时看到的永远是稳定版；想看最新进展请切到 `snapshot`。

**提交 ≠ 发布**：日常提交只进 `snapshot`，只有「更新版本」才会动 `master`、改版本号、打 tag、发 Release。

### 版本规则

采用 `MAJOR.MINOR.PATCH`，按实际变更范围判定：

| 位 | 条件 | 示例 |
|---|---|---|
| `MAJOR` | 不兼容的大改 | 尚未发生，留给 `1.0.0` |
| `MINOR` | 成体系的新能力（新增输入方式、整套配置、大范围重构） | `0.0.6 → 0.1.0` |
| `PATCH` | 修 bug、调参数、换词库、**局部小功能** | `0.0.1 → 0.0.6`、`0.1.3 → 0.1.4` |

混合变更取最高位。完整记录见 [CHANGELOG.md](CHANGELOG.md)。

### 版本历史

| 版本 | 说明 |
|---|---|
| **0.1.6** | 当前稳定版：颠倒输入识别（40 种）、回退模糊音纠错 |
| 0.1.5 | 错音纠正（模糊音，0.1.6 已回退）、大小写双份、Shift+Enter 修复 |
| 0.1.4 | 白名单 80 词、修复计算器、README 重构 |
| 0.1.3 | 候选去重、白名单 77 词、修复候选框幽灵复现 |
| 0.1.2 | 候选框内 `-`/`_` 字面量输入、数字选词只认当前页 |
| 0.1.1 | 文档与发布流程更新（输入功能同 0.1.0） |
| 0.1.0 | 混合造句、英文白名单 72 词、大字表 41448、时间戳禁用、退格三档 |
| 0.0.6 | 移除全部键鼠钩子，仓库自包含 |
| 0.0.1 – 0.0.5 | 历史版本 |

### 发布流程

```powershell
# ── 日常：提交到 snapshot ──
git add -A && git commit -m "<一句话>" && git push

# ── 发版：全程在 snapshot 上 ──
# 1. 读 PENDING-CHANGES.md 汇总，写入 CHANGELOG.md
# 2. 更新 src\installer\HanLin.iss 的 MyAppVersion
# 3. 清空账本，提交推送
git add -A && git commit -m "发布 <版本>" && git push
# 4. 重建前端、打包并核对（版本资源、载荷哈希）
pwsh -File .\src\build-hanlin.ps1
pwsh -File .\src\installer\build-installer.ps1
# 5. 合并到 master（只允许快进）
git checkout master && git merge --ff-only snapshot && git push
# 6. 在 master 上打标签并发布
git tag -a <版本> -m "HanLin-IME <版本>" && git push origin <版本>
gh release create <版本> .\src\installer\output\HanLin-Setup-<版本>.exe `
    .\src\installer\output\SHA256SUMS.txt `
    --title "寒淋输入法 <版本>" --notes-file RELEASE-NOTES.md --latest
# 7. 切回 snapshot 继续工作
git checkout snapshot
```

安装包文件名为 `HanLin-Setup-<版本>.exe`，发布时附带 `SHA256SUMS.txt`。

> 打包与验证在**合并之前**完成。若打包失败，`master` 与 tag 都不会被污染 —— 稳定版永远指向一个真的能出包的提交。

## 致谢与许可

- 输入引擎：[librime](https://github.com/rime/librime)（BSD-3-Clause）
- 候选窗渲染代码：[Weasel 小狼毫](https://github.com/rime/weasel)（GPL-3.0）
- 拼音方案：[rime-ice 雾凇拼音](https://github.com/iDvel/rime-ice)（MIT）
- 皮肤原作：搜狗「竹子 Color-P」，作者**墨竹**；本仓库仅做小狼毫层面的移植

本项目为个人定制作品，基于上述开源项目与皮肤资源二次开发，不是任何上游项目的官方版本，也未获得搜狗授权或背书。源代码遵循上游 Weasel 的 GPL-3.0；皮肤图片资源版权归原作者，仅作个人学习与自用移植。

若需要官方支持、适合长期使用的小狼毫，请使用 [上游 Weasel](https://github.com/rime/weasel)。
