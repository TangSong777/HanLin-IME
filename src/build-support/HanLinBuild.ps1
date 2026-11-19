function Get-HanLinVersion([string]$Installer) {
    $line = Select-String -LiteralPath $Installer -Pattern '^#define MyAppVersion "([0-9]+\.[0-9]+\.[0-9]+)"$'
    if (-not $line) { throw "Missing MyAppVersion in $Installer" }
    $line.Matches[0].Groups[1].Value
}

function Get-HanLinContentFingerprint([string]$Root, [string[]]$Files) {
    $relativePaths = [string[]]@(foreach ($file in $Files) {
        [IO.Path]::GetRelativePath($Root, $file).Replace('\', '/')
    })
    [Array]::Sort($relativePaths, [StringComparer]::Ordinal)
    $entries = foreach ($relative in $relativePaths) {
        $hash = (Get-FileHash -LiteralPath (Join-Path $Root $relative) -Algorithm SHA256).Hash
        "$hash $relative"
    }
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        [BitConverter]::ToString($sha256.ComputeHash([Text.Encoding]::UTF8.GetBytes($entries -join "`n"))).Replace('-', '')
    } finally {
        $sha256.Dispose()
    }
}

function Get-HanLinSourceFingerprint([string]$SourceRoot) {
    $repo = Split-Path -Parent $SourceRoot
    $files = @(
        Get-ChildItem -LiteralPath (Join-Path $SourceRoot HanLin),(Join-Path $SourceRoot include),
            (Join-Path $repo 'reference\librime-reference\include') -File -Recurse | Select-Object -ExpandProperty FullName
        Join-Path $SourceRoot 'build-hanlin.ps1'
        Join-Path $SourceRoot 'build-support\Find-Toolchain.ps1'
        Join-Path $SourceRoot 'build-support\HanLinBuild.ps1'
        Join-Path $SourceRoot 'WeaselTSF\HostCompatibility.h'
        Join-Path $SourceRoot 'installer\HanLin.iss'
        Join-Path $repo 'reference\librime-reference\src\rime_api.h'
    )
    Get-HanLinContentFingerprint $repo $files
}

function Get-HanLinBuildArtifacts {
    [ordered]@{
        'HanLinHost.exe' = 'hanlin-tip-x64\HanLinHost.exe'
        'x64\HanLinTip.dll' = 'hanlin-tip-x64\HanLinTip.dll'
        'x86\HanLinTip.dll' = 'hanlin-tip-x86\HanLinTip.dll'
        'rime.dll' = 'hanlin-tip-x64\rime.dll'
    }
}
