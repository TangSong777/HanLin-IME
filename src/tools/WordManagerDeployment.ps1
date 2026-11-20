function Get-WordManagerTarget([string]$ToolRoot) {
    $nearby = Split-Path $ToolRoot -Parent
    $nativeRoot = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\HanLin' -ErrorAction SilentlyContinue).InstallRoot
    $weaselRoot = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\WOW6432Node\Rime\Weasel' -ErrorAction SilentlyContinue).WeaselRoot
    if (-not $weaselRoot) {
        $weaselRoot = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Rime\Weasel' -ErrorAction SilentlyContinue).WeaselRoot
    }

    $runningRoots = @(foreach ($name in 'WeaselServer', 'HanLinHost') {
        foreach ($process in Get-Process -Name $name -ErrorAction SilentlyContinue) {
            if ($process.SessionId -eq [Diagnostics.Process]::GetCurrentProcess().SessionId -and $process.Path) {
                Split-Path $process.Path -Parent
            }
        }
    })
    $install = @(@($nearby) + $runningRoots + @($nativeRoot, $weaselRoot) | Where-Object {
        $_ -and ([IO.File]::Exists((Join-Path $_ 'HanLinHost.exe')) -or
                [IO.File]::Exists((Join-Path $_ 'WeaselDeployer.exe')))
    } | Select-Object -First 1)[0]
    if (-not $install) { throw '找不到寒淋输入法的安装目录。' }

    $hostPath = Join-Path $install 'HanLinHost.exe'
    $preset = Join-Path $install 'data\preset'
    if ([IO.File]::Exists($hostPath)) {
        $backend = 'HanLin'
        $userDir = (Get-ItemProperty -LiteralPath 'HKCU:\SOFTWARE\HanLin' -ErrorAction SilentlyContinue).UserDataDir
        if (-not $userDir) { $userDir = Join-Path $env:APPDATA 'HanLin' }
        $deployer = $hostPath
    } else {
        $backend = 'Weasel'
        $userDir = (Get-ItemProperty -LiteralPath 'HKCU:\SOFTWARE\Rime\Weasel' -ErrorAction SilentlyContinue).RimeUserDir
        if (-not $userDir) { $userDir = Join-Path $env:APPDATA 'Rime' }
        $hostPath = Join-Path $install 'WeaselServer.exe'
        $deployer = Join-Path $install 'WeaselDeployer.exe'
    }
    $userDir = [IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($userDir))
    $generator = Join-Path $ToolRoot 'gen-mixed-tokens.ps1'
    $lua = Join-Path $install 'data\lua'
    if (-not [IO.File]::Exists($generator)) {
        $generator = Join-Path $nearby 'build-support\gen-mixed-tokens.ps1'
        $lua = Join-Path (Split-Path $nearby -Parent) 'rime-config\lua'
    }
    [pscustomobject]@{
        Backend = $backend
        Directory = $userDir
        Phrase = (Join-Path $userDir 'custom_phrase.txt')
        Preset = $preset
        HostPath = $hostPath
        Deployer = $deployer
        Generator = $generator
        Lua = $lua
    }
}

function Initialize-WordManagerTable($Target) {
    $seed = Join-Path $Target.Preset 'custom_phrase.txt'
    if (-not [IO.File]::Exists($seed) -or [IO.File]::Exists($Target.Phrase)) { return }
    $lock = Lock-WordTable $Target.Phrase
    try {
        if (-not [IO.File]::Exists($Target.Phrase)) {
            Write-WordFile $Target.Phrase ([IO.File]::ReadAllBytes($seed))
        }
    } finally {
        $lock.ReleaseMutex()
        $lock.Dispose()
    }
}

function New-WordManagerProcess([string]$Executable, [string[]]$Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new($Executable)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WorkingDirectory = Split-Path $Executable -Parent
    $start.Arguments = ($Arguments | ForEach-Object {
        if ($_ -and $_ -notmatch '\s') { $_ }
        else { '"' + ($_ -replace '(\\+)$', '$1$1') + '"' }
    }) -join ' '
    $start
}

function Invoke-WordManagerProcess([string]$Executable, [string[]]$Arguments) {
    $process = [Diagnostics.Process]::Start((New-WordManagerProcess $Executable $Arguments))
    try {
        $process.WaitForExit()
        return $process.ExitCode
    } finally { $process.Dispose() }
}

function Invoke-WordManagerCompiler($Target) {
    if ($Target.Backend -eq 'Weasel') {
        $result = Invoke-WordManagerProcess $Target.Deployer @('/deploy')
    } else {
        $result = Invoke-WordManagerProcess $Target.Deployer @('--stop')
        if ($result -ne 0) { throw '寒淋服务尚未退出，无法编译词表。' }
        $arguments = @('--deploy', '--user-data', $Target.Directory)
        $result = Invoke-WordManagerProcess $Target.Deployer $arguments
        if ($result -eq 3) {
            $stopped = Invoke-WordManagerProcess $Target.Deployer @('--stop')
            if ($stopped -eq 0) { $result = Invoke-WordManagerProcess $Target.Deployer $arguments }
        }
    }
    if ($result -ne 0) { throw "词库编译失败，退出码 $result。" }
    $compiled = Join-Path $Target.Directory 'build\rime_ice.schema.yaml'
    if (-not [IO.File]::Exists($compiled) -or (Get-Item -LiteralPath $compiled).Length -eq 0) {
        throw '词库编译后缺少雾凇拼音配置。'
    }
}

function Invoke-WordManagerDeployment($Target, [string]$ExpectedRevision) {
    if (-not [IO.File]::Exists($Target.Generator)) { throw '缺少造句词表生成器。' }
    if (-not [IO.File]::Exists($Target.Deployer)) { throw '缺少输入法部署程序。' }

    $lock = Lock-WordTable $Target.Phrase
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ("hanlin-words." + [guid]::NewGuid().ToString('N'))
    $phraseStage = "$temporary.txt"
    $mixedStage = "$temporary.dict.yaml"
    $updates = [ordered]@{}
    $written = [Collections.Generic.List[object]]::new()
    $backup = Join-Path $Target.Directory ('hanlin-word-backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6))
    $compileStarted = $false
    try {
        if ($PSBoundParameters.ContainsKey('ExpectedRevision') -and
            (Get-WordTableRevision $Target.Phrase) -cne $ExpectedRevision) {
            throw '词表已有新改动，请重新读取后部署。'
        }
        [string[]]$lines = @(if ([IO.File]::Exists($Target.Phrase)) { [IO.File]::ReadAllLines($Target.Phrase) })
        $patchFile = Join-Path $Target.Directory 'rime_ice.custom.yaml'
        $patchText = if ([IO.File]::Exists($patchFile)) {
            [IO.File]::ReadAllText($patchFile)
        } else {
            $presetPatch = Join-Path $Target.Preset 'rime_ice.custom.yaml'
            if ([IO.File]::Exists($presetPatch)) { [IO.File]::ReadAllText($presetPatch) } else { '' }
        }
        $patched = Update-PhraseTranslatorPatch $patchText
        if ($patched -cne $patchText) {
            foreach ($module in 'autocap_filter.lua', 'custom_phrase_case.lua', 'short_prefix_filter.lua', 'lowercase_candidate_filter.lua') {
                $source = Join-Path $Target.Lua $module
                $updates[(Join-Path $Target.Directory "lua\$module")] = [IO.File]::ReadAllBytes($source)
            }
            $updates[$patchFile] = [Text.Encoding]::UTF8.GetBytes($patched)
        }
        $folded = $patched.Contains('lua_translator@*custom_phrase_case@custom_phrase_complete')
        $normalized = if ($folded) { @(Get-NormalizedWordTable $lines) } else { $lines }
        Save-WordTable $phraseStage $normalized
        if ($folded -and ($lines -join "`n") -cne ($normalized -join "`n")) {
            $updates[$Target.Phrase] = [IO.File]::ReadAllBytes($phraseStage)
        }
        & $Target.Generator -CustomPhrase $phraseStage -Out $mixedStage
        $updates[(Join-Path $Target.Directory 'mixed_tokens.dict.yaml')] = [IO.File]::ReadAllBytes($mixedStage)

        foreach ($path in $updates.Keys) {
            $existed = [IO.File]::Exists($path)
            if ($existed -and [Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -ceq
                              [Convert]::ToBase64String($updates[$path])) { continue }
            $relative = $path.Substring($Target.Directory.TrimEnd('\').Length).TrimStart('\')
            $saved = Join-Path $backup $relative
            if ($existed) { Write-WordFile $saved ([IO.File]::ReadAllBytes($path)) }
            Write-WordFile $path $updates[$path]
            $written.Add([pscustomobject]@{ Path = $path; Saved = $saved; Existed = $existed })
        }

        $compileStarted = $true
        Invoke-WordManagerCompiler $Target
        if ($folded) {
            $builtPatch = [IO.File]::ReadAllText((Join-Path $Target.Directory 'build\rime_ice.schema.yaml'))
            if (-not $builtPatch.Contains('lua_translator@*custom_phrase_case@custom_phrase_complete')) {
                throw '编译后的词库没有载入英文大小写匹配组件。'
            }
        }
        [pscustomobject]@{
            Count = @(Get-WordRows $normalized | Group-Object Key -CaseSensitive).Count
            Backup = if ([IO.Directory]::Exists($backup)) { $backup } else { '' }
        }
    } catch {
        $failure = $_.Exception.Message
        for ($index = $written.Count - 1; $index -ge 0; --$index) {
            $file = $written[$index]
            if ($file.Existed) { Write-WordFile $file.Path ([IO.File]::ReadAllBytes($file.Saved)) }
            elseif ([IO.File]::Exists($file.Path)) { [IO.File]::Delete($file.Path) }
        }
        if ($compileStarted -and $written.Count -and $Target.Backend -eq 'Weasel') {
            try { Invoke-WordManagerCompiler $Target }
            catch { $failure += " 恢复前一次词库编译也失败。备份位于 $backup。" }
        }
        throw $failure
    } finally {
        try {
            foreach ($path in $phraseStage, $mixedStage) {
                if ([IO.File]::Exists($path)) { [IO.File]::Delete($path) }
            }
            if ($compileStarted -and [IO.File]::Exists($Target.HostPath)) {
                $processName = [IO.Path]::GetFileNameWithoutExtension($Target.HostPath)
                if (-not (Get-Process -Name $processName -ErrorAction SilentlyContinue)) {
                    Start-Process -FilePath $Target.HostPath -WorkingDirectory (Split-Path $Target.HostPath -Parent) -WindowStyle Hidden
                }
            }
        } finally {
            $lock.ReleaseMutex()
            $lock.Dispose()
        }
    }
}
