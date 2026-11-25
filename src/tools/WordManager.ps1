param(
    [switch]$Deploy,
    [string]$ExpectedTarget,
    [AllowEmptyString()][string]$ExpectedRevision
)

$ErrorActionPreference = 'Stop'
$install = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\HanLin' -ErrorAction SilentlyContinue).InstallRoot
$choices = @(Join-Path $PSScriptRoot '..\output\hanlin-tip-x64\HanLinTools.exe')
if ($install) { $choices = @(Join-Path $install 'HanLinTools.exe') + $choices }
$tool = $choices | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $tool) { throw '找不到寒淋词库管理工具，请先构建或安装。' }

$launch = [Diagnostics.ProcessStartInfo]::new((Get-Item -LiteralPath $tool).FullName)
$launch.UseShellExecute = $false
$launch.WorkingDirectory = Split-Path $launch.FileName -Parent
if ($Deploy) {
    $launch.Arguments = '--deploy-words'
    if ($ExpectedTarget) {
        $target = [IO.Path]::GetFullPath($ExpectedTarget).TrimEnd('\')
        $launch.Arguments += ' --expect-directory "' + $target + '"'
    }
    if ($PSBoundParameters.ContainsKey('ExpectedRevision')) {
        if ($ExpectedRevision -notmatch '^(?:[0-9A-Fa-f]{64})?$') { throw '词表版本无效。' }
        $launch.Arguments += ' --revision "' + $ExpectedRevision + '"'
    }
    $process = [Diagnostics.Process]::Start($launch)
    try {
        $process.WaitForExit()
        exit $process.ExitCode
    } finally { $process.Dispose() }
}
$launch.Arguments = '--words'
[Diagnostics.Process]::Start($launch).Dispose()
