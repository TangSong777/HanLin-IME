# 生成 mixed_tokens.dict.yaml：把「英文白名单」变成可参与造句的词典词条。
#
# 为什么需要：custom_phrase 里的条目只作为独立候选，**不参与 Rime 的造句**。
# 想让 arduinoqishiyejiunayang 出「Arduino其实也就那样」，Arduino 必须是一条
# 词典词条（与 ESP32/STM32 同理）。
#
# 输入：custom_phrase.txt（取其纯英文条目）
# 输出：mixed_tokens.dict.yaml（词<Tab>小写编码<Tab>权重）
#
# 用法：
#   pwsh -File .\src\build-support\gen-mixed-tokens.ps1 `
#       -CustomPhrase <用户目录>\custom_phrase.txt -Out <用户目录>\mixed_tokens.dict.yaml
param(
  [string]$CustomPhrase = (Join-Path $env:APPDATA 'Rime\custom_phrase.txt'),
  [string]$Out = (Join-Path $env:APPDATA 'Rime\mixed_tokens.dict.yaml')
)

$ErrorActionPreference = 'Stop'
$enc = New-Object Text.UTF8Encoding($false)

$lines = [Collections.Generic.List[string]]::new()
$lines.AddRange([string[]]@(
  '# Rime dictionary',
  '# encoding: utf-8',
  '#',
  '# 由 src/build-support/gen-mixed-tokens.ps1 生成，请勿手工编辑。',
  '# 内容：英文白名单（来自 custom_phrase.txt）+ 含数字型号。',
  '# 作用：让这些词能像音节一样参与 Rime 造句。',
  '---',
  'name: mixed_tokens',
  'version: "1.0"',
  'sort: by_weight',
  '...',
  ''
))

# 额外词条由造句词典维护，给型号和专名更高权重。
$extra = @(
  @('ESP32', 'esp32', '100000'),
  @('STM32', 'stm32', '100000'),
  @('杨寒淋', 'yanghanlin', '100000')
)
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($entry in $extra) {
  $pair = "$($entry[0])`t$($entry[1])"
  [void]$seen.Add($pair)
  $lines.Add("$pair`t$($entry[2])")
}

foreach ($line in [IO.File]::ReadAllLines($CustomPhrase, $enc)) {
  if ($line -notmatch '^(?!#)([\x20-\x7E]+)\t([a-zA-Z0-9]+)(?:\t|$)') { continue }
  $word = $Matches[1]
  $code = $Matches[2].ToLowerInvariant()
  # 首字母能单独成音节的英文词（a/e/i/o/u/v 开头，如 Ubuntu / Intel / VSCode）
  # **不写入 mixed_tokens**，只保留在 custom_phrase（精确码 + 前缀补全）。
  # 原因：u 是 ubuntu 的合法首音节，误触 u 后整句会被接成「Ubuntu去哪里」
  # （输入 uqnali，本意 qunali → 去那里）。这类词只要参与造句，无论权重多低，
  # 都是该输入串的唯一解析，降权没用，只能让它不参与造句。
  if ($code -match '^[aeiouv]') { continue }
  $pair = "$word`t$code"
  if ($seen.Add($pair)) { $lines.Add("$pair`t1000") }
}

$target = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
[IO.File]::WriteAllText($target, ($lines -join "`r`n") + "`r`n", $enc)
Write-Host "生成 $target（$($seen.Count) 条）"
