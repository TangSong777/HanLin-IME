#Requires -Version 7.0
<#
.SYNOPSIS
  寒淋输入法词库与配置的云同步（手动迁移，一期）。
.DESCRIPTION
  把本机 Rime 用户目录里的用户词库权重（sync/<installation_id>/*.userdb.txt）、词库本体
  （cn_dicts / en_dicts）、词库与皮肤资源（lua / opencc / Skin）以及配置（*.yaml、
  custom_phrase.txt、sogou_phrases.txt）同步到自建的 HanLin Sync 服务。
  同步不会上传 installation.yaml、user.yaml、stats.log、LevelDB 目录（*.userdb/）、
  构建缓存（build/）与备份文件（*.bak-* 等）。

  push：先调用 HanLinHost --sync-user-data 导出词频，再上传变更文件并提交一个新版本。
  pull：下载指定版本；词频写入 sync/<installation_id>/ 后由 HanLinHost 合并导入
        （librime 以词频合并，不覆盖 installation.yaml）；配置、词库与皮肤默认只落到
        暂存目录，用 -IncludeConfig 才会写入用户目录（先备份）——换机恢复时加该参数。

  令牌用 Windows DPAPI（CurrentUser）加密保存在 %LOCALAPPDATA%\HanLin-IME\sync-credential.bin，
  同步状态保存在 <UserData>\.hanlin-sync.json。

.PARAMETER Action
  pair（验证 Authenticator 动态码并获取设备令牌）、push、pull、restore（-Commit 指定历史版本）、status、history。
.PARAMETER UserData
  Rime 用户目录，默认 %APPDATA%\Rime。
.PARAMETER HostPath
  HanLinHost.exe 路径；默认依次查找脚本旁的 dist、安装目录与桌面部署目录。
.PARAMETER SharedData
  HanLin 共享数据目录（含 build/ 与词库）；默认取 HostPath 同级的 data。
.PARAMETER Server
  同步服务地址，默认 https://sync.yanghanlin.cn；只对 127.0.0.1 / localhost 允许 http。
.PARAMETER Code
  pair 时使用的 Authenticator 六位动态验证码。
.PARAMETER DeviceName
  设备显示名，默认当前计算机名。
.PARAMETER Commit
  restore 时要恢复的版本 id。
.PARAMETER IncludeConfig
  pull/restore 时同时写入配置、词库与皮肤文件（先备份，再按相对路径覆盖），
  换机部署时使用；写入后需要在寒淋输入法里重新部署。
.PARAMETER Force
  远端已有其他设备的提交时，仍以远端当前版本为父提交上传（会覆盖远端内容）。
.EXAMPLE
  pwsh -File src\Sync-HanLin.ps1 pair
.EXAMPLE
  pwsh -File src\Sync-HanLin.ps1 push
.EXAMPLE
  pwsh -File src\Sync-HanLin.ps1 pull -IncludeConfig
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory, Position = 0)]
  [ValidateSet('pair', 'push', 'pull', 'restore', 'status', 'history', 'sync', 'unpair')]
  [string]$Action,

  [string]$UserData = (Join-Path $env:APPDATA 'Rime'),
  [string]$HostPath,
  [string]$SharedData,
  [string]$Server = 'https://sync.yanghanlin.cn',
  [string]$DeviceName = $env:COMPUTERNAME,
  [string]$Commit,
  [switch]$IncludeConfig,
  [switch]$Force,
  [switch]$PairFromStdin
)

$guard = $null
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:App = 'ime'
$script:StateFile = Join-Path $UserData '.hanlin-sync.json'
$script:CredFile = Join-Path $env:LOCALAPPDATA 'HanLin-IME\sync-credential.bin'
$script:Server = $Server.TrimEnd('/')
# 同步范围：用户词频（sync/<id>/*.userdb.txt）、词库本体与资源（cn_dicts / en_dicts /
# lua / opencc / Skin）、以及配置（*.yaml、常用语）。机器相关文件（installation.yaml、
# user.yaml、stats.log）、LevelDB 用户库（*.userdb/）与可重建的 build/ 不上云。
$script:SyncDirs = @('cn_dicts', 'en_dicts', 'lua', 'opencc', 'Skin', 'skins')
$script:SyncSkipNames = @('installation.yaml', 'user.yaml')
$script:SyncSkipPattern = '(?i)(\.bak(-[0-9A-Za-z]+)?|\.pre-[0-9a-f-]+|~|\.tmp)$'
$script:SyncFileLimit = 90MB

function Write-Step([string]$Text) { Write-Host "== $Text" -ForegroundColor Cyan }

function Fail([string]$Text) {
  throw [InvalidOperationException]::new($Text)
}

function Get-InstallationId {
  $yaml = Join-Path $UserData 'installation.yaml'
  if (-not (Test-Path -LiteralPath $yaml)) { Fail "找不到 $yaml，请先运行一次寒淋输入法或指定正确的 -UserData。" }
  foreach ($line in Get-Content -LiteralPath $yaml -Encoding UTF8) {
    if ($line -match '^\s*installation_id:\s*"?([0-9a-fA-F-]{8,64})"?\s*$') { return $Matches[1] }
  }
  Fail "无法从 $yaml 读取 installation_id。"
}

function Resolve-HostPath {
  if ($HostPath) {
    if (-not (Test-Path -LiteralPath $HostPath)) { Fail "找不到 HanLinHost：$HostPath" }
    return (Resolve-Path -LiteralPath $HostPath).Path
  }
  $candidates = @(
    (Join-Path $PSScriptRoot 'dist\HanLinHost.exe'),
    (Join-Path $env:ProgramFiles 'HanLin\HanLinHost.exe'),
    (Join-Path $env:USERPROFILE 'Desktop\HanLin-IME\HanLinHost.exe'),
    'D:\桌面\HanLin-IME\HanLinHost.exe'
  )
  foreach ($item in $candidates) {
    if ($item -and (Test-Path -LiteralPath $item)) { return (Resolve-Path -LiteralPath $item).Path }
  }
  Fail '找不到 HanLinHost.exe，请用 -HostPath 指定。'
}

function Resolve-SharedData([string]$HostExe) {
  if ($SharedData) {
    if (-not (Test-Path -LiteralPath $SharedData -PathType Container)) { Fail "共享数据目录不存在：$SharedData" }
    return (Resolve-Path -LiteralPath $SharedData).Path
  }
  $sibling = Join-Path (Split-Path -Parent $HostExe) 'data'
  if (Test-Path -LiteralPath (Join-Path $sibling 'build')) { return (Resolve-Path -LiteralPath $sibling).Path }
  $installed = Join-Path $env:ProgramFiles 'HanLin\data'
  if (Test-Path -LiteralPath (Join-Path $installed 'build')) { return $installed }
  Fail '找不到共享数据目录（含 build/），请用 -SharedData 指定。'
}

function Read-Secret([string]$Path) {
  $bytes = [System.IO.File]::ReadAllBytes($Path)
  $plain = [System.Security.Cryptography.ProtectedData]::Unprotect($bytes, $null, 'CurrentUser')
  try { return [Text.Encoding]::UTF8.GetString($plain) }
  finally { [Array]::Clear($plain, 0, $plain.Length) }
}

function Write-Secret([string]$Path, [string]$Value) {
  $dir = Split-Path -Parent $Path
  if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
  $plain = [Text.Encoding]::UTF8.GetBytes($Value)
  try {
    $blob = [System.Security.Cryptography.ProtectedData]::Protect($plain, $null, 'CurrentUser')
    $temp = $Path + "." + [guid]::NewGuid().ToString('N') + ".tmp"
    try {
      [System.IO.File]::WriteAllBytes($temp, $blob)
      [System.IO.File]::Move($temp, $Path, $true)
    } finally { [System.IO.File]::Delete($temp) }
  }
  finally { [Array]::Clear($plain, 0, $plain.Length) }
}

function Get-State {
  if (-not (Test-Path -LiteralPath $script:StateFile)) { return $null }
  return Get-Content -LiteralPath $script:StateFile -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Save-State($State) {
  $json = $State | ConvertTo-Json -Depth 6
  $temp = $script:StateFile + "." + [guid]::NewGuid().ToString('N') + ".tmp"
  try {
    [System.IO.File]::WriteAllText($temp, $json, [Text.UTF8Encoding]::new($false))
    [System.IO.File]::Move($temp, $script:StateFile, $true)
  } finally { [System.IO.File]::Delete($temp) }
}

function Get-Token {
  if (-not (Test-Path -LiteralPath $script:CredFile)) { return $null }
  return (Read-Secret $script:CredFile)
}

function Assert-Https {
  $uri = $null
  if (-not [Uri]::TryCreate($script:Server, [UriKind]::Absolute, [ref]$uri) -or
      $uri.UserInfo -or $uri.Query -or $uri.Fragment -or $uri.AbsolutePath -ne '/' -or
      ($uri.Scheme -ne 'https' -and -not ($uri.Scheme -eq 'http' -and $uri.IsLoopback))) {
    Fail '请使用 HTTPS 服务器根地址；仅本机测试支持 HTTP。地址不能包含用户名、路径或查询参数。'
  }
  $script:Server = $uri.GetLeftPart([UriPartial]::Authority)
}

function Invoke-Api {
  param(
    [string]$Method,
    [string]$Path,
    $Body,
    [string]$InFile,
    [string]$OutFile,
    [switch]$NoAuth
  )
  Assert-Https
  $uri = $script:Server + $Path
  $headers = @{ 'Accept' = 'application/json' }
  if (-not $NoAuth) {
    $bound = Get-State
    if (-not $bound -or [string]$bound.server -cne $script:Server) {
      Fail '这台设备没有在当前服务器配对，请先配对。'
    }
    $token = Get-Token
    if (-not $token) { Fail '设备凭据不存在，请重新配对。' }
    $headers['Authorization'] = "Bearer $token"
  }
  $params = @{
    Method             = $Method
    Uri                = $uri
    Headers            = $headers
    SkipHttpErrorCheck = $true
    MaximumRedirection = 0
    TimeoutSec         = 120
  }
  if ($PSBoundParameters.ContainsKey('Body')) {
    $params['Body'] = ($Body | ConvertTo-Json -Depth 8 -Compress)
    $params['ContentType'] = 'application/json'
  }
  if ($InFile) { $params['InFile'] = $InFile; $params['ContentType'] = 'application/octet-stream' }
  # -OutFile 本身不返回对象（$null），-PassThru 让调用方仍能拿到 StatusCode 做错误判断。
  if ($OutFile) { $params['OutFile'] = $OutFile; $params['PassThru'] = $true }
  return Invoke-WebRequest @params
}

function Get-ErrorCode($Response) {
  try {
    $body = $Response.Content | ConvertFrom-Json
    if ($body.error) { return [string]$body.error }
  } catch { }
  return 'http_' + [int]$Response.StatusCode
}

function Get-RemoteHead {
  $resp = Invoke-Api -Method GET -Path "/v1/apps/$($script:App)/commits/latest"
  if ([int]$resp.StatusCode -eq 404) { return $null }
  if ([int]$resp.StatusCode -ne 200) { Fail ("读取远端最新版本失败：" + (Get-ErrorCode $resp)) }
  return ($resp.Content | ConvertFrom-Json)
}

function Get-RemoteCommit([string]$Id) {
  if ($Id -notmatch '^[0-9a-f]{16,64}$') { Fail '无效的云端版本编号。' }
  $resp = Invoke-Api -Method GET -Path "/v1/apps/$($script:App)/commits/$Id"
  if ([int]$resp.StatusCode -ne 200) { Fail ("读取版本 $Id 失败：" + (Get-ErrorCode $resp)) }
  $commit = $resp.Content | ConvertFrom-Json
  # 单版本接口给 files，列表接口给 fileCount；这里补齐，调用方统一按 fileCount 展示。
  $count = @($commit.files).Count
  $commit | Add-Member -NotePropertyName fileCount -NotePropertyValue $count -Force
  return $commit
}

function Get-Sha256([string]$Path) {
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-RelativeSyncPath([string]$Relative) {
  if ([string]::IsNullOrWhiteSpace($Relative)) { Fail '清单里有空路径。' }
  if ($Relative -match '\\' -or $Relative -match ':' -or $Relative.StartsWith('/')) { Fail "非法同步路径：$Relative" }
  $parts = $Relative.Split('/')
  if ($parts -contains '..' -or $parts -contains '.' -or $parts -contains '') { Fail "非法同步路径：$Relative" }
  $reserved = '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?$'
  $allowed = $false
  if ($parts.Count -eq 1) {
    $allowed = ($Relative -match '\.(yaml|yml)$' -or $Relative -in 'custom_phrase.txt','sogou_phrases.txt')
  } elseif ($parts.Count -eq 2) {
    $allowed = ($parts[0] -in $script:SyncDirs)
  } elseif ($parts.Count -eq 3) {
    $allowed = ($Relative -match '^sync/[a-zA-Z0-9_-]{8,64}/[^/]+\.userdb\.txt$')
  }
  if (-not $allowed -or $parts[-1] -in $script:SyncSkipNames -or $parts[-1] -match $script:SyncSkipPattern) {
    Fail "同步范围以外的路径：$Relative"
  }
  foreach ($part in $parts) {
    if ($part -match $reserved) { Fail "非法同步路径（Windows 保留名）：$Relative" }
    if ($part -match '[<>|"?*\x00-\x1f]' -or $part -match '\.$' -or $part -match ' $') { Fail "非法同步路径（尾部点或空格）：$Relative" }
  }
}

function Assert-RegularPath([string]$Path) {
  for ($item = [IO.Path]::GetFullPath($Path); $item; $item = [IO.Path]::GetDirectoryName($item)) {
    if ([IO.File]::Exists($item) -or [IO.Directory]::Exists($item)) {
      if ([IO.File]::GetAttributes($item) -band [IO.FileAttributes]::ReparsePoint) {
        Fail '云同步路径包含符号链接或目录联接。'
      }
    }
  }
}

function Copy-AtomicFile([string]$Source, [string]$Destination) {
  Assert-RegularPath $Destination
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination)) | Out-Null
  $temp = $Destination + "." + [guid]::NewGuid().ToString('N') + ".tmp"
  try {
    [IO.File]::Copy($Source, $temp, $false)
    [IO.File]::Move($temp, $Destination, $true)
  } finally {
    [IO.File]::Delete($temp)
  }
}

function Assert-UserDbSnapshot([string]$Relative, [string]$Path) {
  if ($Relative -notlike 'sync/*/*.userdb.txt') { return }
  $parts = $Relative.Split('/')
  $db = $parts[2].Substring(0, $parts[2].Length - '.userdb.txt'.Length)
  $metadata = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
  $keys = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  $reader = [IO.StreamReader]::new($Path, [Text.UTF8Encoding]::new($false, $true), $false)
  try {
    while (($line = $reader.ReadLine()) -ne $null) {
      if ($line.StartsWith([string][char]0xFEFF, [StringComparison]::Ordinal)) { $line = $line.Substring(1) }
      if ($line.StartsWith('#@/')) {
        $fields = $line.Split("`t", 2)
        if ($fields.Count -ne 2 -or -not $metadata.TryAdd($fields[0], $fields[1])) {
          Fail "词频快照元数据重复或损坏：$Relative"
        }
      } elseif ($line -and -not $line.StartsWith('#')) {
        $fields = $line.Split("`t")
        if ($fields.Count -ne 3 -or -not $fields[0].Trim() -or -not $fields[1] -or
            $fields[2] -cnotmatch '^c=(-?\d+) d=([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?) t=(\d+)$') {
          Fail "词频快照条目格式错误：$Relative"
        }
        $commits = 0
        $weight = 0.0
        $tick = [uint32]0
        if (-not [int]::TryParse($Matches[1], [ref]$commits) -or
            -not [double]::TryParse($Matches[2], [Globalization.NumberStyles]::Float,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$weight) -or
            -not [double]::IsFinite($weight) -or -not [uint32]::TryParse($Matches[3], [ref]$tick)) {
          Fail "词频快照权重超出可读取范围：$Relative"
        }
        $code = $fields[0]
        if (-not $code.EndsWith(' ')) { $code += ' ' }
        if (-not $keys.Add($code + "`t" + $fields[1])) { Fail "词频快照含重复编码和词条：$Relative" }
      }
    }
  } finally { $reader.Dispose() }
  if ($metadata['#@/db_type'] -cne 'userdb' -or $metadata['#@/db_name'] -cne $db -or
      $metadata['#@/user_id'] -ine $parts[1] -or $metadata['#@/tick'] -notmatch '^\d+$') {
    Fail "词频快照标识与清单不匹配：$Relative"
  }
}

function New-SyncEntry([string]$Rel, [string]$Full) {
  $item = Get-Item -LiteralPath $Full -ErrorAction SilentlyContinue
  if (-not $item -or $item.PSIsContainer) { return $null }
  if ($item.Name -match $script:SyncSkipPattern) { return $null }
  if ($script:SyncSkipNames -contains $item.Name) { return $null }
  Assert-RelativeSyncPath $Rel
  Assert-RegularPath $Full
  if ($item.Length -gt $script:SyncFileLimit) {
    Fail "同步文件超过 90 MiB：$Rel"
  }
  Assert-UserDbSnapshot $Rel $item.FullName
  return [pscustomobject]@{ Path = $Rel; FullName = $item.FullName; Sha256 = (Get-Sha256 $item.FullName); Size = $item.Length }
}

function Get-SyncFiles([string]$InstallId, [switch]$WordsOnly, [switch]$AllowEmpty) {
  $files = @()
  $root = (Resolve-Path -LiteralPath $UserData).Path
  if (-not $WordsOnly) {
  # 配置：顶层 *.yaml（含 *.custom.yaml、schema、dict.yaml）与常用语。
  foreach ($pattern in @('*.yaml', '*.yml', 'custom_phrase.txt', 'sogou_phrases.txt')) {
    foreach ($item in Get-ChildItem -LiteralPath $root -Filter $pattern -File) {
      $entry = New-SyncEntry $item.Name $item.FullName
      if ($entry) { $files += $entry }
    }
  }
  # 词库本体与资源：cn_dicts / en_dicts / lua / opencc / Skin / skins。
  foreach ($dir in $script:SyncDirs) {
    $path = Join-Path $root $dir
    if (-not (Test-Path -LiteralPath $path -PathType Container)) { continue }
    foreach ($item in Get-ChildItem -LiteralPath $path -File) {
      $entry = New-SyncEntry ("$dir/" + $item.Name) $item.FullName
      if ($entry) { $files += $entry }
    }
  }
  }
  # 用户词频：sync/<installation_id>/*.userdb.txt（由 HanLinHost --sync-user-data 导出）。
  $syncDir = Join-Path $root 'sync'
  if (Test-Path -LiteralPath $syncDir) {
    Assert-RegularPath $syncDir
    foreach ($source in Get-ChildItem -LiteralPath $syncDir -Directory) {
      if ($source.Name -notmatch '^[a-zA-Z0-9_-]{8,64}$') { continue }
      Assert-RegularPath $source.FullName
      foreach ($item in Get-ChildItem -LiteralPath $source.FullName -Filter '*.userdb.txt' -File) {
        $entry = New-SyncEntry ("sync/" + $source.Name + "/" + $item.Name) $item.FullName
        if ($entry) { $files += $entry }
      }
    }
  }
  if (-not $files -and -not $AllowEmpty) { Fail "在 $UserData 里没有找到可同步的词库或配置，请先运行一次 HanLinHost --sync-user-data。" }
  return $files
}

function Invoke-HostSync {
  param([switch]$AllowEmpty)
  $hostExe = Resolve-HostPath
  $shared = Resolve-SharedData $hostExe
  Write-Step '同步本地词库'
  $reportPath = Join-Path $UserData 'sync-report.json'
  [IO.File]::Delete($reportPath)
  $start = [Diagnostics.ProcessStartInfo]::new($hostExe)
  $start.CreateNoWindow = $true
  $start.UseShellExecute = $false
  foreach ($arg in @('--sync-user-data', $UserData, '--shared-data', $shared)) { $start.ArgumentList.Add($arg) }
  $proc = [Diagnostics.Process]::Start($start)
  $proc.WaitForExit()
  $report = $null
  if (Test-Path -LiteralPath $reportPath) {
    try { $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json } catch { }
  }
  $detail = ''
  if ($report) { $detail = [string]$report.error }
  if ($proc.ExitCode -ne 0 -or -not ($report -and $report.ok)) {
    if ($AllowEmpty -and $detail -like '*exported no *.userdb.txt*') {
      Write-Host '  本机还没有用户词库（还没输入过词条），这次没有词频可导出或导入。' -ForegroundColor Yellow
      return $null
    }
    Fail ("本地词库同步失败（exit=$($proc.ExitCode)）：$detail")
  }
  Write-Host ("  导出 " + $report.files.Count + " 个文件，" + $report.totalBytes + " 字节。")
  return $report
}

function Invoke-Pair {
  $code = if ($PairFromStdin) { [Console]::ReadLine() } else { Read-Host 'Authenticator 六位动态验证码' -MaskInput }
  $code = ([string]$code).Trim()
  $request = if ($code -cmatch '^[0-9]{6}$') {
    @{ app = $script:App; totp = $code; name = $DeviceName }
  } elseif ($code -cmatch '^[A-Za-z0-9-]{8,32}$') {
    @{ app = $script:App; code = $code.ToUpperInvariant(); name = $DeviceName }
  } else { Fail '请输入 Authenticator 的六位动态验证码。' }
  $resp = Invoke-Api -NoAuth -Method POST -Path '/v1/pair' -Body $request
  if ([int]$resp.StatusCode -notin 200,201) {
    $errorCode = Get-ErrorCode $resp
    $message = switch ($errorCode) {
      'totp_required' { '请输入 Authenticator 的六位动态验证码。' }
      'totp_invalid' { '动态验证码不正确，请核对手机上的当前验证码。' }
      'totp_reused' { '这个验证码已经用过，请等下一个。' }
      { $_ -in 'totp_unavailable','bad_code' } { '服务器尚未启用动态验证码配对。' }
      'too_many_failures' { '尝试次数过多，请稍后再试。' }
      default { "配对失败：$errorCode" }
    }
    Fail $message
  }
  $body = $resp.Content | ConvertFrom-Json
  if ($body.app -ne $script:App -or -not $body.token) { Fail '服务器没有签发寒淋输入法的设备令牌。' }
  Write-Secret $script:CredFile ([string]$body.token)
  $state = [pscustomobject]@{
    server      = $script:Server
    app         = $script:App
    deviceId    = [string]$body.deviceId
    deviceName  = $DeviceName
    base        = $null
    lastSync    = $null
    pendingMerge = $false
    installationId = (Get-InstallationId)
  }
  Save-State $state
  Write-Host ("已配对：设备 " + $body.deviceId + "（" + $DeviceName + "）。令牌已用 DPAPI 保存在 " + $script:CredFile)
}

function Invoke-Push {
  $state = Get-State
  if (-not $state) { Fail '尚未配对：先运行 pair。' }
  $installId = Get-InstallationId
  $null = Get-SyncFiles $installId -WordsOnly -AllowEmpty
  $null = Invoke-HostSync
  $files = @(Get-SyncFiles $installId -WordsOnly:(-not $IncludeConfig))
  Write-Step ("准备上传 " + $files.Count + " 个文件")
  $grouped = $files | ForEach-Object { $parts = $_.Path -split '/'; [pscustomobject]@{ Group = $(if ($parts.Count -gt 1) { $parts[0] } else { '(根目录)' }); Size = $_.Size } } | Group-Object Group | Sort-Object Name
  foreach ($group in $grouped) { Write-Host ("  " + $group.Name + "：" + $group.Count + " 个文件，" + (($group.Group | Measure-Object Size -Sum).Sum) + " 字节") }
  foreach ($file in @($files | Where-Object { $_.Size -ge 1MB } | Sort-Object Size -Descending)) {
    Write-Host ("    " + $file.Path + "  " + $file.Size + " B  " + $file.Sha256.Substring(0, 12))
  }

  $files = @($files | Sort-Object -Property Path -CaseSensitive)
  $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  $manifest = foreach ($file in $files) {
    if (-not $seen.Add($file.Path)) { Fail "词库清单含重复路径：$($file.Path)" }
    [ordered]@{ path = $file.Path; sha256 = $file.Sha256; size = $file.Size }
  }
  $manifest = @($manifest)
  $head = Get-RemoteHead
  $parent = $null
  if ($head) {
    $previous = Get-RemoteCommit ([string]$head.id)
    $indexed = @{}
    foreach ($entry in $previous.files) { $indexed[[string]$entry.path] = $entry }
    $same = $manifest.Count -eq $indexed.Count
    foreach ($entry in $manifest) {
      $oldEntry = $indexed[$entry.path]
      if (-not $oldEntry -or $entry.path -cne $oldEntry.path -or
          $entry.sha256 -cne $oldEntry.sha256 -or $entry.size -ne $oldEntry.size) { $same = $false; break }
    }
    if ($same) {
      $state.base = [string]$head.id
      $state | Add-Member -NotePropertyName pendingMerge -NotePropertyValue $false -Force
      Save-State $state
      Write-Host '内容未发生变化，无需传输或创建新版本。'
      return
    }
    if ([string]$state.base -cne [string]$head.id -and -not $Force) {
      Fail ("远端已有其他设备的更新（head=" + $head.id + "）。请先同步合并词频。")
    }
    $parent = [string]$head.id
  }

  $sent = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  foreach ($file in $files) {
    if (-not $sent.Add($file.Sha256)) { continue }
    if ((Get-Item -LiteralPath $file.FullName).Length -ne $file.Size -or
        (Get-Sha256 $file.FullName) -cne $file.Sha256) { Fail "同步文件在准备上传时发生变化：$($file.Path)" }
    $uri = "/v1/apps/$($script:App)/blobs/" + $file.Sha256
    $resp = Invoke-Api -Method PUT -Path $uri -InFile $file.FullName
    if ([int]$resp.StatusCode -notin 200, 201) { Fail ("上传 " + $file.Path + " 失败：" + (Get-ErrorCode $resp)) }
  }
  $commitBody = [ordered]@{
    parent = $parent
    note   = "HanLin-IME"
    files  = $manifest
  }
  $identity = $state.deviceId + "`n" + ($commitBody | ConvertTo-Json -Depth 8 -Compress)
  $digest = [Security.Cryptography.SHA256]::Create()
  try {
    $bytes = $digest.ComputeHash([Text.Encoding]::UTF8.GetBytes($identity))
    $commitBody.opId = [BitConverter]::ToString($bytes).Replace('-', '').ToLowerInvariant()
  } finally { $digest.Dispose() }
  $resp = Invoke-Api -Method POST -Path "/v1/apps/$($script:App)/commits" -Body $commitBody
  if ([int]$resp.StatusCode -notin 200, 201) { Fail ("提交失败：" + (Get-ErrorCode $resp)) }
  $created = $resp.Content | ConvertFrom-Json
  $state.base = [string]$created.id
  $state | Add-Member -NotePropertyName pendingMerge -NotePropertyValue $false -Force
  $state.lastSync = (Get-Date).ToString('o')
  Save-State $state
  Write-Host ("已提交版本 " + $created.id + "（" + $created.fileCount + " 个文件，" + $created.bytes + " 字节）。")
}

function Backup-LocalSyncState([string]$Tag, [switch]$Full) {
  $stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
  $dir = Join-Path $UserData (".hanlin-sync-backup-" + $stamp + "-" + $Tag + "-" + [guid]::NewGuid().ToString("N").Substring(0,8))
  New-Item -ItemType Directory -Path $dir -Force | Out-Null
  Assert-RegularPath $dir
  foreach ($name in @('sync','.hanlin-sync.json')) {
    $source = Join-Path $UserData $name
    if (Test-Path -LiteralPath $source) {
      Assert-RegularPath $source
      Copy-Item -LiteralPath $source -Destination $dir -Recurse -Force
    }
  }
  $databases = @(Get-ChildItem -LiteralPath $UserData -Filter '*.userdb' -Directory)
  foreach ($entry in $databases) {
    Assert-RegularPath $entry.FullName
    foreach ($child in Get-ChildItem -LiteralPath $entry.FullName -File -Force) {
      Assert-RegularPath $child.FullName
    }
    Copy-Item -LiteralPath $entry.FullName -Destination $dir -Recurse -Force
  }
  if ($Full) {
    foreach ($file in Get-SyncFiles (Get-InstallationId)) {
      if ($file.Path -notlike 'sync/*') {
        Copy-AtomicFile $file.FullName (Join-Path $dir ($file.Path -replace '/', '\'))
      }
    }
  }
  return $dir
}

function Invoke-Pull([string]$TargetCommit, [switch]$ApplyConfig) {
  $state = Get-State
  if (-not $state) { Fail '尚未配对：先运行 pair。' }
  $installId = Get-InstallationId
  if (-not $TargetCommit) {
    $head = Get-RemoteHead
    if (-not $head) { Fail '云端还没有同步内容。' }
    if (-not $ApplyConfig -and $state.base -and [string]$state.base -eq [string]$head.id -and
        -not ($state.PSObject.Properties['pendingMerge'] -and $state.pendingMerge)) { return }
    $TargetCommit = [string]$head.id
  }
  $commit = Get-RemoteCommit $TargetCommit
  $profile = [IO.Path]::GetFullPath($UserData)
  $stageRoot = Join-Path $profile '.hanlin-sync-staging'
  Assert-RegularPath $stageRoot
  $stage = Join-Path $stageRoot ([guid]::NewGuid().ToString('N'))
  [IO.Directory]::CreateDirectory($stage) | Out-Null
  $changes = [Collections.Generic.List[string]]::new()
  $backup = $null
  $mergeAttempted = $false

  try {
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $downloads = @{}
    $changed = [Collections.Generic.List[object]]::new()
    $pending = ($state.PSObject.Properties['pendingMerge'] -and $state.pendingMerge)
    $applyEntries = @($commit.files | Where-Object { $ApplyConfig -or $_.path -like 'sync/*/*.userdb.txt' })
    foreach ($entry in $applyEntries) {
      $rel = [string]$entry.path
      Assert-RelativeSyncPath $rel
      if (-not $seen.Add($rel) -or [string]$entry.sha256 -notmatch '^[0-9a-f]{64}$' -or
          [int64]$entry.size -lt 0 -or [int64]$entry.size -gt $script:SyncFileLimit) {
        Fail '服务端返回了无效的词库文件清单。'
      }
      if ($rel -like "sync/$installId/*") { continue }
      $path = $rel -replace '/', '\'
      $destination = Join-Path $profile $path
      Assert-RegularPath $destination
      if ([IO.File]::Exists($destination) -and (Get-Item -LiteralPath $destination).Length -eq [int64]$entry.size -and
          (Get-Sha256 $destination) -ceq [string]$entry.sha256) {
        Assert-UserDbSnapshot $rel $destination
        continue
      }
      $download = Join-Path $stage $path
      [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($download)) | Out-Null
      if ($downloads.ContainsKey($entry.sha256)) {
        [IO.File]::Copy($downloads[$entry.sha256], $download, $false)
      } else {
        $resp = Invoke-Api -Method GET -Path ("/v1/apps/$($script:App)/blobs/" + $entry.sha256) -OutFile $download
        if ([int]$resp.StatusCode -ne 200) { Fail ("下载失败：" + (Get-ErrorCode $resp)) }
      }
      if ((Get-Item -LiteralPath $download).Length -ne [int64]$entry.size -or
          (Get-Sha256 $download) -cne [string]$entry.sha256) { Fail '云端文件未通过大小或摘要校验。' }
      Assert-UserDbSnapshot $rel $download
      $downloads[$entry.sha256] = $download
      $changed.Add($entry)
    }

    if ($changed.Count -or $pending) {
      $backup = Backup-LocalSyncState 'pull' -Full:$ApplyConfig
      Write-Host "已备份本机词库：$backup"
    }
    $wordFiles = 0
    foreach ($entry in $changed) {
      $rel = [string]$entry.path
      $path = $rel -replace '/', '\'
      Copy-AtomicFile (Join-Path $stage $path) (Join-Path $profile $path)
      $changes.Add($path)
      if ($rel -like 'sync/*') { $wordFiles++ }
    }
    if ($wordFiles -or $pending) {
      $null = Get-SyncFiles $installId -WordsOnly -AllowEmpty
      $mergeAttempted = $true
      $merged = Invoke-HostSync -AllowEmpty
      if (-not $merged) {
        $state | Add-Member -NotePropertyName pendingMerge -NotePropertyValue $true -Force
        Save-State $state
        Write-Host '词频已下载，等待合并。请先用本机输入法选词，再重新同步。'
        return
      }
    }
    $state.base = [string]$commit.id
    $state | Add-Member -NotePropertyName pendingMerge -NotePropertyValue $false -Force
    $state.lastSync = (Get-Date).ToString('o')
    Save-State $state
    Write-Host ("已同步版本 " + $commit.id + "，本机安装标识保持原样。")
  }
  catch {
    $failure = $_
    if ($backup) {
      foreach ($path in $changes) {
        $original = Join-Path $backup $path
        $destination = Join-Path $profile $path
        if ([IO.File]::Exists($original)) { Copy-AtomicFile $original $destination }
        else { [IO.File]::Delete($destination) }
      }
      if ($mergeAttempted) {
        foreach ($entry in Get-ChildItem -LiteralPath $profile -Directory -Filter '*.userdb') {
          $destination = $entry.FullName
          Assert-RegularPath $destination
          $park = $destination + ".failed-sync-" + [guid]::NewGuid().ToString('N')
          [IO.Directory]::Move($destination, $park)
        }
        foreach ($entry in Get-ChildItem -LiteralPath $backup -Directory -Filter '*.userdb') {
          $destination = Join-Path $profile $entry.Name
          Copy-Item -LiteralPath $entry.FullName -Destination $destination -Recurse -Force
        }
      }
    }
    throw $failure
  }
  finally {
    if ([IO.Directory]::GetParent($stage).FullName -ieq $stageRoot -and [IO.Directory]::Exists($stage)) {
      Assert-RegularPath $stage
      Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    }
  }
}

function Show-Status {
  $state = Get-State
  Write-Host "用户目录：$UserData"
  if (-not $state) {
    Write-Host "同步状态：未配对"
    return
  }
  $installId = Get-InstallationId
  Write-Host "installation_id：$installId"
  Write-Host "服务器：$($state.server)"
  Write-Host "设备：$($state.deviceId)（$($state.deviceName)）"
  Write-Host "本地基线：$([string]$state.base)"
  $head = Get-RemoteHead
  if ($head) { Write-Host "远端最新：$($head.id)  $($head.at)  $($head.fileCount) 个文件" } else { Write-Host "远端最新：空库" }
  $syncDir = Join-Path $UserData ("sync\" + $installId)
  if (Test-Path -LiteralPath $syncDir) {
    $local = @(Get-ChildItem -LiteralPath $syncDir -Filter '*.userdb.txt' -File)
    $localBytes = if ($local.Count -gt 0) { ($local | Measure-Object -Property Length -Sum).Sum } else { 0 }
    Write-Host ("本地导出：" + $local.Count + " 个文件，" + $localBytes + " 字节")
    foreach ($item in $local) { Write-Host ("  " + $item.Name + "  " + $item.Length + " B") }
  } else {
    Write-Host "本地导出：尚未生成（先 push 一次）"
  }
  if ($state.lastSync) { Write-Host "上次同步：$($state.lastSync)" }
  if ($state.PSObject.Properties['pendingMerge'] -and $state.pendingMerge) {
    Write-Host '词频待合并：请先使用寒淋输入法输入并选词，再重试同步。'
  }
}

function Show-History {
  $resp = Invoke-Api -Method GET -Path "/v1/apps/$($script:App)/commits?limit=20"
  if ([int]$resp.StatusCode -ne 200) { Fail ("读取历史失败：" + (Get-ErrorCode $resp)) }
  $body = $resp.Content | ConvertFrom-Json
  $state = Get-State
  foreach ($item in $body.commits) {
    $mark = if ($state -and [string]$state.base -eq [string]$item.id) { ' *' } else { '' }
    Write-Host ("{0}  {1}  {2}  {3} 个文件{4}" -f $item.id, $item.at, $item.device, $item.fileCount, $mark)
  }
}

try {
  Assert-Https
  $guard = $null
  if ($Action -in 'push','pull','restore','sync') {
    Assert-RegularPath $UserData
    try {
      $guard = [IO.File]::Open((Join-Path $UserData '.hanlin-sync-operation.lock'),
        [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    } catch [IO.IOException] { Fail '这份用户词库正在被输入法或同步操作使用。退出使用该资料的后端后再试。' }
  }
  switch ($Action) {
    'pair' { Invoke-Pair }
    'push' { Invoke-Push }
    'pull' { Invoke-Pull -ApplyConfig:$IncludeConfig }
    'restore' {
      if (-not $Commit) { Fail 'restore 需要 -Commit <版本 id>。' }
      Invoke-Pull -TargetCommit $Commit -ApplyConfig:$IncludeConfig
    }
    'status' { Show-Status }
    'unpair' {
      $known = Get-State
      if ($known -and [string]$known.server -cne $script:Server) { Fail '服务器地址与配对记录不一致。' }
      if ($known) { [IO.File]::Delete($script:StateFile) }
      if ($known) { [IO.File]::Delete($script:CredFile) }
      Write-Host '已解除本机配对。'
    }
    'sync' {
      $known = Get-State
      if (-not $known) { Fail '请先配对。' }
      $latest = Get-RemoteHead
      if ($latest -and [string]$latest.id -cne [string]$known.base) {
        Invoke-Pull
        $known = Get-State
        if ($known.PSObject.Properties['pendingMerge'] -and $known.pendingMerge) { break }
      }
      Invoke-Push
    }
    'history' { Show-History }
  }
}
catch {
  [Console]::Error.WriteLine("错误：" + $_.Exception.Message)
  exit 1
}
finally {
  if ($guard) { $guard.Dispose() }
}
