param(
    [string]$RendererLibrary = (Join-Path $PSScriptRoot 'msbuild\Release\x64\WeaselUI.lib'),
    [string]$EngineDll = (Join-Path $PSScriptRoot 'dist\rime.dll'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot 'output'),
    [string]$SharedDataDir
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-support\Find-Toolchain.ps1')
$cmake = (Get-Command cmake.exe -CommandType Application).Source
$boost = Get-BoostRoot
$source = Join-Path $PSScriptRoot 'HanLin'
$renderer = (Get-Item -LiteralPath $RendererLibrary).FullName
$engine = (Get-Item -LiteralPath $EngineDll).FullName
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputRoot)
$shared = $null
if ($SharedDataDir) {
    $shared = (Get-Item -LiteralPath $SharedDataDir).FullName
    if (-not (Test-Path -LiteralPath (Join-Path $shared 'default.yaml') -PathType Leaf)) {
        throw 'Shared data must contain default.yaml.'
    }
}

Push-Location -LiteralPath $PSScriptRoot
try {
    foreach ($architecture in 'x64', 'x86') {
        $vcvars = Get-VcVarsPath -Arch $architecture
        $build = Join-Path $output "hanlin-tip-$architecture"
        $options = if ($architecture -eq 'x64') {
            ('-DHANLIN_BUILD_HOST=ON -DBOOST_ROOT="{0}" -DHANLIN_RENDERER_LIBRARY="{1}"' -f $boost, $renderer)
        } else { '-DHANLIN_BUILD_HOST=OFF' }
        $command = 'call "{0}" >nul && "{1}" -S "{2}" -B "{3}" -GNinja -DCMAKE_BUILD_TYPE=Release {4} && "{1}" --build "{3}"' -f $vcvars, $cmake, $source, $build, $options
        & cmd.exe /d /c $command
        if ($LASTEXITCODE -ne 0) { throw "HanLin $architecture build failed ($LASTEXITCODE)" }
    }
    $hostDir = Join-Path $output 'hanlin-tip-x64'
    Copy-Item -LiteralPath $engine -Destination (Join-Path $hostDir 'rime.dll') -Force
    if ($shared) {
        $data = Join-Path $hostDir 'data'
        if ($data -ine $shared) {
            New-Item -ItemType Directory -Path $data -Force | Out-Null
            foreach ($entry in Get-ChildItem -LiteralPath $shared) {
                Copy-Item -LiteralPath $entry.FullName -Destination $data -Recurse -Force
            }
        }
    }
} finally {
    Pop-Location
}
