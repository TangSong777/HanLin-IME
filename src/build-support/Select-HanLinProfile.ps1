[CmdletBinding()]
param([Parameter(Mandatory)][string]$Backup)

$ErrorActionPreference = 'Stop'
$hanlin = '0804:{0857A4AE-7B77-47F3-98EC-FC98F5980111}{0EDFAE44-206D-4A8C-89FE-52873569623A}'
$legacy = '0804:{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}{3D02CAB6-2B8E-4781-BA20-1C9267529467}'
$before = Get-WinUserLanguageList
$list = Get-WinUserLanguageList
$chinese = @($list | Where-Object { $_.LanguageTag -like 'zh-Hans*' -or $_.LanguageTag -eq 'zh-CN' })
if (-not $chinese.Count) { throw '当前用户没有简体中文语言项。' }
$previousDefault = [Microsoft.Win32.Registry]::GetValue(
    'HKEY_CURRENT_USER\Control Panel\International\User Profile', 'InputMethodOverride', $null)
$replaceDefault = $previousDefault -ieq $legacy
@{
    languages = @($before | Select-Object LanguageTag, InputMethodTips, Handwriting, Spellchecking)
    inputMethodTip = $previousDefault
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Backup 'input-methods.json') -Encoding UTF8

$modified = $false
if (-not ($chinese[0].InputMethodTips -contains $hanlin)) {
    $chinese[0].InputMethodTips.Add($hanlin)
    $modified = $true
}
foreach ($language in $list) {
    if ($language.InputMethodTips.Remove($legacy)) { $modified = $true }
}
try {
    if ($modified) { Set-WinUserLanguageList -LanguageList $list -Force }
    if ($replaceDefault) { Set-WinDefaultInputMethodOverride -InputTip $hanlin }
    $updated = Get-WinUserLanguageList
    $observed = foreach ($language in $updated) { $language.InputMethodTips }
    if ($observed -notcontains $hanlin -or $observed -contains $legacy) {
        throw '输入法清单与切换目标不一致。'
    }
} catch {
    if ($modified) { Set-WinUserLanguageList -LanguageList $before -Force }
    if ($replaceDefault) { Set-WinDefaultInputMethodOverride -InputTip $previousDefault }
    throw
}
