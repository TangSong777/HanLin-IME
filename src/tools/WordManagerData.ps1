function Test-EnglishWord([string]$Word) {
    $Word -cmatch '^[A-Za-z0-9]+$' -and $Word -cmatch '[A-Za-z]'
}

function Get-WordKey([string]$Word, [string]$Code) {
    if ((Test-EnglishWord $Word) -and $Code -cmatch '^[A-Za-z][A-Za-z0-9]*$') {
        return "$($Word.ToLowerInvariant())`t$($Code.ToLowerInvariant())"
    }
    "$Word`t$Code"
}

function Get-WordRows([string[]]$Lines) {
    for ($i = 0; $i -lt $Lines.Count; ++$i) {
        if ($Lines[$i] -cmatch '^([^#\t][^\t]*)\t([^\t]+)(\t.*)?$') {
            $word = $Matches[1]
            $code = $Matches[2].Trim()
            $suffix = $Matches[3]
            $foldCase = (Test-EnglishWord $word) -and $code -cmatch '^[A-Za-z][A-Za-z0-9]*$'
            if ($foldCase) { $code = $code.ToLowerInvariant() }
            $weight = if ($suffix) { ($suffix.Substring(1) -split '\t', 2)[0] } else { '' }
            [pscustomobject]@{ Word = $word; Code = $code; Key = (Get-WordKey $word $code); Suffix = $suffix; Weight = $weight; Line = $i }
        }
    }
}

function Read-WordEntries([string]$Path) {
    if (-not [IO.File]::Exists($Path)) { return }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($row in Get-WordRows ([IO.File]::ReadAllLines($Path))) {
        if ($seen.Add($row.Key)) { $row }
    }
}

function Get-WordTableRevision([string]$Path) {
    if ([IO.File]::Exists($Path)) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
    ''
}

function Lock-WordTable([string]$Path) {
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $fullPath = [IO.Path]::GetFullPath($Path).ToUpperInvariant()
        $key = [BitConverter]::ToString($sha256.ComputeHash([Text.Encoding]::UTF8.GetBytes($fullPath))).Replace('-', '')
    } finally { $sha256.Dispose() }
    $mutex = [Threading.Mutex]::new($false, "Local\HanLinWords.$key")
    try {
        if ($mutex.WaitOne(0)) { return $mutex }
    } catch [Threading.AbandonedMutexException] {
        return $mutex
    }
    $mutex.Dispose()
    throw '已有词条操作正在运行，请等当前操作完成。'
}

function Write-WordFile([string]$Path, [byte[]]$Bytes) {
    $Path = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    $temp = "$Path.$([guid]::NewGuid().ToString('N')).tmp"
    try {
        [IO.File]::WriteAllBytes($temp, $Bytes)
        if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temp, $Path, [NullString]::Value) }
        else { [IO.File]::Move($temp, $Path) }
    } finally {
        if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) }
    }
}

function Save-WordTable([string]$Path, [string[]]$Lines) {
    $text = if ($Lines.Count) { ($Lines -join "`r`n") + "`r`n" } else { '' }
    Write-WordFile $Path ([Text.Encoding]::UTF8.GetBytes($text))
}

function Get-NormalizedWordTable([string[]]$Lines) {
    $skip = [Collections.Generic.HashSet[int]]::new()
    $first = @{}
    foreach ($group in (Get-WordRows $Lines | Group-Object Key -CaseSensitive)) {
        $rows = @($group.Group)
        # Conflicting weights stay available for explicit editing.
        if (@($rows | ForEach-Object { [string]$_.Suffix } | Select-Object -Unique).Count -gt 1) { continue }
        $entry = $rows[0]
        $first[$entry.Line] = "$($entry.Word)`t$($entry.Code)$($entry.Suffix)"
        foreach ($row in $rows | Select-Object -Skip 1) { [void]$skip.Add($row.Line) }
    }
    for ($i = 0; $i -lt $Lines.Count; ++$i) {
        if ($skip.Contains($i)) { continue }
        if ($first.ContainsKey($i)) { $first[$i] } else { $Lines[$i] }
    }
}

function Normalize-WordTable([string]$Path) {
    $lock = Lock-WordTable $Path
    try {
        if (-not [IO.File]::Exists($Path)) { return }
        $lines = [IO.File]::ReadAllLines($Path)
        $output = @(Get-NormalizedWordTable $lines)
        if (($lines -join "`n") -cne ($output -join "`n")) { Save-WordTable $Path $output }
    } finally {
        $lock.ReleaseMutex()
        $lock.Dispose()
    }
}

function Edit-WordEntry {
    param(
        [string]$Path, [string]$Word, [string]$Code, [switch]$Remove,
        [string]$OriginalKey, [AllowEmptyString()][string]$Weight,
        [AllowEmptyString()][string]$ExpectedRevision, [switch]$AddOnly
    )

    $Word = $Word.Trim()
    $Code = $Code.Trim()
    if (-not $Code -and (Test-EnglishWord $Word) -and $Word -cmatch '^[A-Za-z]') { $Code = $Word.ToLowerInvariant() }
    if (-not $Word -or -not $Code -or $Word.StartsWith('#') -or "$Word$Code" -match '[\t\r\n]') {
        throw '请输入有效的词条和编码。'
    }
    if ((Test-EnglishWord $Word) -and $Code -cmatch '^[A-Za-z][A-Za-z0-9]*$') { $Code = $Code.ToLowerInvariant() }

    $key = Get-WordKey $Word $Code
    $matchKey = if ($OriginalKey) { $OriginalKey } else { $key }
    $lock = Lock-WordTable $Path
    try {
        if ($PSBoundParameters.ContainsKey('ExpectedRevision') -and
            (Get-WordTableRevision $Path) -cne $ExpectedRevision) {
            throw '词表已在别处更新，请重新读取后再编辑。'
        }
        [string[]]$lines = @(if ([IO.File]::Exists($Path)) { [IO.File]::ReadAllLines($Path) })
        $entries = @(Get-WordRows $lines)
        $rows = @($entries | Where-Object { $_.Key -ceq $matchKey })
        if (($OriginalKey -or $Remove) -and -not $rows.Count) {
            throw '这条词已不存在，请重新读取。'
        }
        if (($AddOnly -or $key -cne $matchKey) -and
            @($entries | Where-Object { $_.Key -ceq $key }).Count) {
            throw '这个词条和编码已经存在。'
        }

        $suffix = if ($rows.Count) { $rows[0].Suffix } else { '' }
        if ($PSBoundParameters.ContainsKey('Weight') -and -not $Remove) {
            $Weight = $Weight.Trim()
            if (-not $rows.Count -or $Weight -cne $rows[0].Weight) {
                $parsed = 0
                if ($Weight -and (-not [int]::TryParse($Weight, [ref]$parsed) -or $parsed -lt 0)) {
                    throw '权重请输入非负整数，留空使用默认权重。'
                }
                $suffix = if ($Weight) { "`t$parsed" } else { '' }
            }
        }
        $output = [Collections.Generic.List[string]]::new()
        $rowSet = [Collections.Generic.HashSet[int]]::new()
        foreach ($row in $rows) { [void]$rowSet.Add($row.Line) }
        for ($i = 0; $i -lt $lines.Count; ++$i) {
            if (-not $rowSet.Contains($i)) { $output.Add($lines[$i]) }
            elseif (-not $Remove -and $i -eq $rows[0].Line) { $output.Add("$Word`t$Code$suffix") }
        }
        if (-not $rows.Count) { $output.Add("$Word`t$Code$suffix") }
        Save-WordTable $Path $output.ToArray()
    } finally {
        $lock.ReleaseMutex()
        $lock.Dispose()
    }
}

function Update-PhraseTranslatorPatch([string]$Text) {
    foreach ($name in 'custom_phrase', 'custom_phrase_complete') {
        $old = '(?m)^([ \t]*-[ \t]*)table_translator@' + $name + '([ \t]*(?:#[^\r\n]*)?)(?=\r?$)'
        $new = 'lua_translator@*custom_phrase_case@' + $name
        $Text = [regex]::Replace($Text, $old, ('${1}' + $new + '${2}'))
    }
    $Text
}
