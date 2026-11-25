#Requires -Version 7.0
param(
    [string]$EngineDll = (Join-Path $PSScriptRoot 'dist\rime.dll'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot 'output'),
    [string]$SharedDataDir,
    [switch]$BuildRegression
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-support\Find-Toolchain.ps1')
. (Join-Path $PSScriptRoot 'build-support\HanLinBuild.ps1')
$cmake = (Get-Command cmake.exe -CommandType Application).Source
$boost = Get-BoostRoot
$source = Join-Path $PSScriptRoot 'HanLin'
$engine = (Get-Item -LiteralPath $EngineDll).FullName
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputRoot)
$version = Get-HanLinVersion (Join-Path $PSScriptRoot 'installer\HanLin.iss')
$sourceFingerprint = Get-HanLinSourceFingerprint $PSScriptRoot
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
            ('-DHANLIN_BUILD_HOST=ON -DBOOST_ROOT="{0}"' -f $boost)
        } else { '-DHANLIN_BUILD_HOST=OFF' }
        $regression = if ($BuildRegression) { 'ON' } else { 'OFF' }
        $command = 'call "{0}" >nul && "{1}" -S "{2}" -B "{3}" -GNinja -DCMAKE_BUILD_TYPE=Release -DHANLIN_BUILD_REGRESSION={4} {5} && "{1}" --build "{3}"' -f $vcvars, $cmake, $source, $build, $regression, $options
        & cmd.exe /d /c $command
        if ($LASTEXITCODE -ne 0) { throw "HanLin $architecture build failed ($LASTEXITCODE)" }
    }
    $hostDir = Join-Path $output 'hanlin-tip-x64'
    Copy-Item -LiteralPath $engine -Destination (Join-Path $hostDir 'rime.dll') -Force
    $data = Join-Path $hostDir 'data'
    if ($shared) {
        if ($data -ine $shared) {
            New-Item -ItemType Directory -Path $data -Force | Out-Null
            foreach ($entry in Get-ChildItem -LiteralPath $shared) {
                Copy-Item -LiteralPath $entry.FullName -Destination $data -Recurse -Force
            }
        }
    }
    New-Item -ItemType Directory -Path $data -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\rime-config\hanlin.yaml') -Destination $data -Force
} finally {
    Pop-Location
}

if ((Get-HanLinSourceFingerprint $PSScriptRoot) -cne $sourceFingerprint) {
    throw 'HanLin build inputs changed during compilation. Rebuild before packaging.'
}
$hashes = [ordered]@{}
foreach ($artifact in (Get-HanLinBuildArtifacts).GetEnumerator()) {
    $hashes[$artifact.Key] = (Get-FileHash -LiteralPath (Join-Path $output $artifact.Value)).Hash
}
$manifest = [ordered]@{
    version = $version
    sourceFingerprint = $sourceFingerprint
    artifacts = $hashes
}
$manifestFile = Join-Path $output 'HanLinBuild.json'
$pendingManifest = "$manifestFile.$([Guid]::NewGuid().ToString('N')).tmp"
[IO.File]::WriteAllText($pendingManifest, ($manifest | ConvertTo-Json))
[IO.File]::Move($pendingManifest, $manifestFile, $true)
