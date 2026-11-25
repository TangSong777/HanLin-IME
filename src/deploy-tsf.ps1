#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$BuildRoot = (Join-Path $PSScriptRoot 'output'),
    [string]$SharedDataDir,
    [string]$RimeSourceDir,
    [string]$UserDataDir,
    [string]$LegacyServerPath,
    [string]$InstallPackage,
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-support\HanLinBuild.ps1')

$userKey = 'HKCU:\Software\HanLin'
$previousUserDir = [Microsoft.Win32.Registry]::GetValue('HKEY_CURRENT_USER\Software\HanLin', 'UserDataDir', $null)
if (-not $UserDataDir) { $UserDataDir = $previousUserDir }
if (-not $UserDataDir) { $UserDataDir = Join-Path $env:APPDATA 'HanLin' }
$UserDataDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($UserDataDir)
if (-not $RimeSourceDir) { $RimeSourceDir = $UserDataDir }

function Invoke-HanLinProcess([string]$Program, [string[]]$Arguments) {
    $run = [Diagnostics.ProcessStartInfo]::new($Program)
    $run.UseShellExecute = $false
    $run.CreateNoWindow = $true
    $run.WorkingDirectory = Split-Path $Program -Parent
    foreach ($argument in $Arguments) { $run.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($run)
    try {
        $process.WaitForExit()
        return $process.ExitCode
    } finally { $process.Dispose() }
}

if (-not $InstallPackage) {
    $out = Join-Path $BuildRoot ('local-install-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
    $builder = @{ BuildRoot = $BuildRoot; RimeSourceDir = $RimeSourceDir; OutputDir = $out }
    if ($SharedDataDir) { $builder.SharedDataDir = $SharedDataDir }
    & (Join-Path $PSScriptRoot 'installer\build-installer.ps1') @builder
    $version = Get-HanLinVersion (Join-Path $PSScriptRoot 'installer\HanLin.iss')
    $InstallPackage = Join-Path $out "HanLin-Setup-$version.exe"
}
$package = Get-Item -LiteralPath $InstallPackage
Write-Host "本地安装包 $($package.FullName)"
if ($PrepareOnly) { return }
if ($LegacyServerPath) { $LegacyServerPath = (Get-Item -LiteralPath $LegacyServerPath).FullName }

$backup = Join-Path $env:LOCALAPPDATA ('HanLin\deployment-backups\' +
    (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,6))
[void][IO.Directory]::CreateDirectory($backup)
$setupLog = Join-Path $backup 'setup.log'
$setupArgs = @('/SILENT', '/NORESTART', '/SUPPRESSMSGBOXES', '/HANLINDEFERSTART=1', ('/LOG="{0}"' -f $setupLog))
$installer = Start-Process -FilePath $package.FullName -ArgumentList $setupArgs -Verb RunAs -WindowStyle Hidden -PassThru -Wait
if ($installer.ExitCode -ne 0) { throw "安装失败，退出码 $($installer.ExitCode)。日志 $setupLog" }

$install = Get-ItemPropertyValue -LiteralPath 'HKLM:\SOFTWARE\HanLin' -Name InstallRoot
$hostExe = Join-Path $install 'HanLinHost.exe'
$legacy = @()
if ($LegacyServerPath) {
    $name = [IO.Path]::GetFileNameWithoutExtension($LegacyServerPath)
    $session = [Diagnostics.Process]::GetCurrentProcess().SessionId
    $legacy = @(Get-Process -Name $name -ErrorAction SilentlyContinue | Where-Object {
        $_.SessionId -eq $session -and $_.Path -ieq $LegacyServerPath
    })
}
$legacyStopped = $false
$currentChanged = $false
$nativeStarted = $false
$configurationCompiled = $false
try {
    if ($legacy.Count) {
        $result = Invoke-HanLinProcess $LegacyServerPath @('/q')
        if ($result -ne 0) { throw "原输入法后台停止失败，退出码 $result。" }
        foreach ($process in $legacy) {
            if (-not $process.WaitForExit(6000)) { throw '原输入法后台仍占用用户目录。' }
        }
        $legacyStopped = $true
    }

    @{ userDataDir = $previousUserDir; target = $UserDataDir; legacy = $LegacyServerPath } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'user-data.json') -Encoding utf8
    foreach ($relative in 'hanlin.custom.yaml', 'build') {
        $source = Join-Path $UserDataDir $relative
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $backup -Recurse }
    }
    if (-not (Test-Path -LiteralPath $userKey)) { [void](New-Item -Path $userKey -Force) }
    Set-ItemProperty -LiteralPath $userKey -Name UserDataDir -Value $UserDataDir
    $currentChanged = $true

    $result = Invoke-HanLinProcess $hostExe @('--deploy', '--user-data', $UserDataDir)
    if ($result -ne 0) { throw "寒淋配置编译失败，退出码 $result。" }
    $configurationCompiled = $true
    $background = Start-Process -FilePath $hostExe -WorkingDirectory $install -WindowStyle Hidden -PassThru
    $nativeStarted = $true
    $ready = $false
    try {
        for ($attempt = 0; $attempt -lt 40; $attempt++) {
            if ((Invoke-HanLinProcess $hostExe @('--health')) -eq 0) { $ready = $true; break }
            if ($background.HasExited -and $background.ExitCode -ne 0) {
                throw "寒淋后台启动失败，退出码 $($background.ExitCode)。"
            }
            Start-Sleep -Milliseconds 250
        }
    } finally {
        $background.Dispose()
    }
    if (-not $ready) { throw '寒淋后台未能完成启动握手。' }
    $result = Invoke-HanLinProcess $hostExe @('--enable-profile')
    if ($result -ne 0) { throw '寒淋输入法注册后未能加入当前用户的输入法列表。' }

    if ($LegacyServerPath) {
        $winPS = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $profileScript = Join-Path $PSScriptRoot 'build-support\Select-HanLinProfile.ps1'
        & $winPS -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $profileScript -Backup $backup
        if ($LASTEXITCODE -ne 0) { throw "Windows 输入法列表切换失败。备份 $backup" }
    }
} catch {
    if ($nativeStarted) { [void](Invoke-HanLinProcess $hostExe @('--stop')) }
    if ($currentChanged) {
        if ($null -eq $previousUserDir) {
            Remove-ItemProperty -LiteralPath $userKey -Name UserDataDir -ErrorAction SilentlyContinue
        } else {
            Set-ItemProperty -LiteralPath $userKey -Name UserDataDir -Value $previousUserDir
        }
    }
    if ($legacyStopped) {
        $saved = Join-Path $backup 'build'
        if ($configurationCompiled -and (Test-Path -LiteralPath $saved -PathType Container)) {
            $current = Join-Path $UserDataDir 'build'
            $failed = Join-Path $UserDataDir ('build.hanlin-failed-' + [Guid]::NewGuid().ToString('N'))
            if ([IO.Directory]::Exists($current)) {
                $profileRoot = (Get-Item -LiteralPath $UserDataDir).FullName
                $cache = Get-Item -LiteralPath $current
                if ($cache.Parent.FullName -ine $profileRoot -or [IO.Path]::GetDirectoryName($failed) -ine $profileRoot -or
                    ($cache.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                    throw "不能安全移动编译缓存，备份在 $saved。"
                }
                [IO.Directory]::Move($cache.FullName, $failed)
            }
            Copy-Item -LiteralPath $saved -Destination $current -Recurse
        }
        Start-Process -FilePath $LegacyServerPath -WorkingDirectory (Split-Path $LegacyServerPath -Parent) -WindowStyle Hidden
    }
    throw
}

Write-Host "已启动寒淋输入法。部署记录 $backup"
