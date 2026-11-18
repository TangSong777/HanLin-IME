<#
.SYNOPSIS
  生成寒淋输入法离线安装包。
.DESCRIPTION
  使用 build-hanlin.ps1 的 x64 主进程、双架构 TSF 和 librime DLL。
  从共享数据和雾凇源文件编译随包词库。
  -SkipCompile 完成暂存和词库编译后退出。
#>
[CmdletBinding()]
param(
    [switch]$SkipCompile,
    [string]$BuildRoot = (Join-Path $PSScriptRoot '..\output'),
    [string]$SharedDataDir,
    [string]$RimeSourceDir,
    [string]$SkinDir
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$src = Split-Path $root -Parent
$repo = Split-Path $src -Parent
$config = Join-Path $repo 'rime-config'
$installer = Join-Path $root 'HanLin.iss'
$versionLine = Select-String -LiteralPath $installer -Pattern '^#define MyAppVersion "([0-9.]+)"$'
$packageVersion = $versionLine.Matches[0].Groups[1].Value
$packageName = "HanLin-Setup-$packageVersion.exe"
$output = Join-Path $root 'output'
$stage = Join-Path $output ('stage-' + [guid]::NewGuid().ToString('N').Substring(0,8))
$payload = Join-Path $stage 'payload'
$data = Join-Path $payload 'data'
$preset = Join-Path $data 'preset'
$compiledProfile = Join-Path $stage 'compile-profile'

if (-not $SharedDataDir) {
    $SharedDataDir = @((Join-Path $BuildRoot 'hanlin-tip-x64\data'), (Join-Path $repo 'weasel-0.17.4\data')) |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ 'default.yaml') -PathType Leaf } | Select-Object -First 1
}
if (-not $RimeSourceDir) {
    $RimeSourceDir = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Rime\Weasel' -Name RimeUserDir -ErrorAction SilentlyContinue).RimeUserDir
    if (-not $RimeSourceDir) { $RimeSourceDir = Join-Path $env:APPDATA 'Rime' }
}
if (-not $SkinDir) { $SkinDir = Join-Path $repo 'assets\Color-P' }
if (-not $SharedDataDir -or -not (Test-Path -LiteralPath (Join-Path $SharedDataDir 'default.yaml') -PathType Leaf)) {
    throw '请用 -SharedDataDir 指定包含 default.yaml 的 Rime 共享数据。'
}
if (-not (Test-Path -LiteralPath (Join-Path $RimeSourceDir 'rime_ice.schema.yaml') -PathType Leaf)) {
    throw '请用 -RimeSourceDir 指定雾凇拼音源文件目录。'
}
if (-not (Test-Path -LiteralPath (Join-Path $SkinDir 'skin.ini') -PathType Leaf)) {
    throw '皮肤目录缺少 skin.ini。'
}

function Copy-PayloadFile([string]$Source, [string]$Target) {
    New-Item -ItemType Directory -Path (Split-Path $Target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Target -Force
}

Copy-PayloadFile (Join-Path $BuildRoot 'hanlin-tip-x64\HanLinHost.exe') (Join-Path $payload 'HanLinHost.exe')
Copy-PayloadFile (Join-Path $BuildRoot 'hanlin-tip-x64\rime.dll') (Join-Path $payload 'rime.dll')
foreach ($arch in 'x64','x86') {
    Copy-PayloadFile (Join-Path $BuildRoot "hanlin-tip-$arch\HanLinTip.dll") (Join-Path $payload "$arch\HanLinTip.dll")
}

New-Item -ItemType Directory -Path $data,$preset -Force | Out-Null
foreach ($item in Get-ChildItem -LiteralPath $SharedDataDir) {
    if ($item.PSIsContainer -and $item.Name -ne 'opencc') { continue }
    if (-not $item.PSIsContainer -and $item.Name -notmatch '\.(?:yaml|txt)$') { continue }
    Copy-Item -LiteralPath $item.FullName -Destination $data -Recurse -Force
}
# 雾凇从自己的 default.yaml 继承标点映射和按键绑定。
foreach ($name in 'default.yaml','rime_ice.schema.yaml','melt_eng.schema.yaml','melt_eng.dict.yaml',
                  'radical_pinyin.schema.yaml','radical_pinyin.dict.yaml','symbols_v.yaml','symbols_caps_v.yaml') {
    Copy-PayloadFile (Join-Path $RimeSourceDir $name) (Join-Path $data $name)
}
Copy-PayloadFile (Join-Path $config 'rime_ice.dict.yaml') (Join-Path $data 'rime_ice.dict.yaml')
# rime_ice.dict.yaml 的 import_tables 引用了 mixed_tokens（英文白名单），缺了它词典编译会失败。
Copy-PayloadFile (Join-Path $config 'mixed_tokens.dict.yaml') (Join-Path $data 'mixed_tokens.dict.yaml')
foreach ($name in '8105','41448','base','ext','tencent','others') {
    $file = "cn_dicts\$name.dict.yaml"
    Copy-PayloadFile (Join-Path $RimeSourceDir $file) (Join-Path $data $file)
}
foreach ($file in 'en_dicts\en.dict.yaml','en_dicts\en_ext.dict.yaml') {
    Copy-PayloadFile (Join-Path $RimeSourceDir $file) (Join-Path $data $file)
}
Copy-PayloadFile (Join-Path $RimeSourceDir 'en_dicts\cn_en.txt') (Join-Path $preset 'en_dicts\cn_en.txt')
# ReverseDb resolves the calendar table inside the user directory.
Copy-PayloadFile (Join-Path $RimeSourceDir 'lua\lunar.db') (Join-Path $preset 'lua\lunar.db')
foreach ($folder in 'lua','opencc') {
    $origin = Join-Path $RimeSourceDir $folder
    foreach ($item in Get-ChildItem -LiteralPath $origin -Recurse -File) {
        if ($item.Name -notmatch '\.(?:lua|json|txt|ocd2)$') { continue }
        $relative = [IO.Path]::GetRelativePath($origin, $item.FullName)
        Copy-PayloadFile $item.FullName (Join-Path (Join-Path $data $folder) $relative)
    }
}
foreach ($item in Get-ChildItem -LiteralPath (Join-Path $config 'lua') -Filter '*.lua' -File) {
    Copy-PayloadFile $item.FullName (Join-Path $data "lua\$($item.Name)")
}
foreach ($name in 'default.custom.yaml','rime_ice.custom.yaml','custom_phrase.txt') {
    Copy-PayloadFile (Join-Path $config $name) (Join-Path $preset $name)
}
$style = Get-Content -LiteralPath (Join-Path $config 'weasel.custom.yaml') -Raw
$style -replace '(?m)^  style/ssf_skin:[^\r\n]*', '  style/ssf_skin: ""' |
    Set-Content -LiteralPath (Join-Path $preset 'weasel.custom.yaml') -Encoding utf8

$skin = Join-Path $payload 'skin'
New-Item -ItemType Directory -Path $skin | Out-Null
foreach ($item in Get-ChildItem -LiteralPath $SkinDir -File) {
    if ($item.Name -eq 'skin.ini' -or $item.Extension -match '^\.(?:png|jpe?g|ico|bmp)$') {
        Copy-Item -LiteralPath $item.FullName -Destination $skin
    }
}
Copy-PayloadFile (Join-Path $src 'LICENSE.txt') (Join-Path $payload 'licenses\Weasel.txt')
Copy-PayloadFile (Join-Path $repo 'reference\librime-reference\LICENSE') (Join-Path $payload 'licenses\librime.txt')
Copy-PayloadFile (Join-Path $RimeSourceDir 'LICENSE') (Join-Path $payload 'licenses\rime-ice.txt')

# HanLinHost 是 GUI 程序（wWinMain），Start-Process -Wait 拿不到可靠退出码，改用 start /wait。
$deployExe = Join-Path $payload 'HanLinHost.exe'
$deployCmd = 'start /wait "" "{0}" --deploy --user-data "{1}"' -f $deployExe, $compiledProfile
Push-Location $payload
try { cmd.exe /d /c $deployCmd } finally { Pop-Location }
$deployExit = $LASTEXITCODE
if ($deployExit -eq 3) { throw '打包需要临时使用寒淋部署程序。请先退出正在运行的寒淋后端。' }
if ($deployExit -ne 0) { throw "安装包词库编译失败，退出码 $deployExit。" }
Copy-Item -LiteralPath (Join-Path $compiledProfile 'build') -Destination (Join-Path $data 'build') -Recurse

foreach ($name in 'default.yaml','rime_ice.schema.yaml','rime_ice.table.bin','rime_ice.prism.bin','weasel.yaml') {
    if ((Get-Item -LiteralPath (Join-Path $data "build\$name")).Length -eq 0) { throw "安装包缺少编译产物 $name。" }
}
$private = @(Get-ChildItem -LiteralPath $payload -Recurse -Force | Where-Object {
    $_.Name -match '\.userdb(?:\.|$)|^(installation\.yaml|user\.yaml|sync|sogou_phrases\.txt)$'
})
if ($private) { throw "安装包混入用户数据 $($private[0].Name)。" }
"安装包载荷 $payload"
if ($SkipCompile) { return }

$iscc = (Get-Command ISCC.exe -CommandType Application -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $programFiles32 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    $candidates = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
                    "$programFiles32\Inno Setup 6\ISCC.exe",
                    "$env:ProgramFiles\Inno Setup 6\ISCC.exe")
    $iscc = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $iscc) { throw '找不到 ISCC.exe，请先安装 Inno Setup 6.3 或更新版本。' }

& $iscc "/DPayloadDir=$payload" $installer
if ($LASTEXITCODE -ne 0) { throw "安装包编译失败，退出码 $LASTEXITCODE。" }
$package = Get-Item -LiteralPath (Join-Path $output $packageName)
if (-not $package.Length) { throw '安装包为空。' }
$package.FullName
