#Requires -Version 7.0
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
    [string]$OutputDir = (Join-Path $PSScriptRoot 'output')
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$src = Split-Path $root -Parent
$repo = Split-Path $src -Parent
$config = Join-Path $repo 'rime-config'
$installer = Join-Path $root 'HanLin.iss'
. (Join-Path $src 'build-support\HanLinBuild.ps1')
$packageVersion = Get-HanLinVersion $installer
$packageName = "HanLin-Setup-$packageVersion.exe"
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
$stage = Join-Path $output ('stage-' + [guid]::NewGuid().ToString('N').Substring(0,8))
$payload = Join-Path $stage 'payload'
$data = Join-Path $payload 'data'
$preset = Join-Path $data 'preset'
$compiledProfile = Join-Path $stage 'compile-profile'

$manifestFile = Join-Path $BuildRoot 'HanLinBuild.json'
if (-not (Test-Path -LiteralPath $manifestFile -PathType Leaf)) {
    throw '找不到 HanLinBuild.json，请先用 build-hanlin.ps1 生成此构建目录。'
}
$manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json -AsHashtable
if ($manifest.version -cne $packageVersion -or $manifest.sourceFingerprint -cne (Get-HanLinSourceFingerprint $src)) {
    throw '构建记录与当前前端源码或版本不同，请重新运行 build-hanlin.ps1。'
}

# The default release inputs are the curated public dictionary and its shared
# resources. Runtime profiles can contain private phrases and learning data.
$releaseInputs = Join-Path $repo '.local-build-inputs'
if (-not $SharedDataDir) {
    $SharedDataDir = Join-Path $releaseInputs 'SharedData'
}
if (-not $RimeSourceDir) {
    $RimeSourceDir = Join-Path $releaseInputs 'RimeSource'
}
if (-not $SharedDataDir -or -not (Test-Path -LiteralPath (Join-Path $SharedDataDir 'default.yaml') -PathType Leaf)) {
    throw '请用 -SharedDataDir 指定包含 default.yaml 的 Rime 共享数据。'
}
if (-not (Test-Path -LiteralPath (Join-Path $RimeSourceDir 'rime_ice.schema.yaml') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $RimeSourceDir 'LICENSE') -PathType Leaf)) {
    throw '请用 -RimeSourceDir 指定包含 rime_ice.schema.yaml 和 LICENSE 的雾凇拼音源文件目录。'
}

function Copy-PayloadFile([string]$Source, [string]$Target) {
    New-Item -ItemType Directory -Path (Split-Path $Target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Target -Force
}

foreach ($artifact in (Get-HanLinBuildArtifacts).GetEnumerator()) {
    $target = Join-Path $payload $artifact.Key
    Copy-PayloadFile (Join-Path $BuildRoot $artifact.Value) $target
    if ((Get-FileHash -LiteralPath $target).Hash -cne $manifest.artifacts[$artifact.Key]) {
        throw "$($artifact.Key) 的哈希与构建记录不同，请重新运行 build-hanlin.ps1。"
    }
    if ($artifact.Key -match '\.(?:exe|dll)$' -and $artifact.Key -ne 'rime.dll' -and
        [Diagnostics.FileVersionInfo]::GetVersionInfo($target).ProductVersion -cne $packageVersion) {
        throw "$($artifact.Key) 的产品版本与安装包版本不同。"
    }
}
Copy-PayloadFile (Join-Path $root 'Migrate-HanLinUserData.ps1') (Join-Path $payload 'Migrate-HanLinUserData.ps1')
Copy-PayloadFile (Join-Path $src 'Sync-HanLin.ps1') (Join-Path $payload 'Sync-HanLin.ps1')
New-Item -ItemType Directory -Path $data,$preset -Force | Out-Null
# Ship only the runtime resources the HanLin full-pinyin stack may use.
# The distributor's shared directory can contain additional, unrelated schemas.
foreach ($name in 'essay.txt','key_bindings.yaml','pinyin.yaml','punctuation.yaml','symbols.yaml') {
    $source = Join-Path $SharedDataDir $name
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        Copy-PayloadFile $source (Join-Path $data $name)
    }
}
$sharedOpenCc = Join-Path $SharedDataDir 'opencc'
if (Test-Path -LiteralPath $sharedOpenCc -PathType Container) {
    Copy-Item -LiteralPath $sharedOpenCc -Destination $data -Recurse -Force
}
# 雾凇从自己的 default.yaml 继承标点映射和按键绑定。
foreach ($name in 'default.yaml','rime_ice.schema.yaml','melt_eng.schema.yaml','melt_eng.dict.yaml',
                  'radical_pinyin.schema.yaml','radical_pinyin.dict.yaml','symbols_v.yaml','symbols_caps_v.yaml') {
    Copy-PayloadFile (Join-Path $RimeSourceDir $name) (Join-Path $data $name)
}
$defaultPath = Join-Path $data 'default.yaml'
$defaultText = [IO.File]::ReadAllText($defaultPath)
$schemaList = [regex]::new('(?m)^schema_list:[^\r\n]*\r?\n(?:(?:[ \t]+[^\r\n]*|[ \t]*)\r?\n)*')
if ($schemaList.Matches($defaultText).Count -ne 1) {
    throw '无法识别雾凇默认配置的方案列表。'
}
$lineEnd = if ($defaultText.Contains("`r`n")) { "`r`n" } else { "`n" }
[IO.File]::WriteAllText($defaultPath, $schemaList.Replace($defaultText,
    "schema_list:${lineEnd}  - schema: rime_ice${lineEnd}${lineEnd}", 1))
Copy-PayloadFile (Join-Path $config 'rime_ice.dict.yaml') (Join-Path $data 'rime_ice.dict.yaml')
Copy-PayloadFile (Join-Path $config 'hanlin.yaml') (Join-Path $data 'hanlin.yaml')
$mixedTokens = Join-Path $data 'mixed_tokens.dict.yaml'
& (Join-Path $src 'build-support\gen-mixed-tokens.ps1') -CustomPhrase (Join-Path $config 'custom_phrase.txt') -Out $mixedTokens
$expected = [IO.File]::ReadAllText((Join-Path $config 'mixed_tokens.dict.yaml')).Replace("`r`n", "`n")
if ([IO.File]::ReadAllText($mixedTokens).Replace("`r`n", "`n") -cne $expected) {
    throw 'mixed_tokens.dict.yaml 与英文白名单生成结果不同，请运行 gen-mixed-tokens.ps1 更新。'
}
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
foreach ($name in 'default.custom.yaml','rime_ice.custom.yaml','custom_phrase.txt','hanlin.custom.yaml') {
    Copy-PayloadFile (Join-Path $config $name) (Join-Path $preset $name)
}
Copy-PayloadFile (Join-Path $src 'LICENSE.txt') (Join-Path $payload 'licenses\Weasel.txt')
Copy-PayloadFile (Join-Path $repo 'reference\librime-reference\LICENSE') (Join-Path $payload 'licenses\librime.txt')
Copy-PayloadFile (Join-Path $RimeSourceDir 'LICENSE') (Join-Path $payload 'licenses\rime-ice.txt')

$start = [Diagnostics.ProcessStartInfo]::new((Join-Path $payload 'HanLinHost.exe'))
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
foreach ($argument in '--compile-package-data', $compiledProfile) { $start.ArgumentList.Add($argument) }
$deploy = [Diagnostics.Process]::Start($start)
try {
    $deploy.WaitForExit()
    $deployExit = $deploy.ExitCode
} finally {
    $deploy.Dispose()
}
if ($deployExit -ne 0) { throw "安装包词库编译失败，退出码 $deployExit。" }
Copy-Item -LiteralPath (Join-Path $compiledProfile 'build') -Destination (Join-Path $data 'build') -Recurse

$requiredSchemas = 'melt_eng.schema.yaml','radical_pinyin.schema.yaml','rime_ice.schema.yaml'
foreach ($folder in $data, (Join-Path $data 'build')) {
    $schemas = @(Get-ChildItem -LiteralPath $folder -Filter '*.schema.yaml' -File | Select-Object -ExpandProperty Name)
    if (Compare-Object $requiredSchemas $schemas) {
        throw "寒淋方案或其依赖清单异常：$folder"
    }
}
foreach ($name in 'default.yaml','rime_ice.schema.yaml','rime_ice.table.bin','rime_ice.prism.bin','hanlin.yaml') {
    if ((Get-Item -LiteralPath (Join-Path $data "build\$name")).Length -eq 0) { throw "安装包缺少编译产物 $name。" }
}
$private = @(Get-ChildItem -LiteralPath $payload -Recurse -Force | Where-Object {
    $_.Name -match '\.userdb(?:\.|$)|^(installation\.yaml|user\.yaml|sync|sogou_phrases\.txt)$'
})
if ($private) { throw "安装包混入用户数据 $($private[0].Name)。" }
$localSkin = @(Get-ChildItem -LiteralPath $payload -Recurse -Force | Where-Object {
    $_.Name -match '^(?:skin|skin\.ini)$|\.ssf$'
})
if ($localSkin) { throw "安装包混入本机皮肤资源 $($localSkin[0].Name)。" }
$dataFiles = @((Get-ChildItem -LiteralPath $data -Recurse -Force -File).FullName; (Join-Path $payload 'rime.dll'))
$revision = Get-HanLinContentFingerprint $payload $dataFiles
[IO.File]::WriteAllText((Join-Path $data 'hanlin-data.revision'), $revision)
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
if (-not $iscc) { throw '找不到 ISCC.exe，请先安装 Inno Setup 6.6 或更新版本。' }

& $iscc "/DPayloadDir=$payload" "/O$output" $installer
if ($LASTEXITCODE -ne 0) { throw "安装包编译失败，退出码 $LASTEXITCODE。" }
$package = Get-Item -LiteralPath (Join-Path $output $packageName)
if (-not $package.Length) { throw '安装包为空。' }
$package.FullName
