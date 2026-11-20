# 生成「自定义字词总表」(docs/CUSTOM-WORDS.md)。
#
# 数据来源（唯一事实来源，不手工抄写）：
#   custom_phrase.txt        —— 英文白名单 + 中文短码
#   mixed_tokens.dict.yaml   —— 参与造句的英文词条
#
# 用法：
#   pwsh -File .\src\build-support\gen-word-table.ps1 `
#       -CustomPhrase <用户目录>\custom_phrase.txt `
#       -MixedTokens  <用户目录>\mixed_tokens.dict.yaml `
#       -Out          <仓库>\docs\CUSTOM-WORDS.md
param(
  [string]$CustomPhrase = (Join-Path $env:APPDATA 'Rime\custom_phrase.txt'),
  [string]$MixedTokens  = (Join-Path $env:APPDATA 'Rime\mixed_tokens.dict.yaml'),
  [string]$Out          = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) 'docs\CUSTOM-WORDS.md')
)

$ErrorActionPreference = 'Stop'
$enc = New-Object Text.UTF8Encoding($false)
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'tools\WordManagerData.ps1')

foreach ($f in @($CustomPhrase, $MixedTokens)) {
  if (-not (Test-Path $f)) { throw "找不到 $f" }
}

# 分类表：保持与 README 的白名单分组一致；未命中的词条归入「其他」
$categories = [ordered]@{
  'AI/模型'  = @('ChatGPT','Gemini','DeepSeek','OpenCode','Anthropic','OpenAI','Claude',
                 'Copilot','Qwen','Kimi','Grok','prompt','Codex')
  '厂商/平台' = @('MicroSoft','Google','Windows','Ubuntu','Linux','Debian','Docker','Vim',
                 'VMware','VPN','NVIDIA','Intel','AMD','GitHub','GitLab','VSCode',
                 'PowerShell','WinScp','Cloudflare')
  '语言/格式' = @('Python','Java','Rust','JavaScript','Markdown','English','Chinese','HDMI','SSD','SATA','CPU','GPU','RAM','NVMe')
  '软件/游戏' = @('Minecraft','Undertale','CDDA','Clash','MATLAB','PyCharm','VisualStudio',
                 'Multisim','SolidWorks','AutoCAD','STM32CubeMX','STM32CubeIDE','Keil',
                 'Arduino','PlatformIO','Blender','Chrome','Geek','Flash','OBS',
                 'PowerPoint','Excel','Word','XMind','Obsidian','Notion','Steam')
  '工具/其他' = @('Listary','PixPin','ZeroTier','Wallpaper','Desktop','Patina','README','WorkSpace','Sunshine','Moonlight','ffmpeg',
                 'manager','session','server','blogs','stream')
}

function Get-Category([string]$word) {
  foreach ($name in $categories.Keys) {
    if ($categories[$name] -contains $word) { return $name }
  }
  return '其他'
}

# ---- 读 custom_phrase.txt ----
$rows = @(Read-WordEntries $CustomPhrase)
$english = @($rows | Where-Object { $_.Word -notmatch '[^\x00-\x7F]' })
$chinese = @($rows | Where-Object { $_.Word -match '[^\x00-\x7F]' })
$englishGroups = @($english | Group-Object Word -CaseSensitive)

# ---- 读 mixed_tokens.dict.yaml（词条 / 编码 / 权重）----
$tokens = @()
if (Test-Path $MixedTokens) {
  foreach ($line in ([IO.File]::ReadAllLines($MixedTokens, $enc) | Where-Object { $_ -match "`t" -and $_ -notmatch '^#' })) {
    $p = $line -split "`t"
    $tokens += [pscustomobject]@{ Word = $p[0]; Code = $p[1]; Weight = $(if ($p.Count -gt 2) { $p[2] } else { '' }) }
  }
}
$tokenPairs = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($t in $tokens) { [void]$tokenPairs.Add("$($t.Word)`t$($t.Code)") }
$sentenceCount = @($english | Where-Object { $tokenPairs.Contains("$($_.Word)`t$($_.Code)") }).Count

$now = Get-Date -Format 'yyyy-MM-dd HH:mm'
$sb = New-Object System.Text.StringBuilder

function Add-Line([string]$s = '') { [void]$sb.AppendLine($s) }

Add-Line '# 自定义字词总表'
Add-Line ''
Add-Line '> 本文件由 `src/build-support/gen-word-table.ps1` 从实际词条文件生成，**请勿手工编辑**。'
Add-Line '>'
Add-Line "> 生成时间：$now"
Add-Line ''
Add-Line '## 概览'
Add-Line ''
Add-Line '> 造句词表 `mixed_tokens` 由 `custom_phrase.txt` 和额外词条生成。'
Add-Line ''
Add-Line '| 类别 | 数量 | 输入方式 | 参与造句 |'
Add-Line '|---|---:|---|---|'
Add-Line "| 英文白名单 | $($englishGroups.Count) 个词 / $($english.Count) 个编码 | 小写编码 | $sentenceCount 个编码 |"
Add-Line "| 中文短码 | $($chinese.Count) | 单/双字母 | — |"
Add-Line '| 动态数字 | 引擎识别 | 小键盘数字 | ✅ |'
Add-Line "| **合计自维护词条** | $($english.Count + $chinese.Count) | — | — |"
Add-Line ''

# ---- 英文白名单 ----
Add-Line "## 英文白名单（$($englishGroups.Count) 个词 / $($english.Count) 个编码）"
Add-Line ''
Add-Line '按分类排列。词表保存标准词与小写编码，精确匹配和前缀补全都接受混合大小写。'
Add-Line ''
Add-Line '| # | 词条 | 编码 | 分类 | 参与造句 |'
Add-Line '|---:|---|---|---|:---:|'
$i = 0
$ordered = $englishGroups | Sort-Object @{ Expression = { $categories.Keys.IndexOf((Get-Category $_.Name)) } }, Name
foreach ($group in $ordered) {
  $i++
  $codes = (@($group.Group | ForEach-Object { '`' + $_.Code + '`' }) -join '、')
  $sentence = @($group.Group | Where-Object { $tokenPairs.Contains("$($_.Word)`t$($_.Code)") })
  $join = if ($sentence.Count -eq $group.Count) { '✅' } elseif ($sentence.Count) {
    (@($sentence | ForEach-Object { '`' + $_.Code + '`' }) -join '、')
  } else { '—' }
  Add-Line "| $i | $($group.Name) | $codes | $(Get-Category $group.Name) | $join |"
}
Add-Line ''

# ---- 型号词条（有编码但不在 custom_phrase 里的）----
$extraTokens = @($tokens | Where-Object {
  $tok = $_
  -not ($english | Where-Object { $_.Word -eq $tok.Word -and $_.Code -eq $tok.Code })
})
if ($extraTokens.Count) {
  Add-Line "## 其它参与造句的词条（$($extraTokens.Count) 条）"
  Add-Line ''
  Add-Line '编码中的数字用小键盘输入。'
  Add-Line ''
  Add-Line '| # | 词条 | 编码 | 权重 |'
  Add-Line '|---:|---|---|---:|'
  $j = 0
  foreach ($t in $extraTokens) {
    $j++
    Add-Line "| $j | $($t.Word) | ``$($t.Code)`` | $($t.Weight) |"
  }
  Add-Line ''
}

# ---- 中文短码（按编码分组）----
$groups = [ordered]@{}
foreach ($c in $chinese) {
  if (-not $groups.Contains($c.Code)) { $groups[$c.Code] = @() }
  $groups[$c.Code] += $c.Word
}
Add-Line "## 中文短码（$($chinese.Count) 条 / $($groups.Count) 个编码）"
Add-Line ''
Add-Line '同一编码下按候选顺序排列（第一条优先级最高）。'
Add-Line ''
Add-Line '| # | 编码 | 词条（按候选顺序） | 说明 |'
Add-Line '|---:|---|---|---|'
$k = 0
foreach ($code in $groups.Keys) {
  $k++
  $words = ($groups[$code] | ForEach-Object { "「$_」" }) -join ' '
  $note = switch ($code) {
    'hm'  { '语气词' }
    'm'   { '语气词' }
    'u'   { '高频单字，u+数字 可调序' }
    'i'   { '高频单字，i+数字 可调序' }
    'uuuu'{ '网络梗「又双叒叕」' }
    default { '常用简拼' }
  }
  Add-Line "| $k | ``$code`` | $words | $note |"
}
Add-Line ''

# ---- 使用规则 ----
Add-Line '## 使用规则'
Add-Line ''
Add-Line '| 规则 | 说明 |'
Add-Line '|---|---|'
Add-Line '| 英文编码不区分大小写 | `CHAtgpt` 与 `ChAtg` 都可得到 `ChatGPT`、`chatgpt`、`CHATGPT` |'
Add-Line '| 编码与词条都不含空格 | `visualstudio`、`platformio` |'
Add-Line '| 型号支持数字与部分短码 | `STM32CubeMX` 可用 `stm32cubemx` 或 `stmcubemx` |'
Add-Line '| 主键盘数字只选词 | 1–9（0 = 第 10 个）永远是选词，不会进入编码 |'
Add-Line '| 小键盘数字才输入数字 | `KP_0`–`KP_9` 参与拼写与造句 |'
Add-Line '| 英文白名单可参与造句 | 如 `wozhendehenxihuanshiyongcodexzhezhonggongju` → 「我真的很喜欢使用Codex这种工具」 |'
Add-Line ''

# ---- 维护方式 ----
Add-Line '## 如何增删词条'
Add-Line ''
Add-Line '| 想改什么 | 改哪里 | 之后做什么 |'
Add-Line '|---|---|---|'
Add-Line '| 英文词 / 中文短码 | [自定义词条工具](ADD-WORDS.md) | 点击「部署词条」 |'
Add-Line '| 造句词条 | 从 `custom_phrase.txt` 重新生成 `mixed_tokens.dict.yaml` | 重新部署 |'
Add-Line ''
Add-Line '---'
Add-Line ''
Add-Line '返回 [README](../README.md)'

New-Item -ItemType Directory -Force -Path (Split-Path $Out -Parent) | Out-Null
[IO.File]::WriteAllText($Out, $sb.ToString(), $enc)

Write-Host "生成 $Out"
Write-Host "  英文 $($english.Count) 条 / 中文 $($chinese.Count) 条 / 造句 $($tokens.Count) 条"
