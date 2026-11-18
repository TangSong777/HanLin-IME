#Requires -Version 7.0
<#
.SYNOPSIS
  迁移小狼毫或其备份中的配置、短语和用户词典。
.DESCRIPTION
  先退出正在使用该目录的小狼毫，并关闭寒淋后端。
  在新目录完成部署后切换寒淋的 UserDataDir。-NoActivate 只生成新目录。
#>
[CmdletBinding()]
param(
    [string]$Source,
    [string]$Destination,
    [string]$HanLinHost = (Join-Path $PSScriptRoot 'HanLinHost.exe'),
    [switch]$NoActivate
)

$ErrorActionPreference = 'Stop'
$registry = 'HKCU:\Software\HanLin'
$previous = Get-ItemPropertyValue -LiteralPath $registry -Name UserDataDir -ErrorAction SilentlyContinue
if (-not $previous) { $previous = Join-Path $env:APPDATA 'HanLin' }
if (-not $Source) {
    $Source = Get-ItemPropertyValue -LiteralPath 'HKCU:\Software\Rime\Weasel' -Name RimeUserDir -ErrorAction SilentlyContinue
    if (-not $Source) { $Source = Join-Path $env:APPDATA 'Rime' }
}
$sourceDir = Get-Item -LiteralPath $Source
if (-not $sourceDir.PSIsContainer -or ($sourceDir.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw '请用 -Source 指定实际的 Rime 用户目录。'
}
$exe = Get-Item -LiteralPath $HanLinHost
if ($exe.PSIsContainer) { throw '请用 -HanLinHost 指定寒淋安装目录中的 HanLinHost.exe。' }

if (-not $Destination) {
    $Destination = [IO.Path]::TrimEndingDirectorySeparator($previous)
    if (Test-Path -LiteralPath $Destination) {
        $Destination += '-migrated-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    }
}
$target = [IO.Path]::TrimEndingDirectorySeparator($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Destination))
$sourcePrefix = $sourceDir.FullName.TrimEnd('\') + '\'
if ($target -ieq $sourceDir.FullName -or $target.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw '请将迁移目标设在来源目录之外。'
}
if (Test-Path -LiteralPath $target) { throw "目标已存在，请指定一个新的目录。$target" }

$entries = @(Get-ChildItem -LiteralPath $sourceDir.FullName -Force | Where-Object {
    if ($_.PSIsContainer) {
        $_.Name -in 'cn_dicts','en_dicts','lua','opencc' -or $_.Name.EndsWith('.userdb', [StringComparison]::OrdinalIgnoreCase)
    } else {
        $_.Name -ne 'installation.yaml' -and $_.Name -match '\.(?:ya?ml|txt|lua|json|gram|ocd2|userdb|userdb\.kct)$'
    }
})
if (-not $entries) { throw "来源目录没有可迁移的配置或词库。$Source" }

$parent = [IO.Path]::GetDirectoryName($target)
[void][IO.Directory]::CreateDirectory($parent)
$staging = Join-Path $parent ('.hanlin-migration-' + [Guid]::NewGuid().ToString('N'))
$pending = New-Item -ItemType Directory -Path $staging

function Copy-ProfileEntry($Entry, [string]$DatabaseLock) {
    if ($Entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "请使用不含文件链接的备份来迁移。$($Entry.FullName)"
    }
    if ($Entry.FullName -ieq $DatabaseLock) { return }
    $relative = [IO.Path]::GetRelativePath($sourceDir.FullName, $Entry.FullName)
    $copy = Join-Path $staging $relative
    if ($Entry.PSIsContainer) {
        [void][IO.Directory]::CreateDirectory($copy)
        foreach ($child in Get-ChildItem -LiteralPath $Entry.FullName -Force) {
            Copy-ProfileEntry $child $DatabaseLock
        }
    } else {
        [IO.File]::Copy($Entry.FullName, $copy)
    }
}

try {
    foreach ($entry in $entries) {
        $lockPath = $null
        $lock = $null
        if ($entry.PSIsContainer -and $entry.Name -like '*.userdb' -and
            -not ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            $lockPath = Join-Path $entry.FullName 'LOCK'
            if (Test-Path -LiteralPath $lockPath) {
                try {
                    $lock = [IO.File]::Open($lockPath, 'Open', 'Read', 'None')
                } catch {
                    throw "无法独占词典 $($entry.Name) 的 LOCK。请退出小狼毫或其他正在使用词典的进程后重试。"
                }
            }
        }
        try { Copy-ProfileEntry $entry $lockPath } finally { if ($lock) { $lock.Dispose() } }
    }

    $start = [Diagnostics.ProcessStartInfo]::new($exe.FullName)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    foreach ($argument in '--deploy', '--user-data', $staging) { $start.ArgumentList.Add($argument) }
    $deploy = [Diagnostics.Process]::Start($start)
    try {
        $deploy.WaitForExit()
        $deployExit = $deploy.ExitCode
    } finally {
        $deploy.Dispose()
    }
    if ($deployExit -eq 3) { throw '寒淋后端正在运行。请关闭后再迁移。' }
    if ($deployExit -ne 0) { throw "迁移副本部署失败，退出码 $deployExit。" }

    [IO.Directory]::Move($staging, $target)
    $staging = $null
    if (-not $NoActivate) {
        if (-not (Test-Path -LiteralPath $registry)) { New-Item -Path $registry | Out-Null }
        New-ItemProperty -LiteralPath $registry -Name UserDataDir -Value $target -PropertyType String -Force | Out-Null
        Write-Host '下次启动寒淋时将使用新的用户目录。'
    }
    Write-Host "原寒淋用户目录  $previous"
    $target
} finally {
    if ($staging -and (Test-Path -LiteralPath $staging -PathType Container)) {
        $current = Get-Item -LiteralPath $staging -Force
        if ($current.FullName -ieq $pending.FullName -and $current.Parent.FullName -ieq $parent -and
            -not ($current.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            Remove-Item -LiteralPath $current.FullName -Recurse -Force -ErrorAction Continue
        }
    }
}
