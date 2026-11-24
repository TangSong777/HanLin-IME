param(
    [switch]$Deploy,
    [string]$ExpectedTarget,
    [AllowEmptyString()][string]$ExpectedRevision
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'WordManagerData.ps1')
. (Join-Path $PSScriptRoot 'WordManagerDeployment.ps1')
if ($Deploy) {
    [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
    try {
        $target = Get-WordManagerTarget $PSScriptRoot
        if ($ExpectedTarget -and $target.Directory -ine $ExpectedTarget) {
            throw '当前词库路径已切换，请重新打开词条工具。'
        }
        Initialize-WordManagerTable $target
        $result = if ($PSBoundParameters.ContainsKey('ExpectedRevision')) {
            Invoke-WordManagerDeployment $target -ExpectedRevision $ExpectedRevision
        } else { Invoke-WordManagerDeployment $target }
        [Console]::WriteLine("已部署 $($result.Count) 条。")
        exit 0
    } catch {
        [Console]::Error.WriteLine($_.Exception.Message)
        exit 1
    }
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
try {
    $target = Get-WordManagerTarget $PSScriptRoot
    Initialize-WordManagerTable $target
} catch {
    [void][Windows.Forms.MessageBox]::Show($_.Exception.Message, '寒淋输入法')
    return
}

$paper = [Drawing.ColorTranslator]::FromHtml('#F8F8F6')
$panel = [Drawing.Color]::White
$ink = [Drawing.ColorTranslator]::FromHtml('#342D2E')
$muted = [Drawing.ColorTranslator]::FromHtml('#756966')
$line = [Drawing.ColorTranslator]::FromHtml('#E8DFDB')
$accent = [Drawing.ColorTranslator]::FromHtml('#A84049')
$accentHover = [Drawing.ColorTranslator]::FromHtml('#8D343D')
$selection = [Drawing.ColorTranslator]::FromHtml('#F4E2E0')
$uiFont = [Drawing.Font]::new('Microsoft YaHei UI', 10)
$headingFont = [Drawing.Font]::new('Microsoft YaHei UI', 19, [Drawing.FontStyle]::Bold)
$smallFont = [Drawing.Font]::new('Microsoft YaHei UI', 9)

function New-WordLabel([string]$Text, [Drawing.Color]$Color, [Drawing.Font]$Font) {
    $label = New-Object Windows.Forms.Label
    $label.Text = $Text
    $label.ForeColor = $Color
    $label.Font = $Font
    $label.Dock = 'Fill'
    $label.AutoEllipsis = $true
    $label.TextAlign = 'MiddleLeft'
    $label.Margin = [Windows.Forms.Padding]::new(0)
    $label
}

function New-WordButton([string]$Text, [switch]$Primary) {
    $button = New-Object Windows.Forms.Button
    $button.Text = $Text
    $button.Dock = 'Fill'
    $button.Margin = [Windows.Forms.Padding]::new(5, 4, 0, 4)
    $button.FlatStyle = 'Flat'
    $button.UseVisualStyleBackColor = $false
    $button.FlatAppearance.BorderColor = $line
    $button.FlatAppearance.MouseOverBackColor = $selection
    $button.BackColor = $panel
    $button.ForeColor = $ink
    if ($Primary) {
        $button.BackColor = $accent
        $button.ForeColor = [Drawing.Color]::White
        $button.FlatAppearance.BorderSize = 0
        $button.FlatAppearance.MouseOverBackColor = $accentHover
    }
    $button
}

function New-WordLayout([double[]]$Columns, [double[]]$Rows) {
    $layout = New-Object Windows.Forms.TableLayoutPanel
    $layout.Dock = 'Fill'
    $layout.Margin = [Windows.Forms.Padding]::new(0)
    $layout.ColumnCount = $Columns.Count
    $layout.RowCount = $Rows.Count
    foreach ($width in $Columns) {
        $style = if ($width -lt 0) { [Windows.Forms.ColumnStyle]::new('Percent', -$width) }
                 else { [Windows.Forms.ColumnStyle]::new('Absolute', $width) }
        [void]$layout.ColumnStyles.Add($style)
    }
    foreach ($height in $Rows) {
        $style = if ($height -lt 0) { [Windows.Forms.RowStyle]::new('Percent', -$height) }
                 else { [Windows.Forms.RowStyle]::new('Absolute', $height) }
        [void]$layout.RowStyles.Add($style)
    }
    $layout
}

function New-WordTextBox([string]$Name) {
    $box = New-Object Windows.Forms.TextBox
    $box.AccessibleName = $Name
    $box.Dock = 'Fill'
    $box.Margin = [Windows.Forms.Padding]::new(0, 4, 8, 0)
    $box.ForeColor = $ink
    $box.BackColor = $panel
    $box.BorderStyle = 'FixedSingle'
    $box
}

$form = New-Object Windows.Forms.Form
$form.Text = '寒淋输入法'
$form.ClientSize = [Drawing.Size]::new(844, 626)
$form.MinimumSize = [Drawing.Size]::new(680, 500)
$form.StartPosition = 'CenterScreen'
$form.AutoScaleMode = 'Dpi'
$form.BackColor = $paper
$form.ForeColor = $ink
$form.Font = $uiFont
$form.KeyPreview = $true
$layout = New-WordLayout @(-100) @(74, 44, -100, 103, 47)
$layout.Padding = [Windows.Forms.Padding]::new(18, 12, 18, 10)
$form.Controls.Add($layout)

$header = New-WordLayout @(53, -100) @(38, 24, -100)
$layout.Controls.Add($header, 0, 0)
$badge = New-Object Windows.Forms.PictureBox
$badge.Size = [Drawing.Size]::new(42, 42)
$badge.Anchor = 'Left'
$badge.SizeMode = 'Zoom'
$badge.Margin = [Windows.Forms.Padding]::new(0)
$iconFile = @(
    (Join-Path $PSScriptRoot 'hanlin.ico'),
    (Join-Path (Split-Path $PSScriptRoot -Parent) 'HanLin\icons\hanlin.ico')
) | Where-Object { [IO.File]::Exists($_) } | Select-Object -First 1
if ($iconFile) {
    $brandIcon = [Drawing.Icon]::new($iconFile, 48, 48)
    $form.Icon = $brandIcon
}
$badgeFile = Join-Path $PSScriptRoot 'hanlin-word-icon.png'
if ([IO.File]::Exists($badgeFile)) {
    $badgeSource = [Drawing.Image]::FromFile($badgeFile)
    try { $brandBitmap = [Drawing.Bitmap]::new($badgeSource) }
    finally { $badgeSource.Dispose() }
    $badge.Image = $brandBitmap
}
$header.Controls.Add($badge, 0, 0)
$header.SetRowSpan($badge, 2)
$header.Controls.Add((New-WordLabel '自定义词条' $ink $headingFont), 1, 0)
$header.Controls.Add((New-WordLabel $target.Phrase $muted $smallFont), 1, 1)

$tools = New-WordLayout @(52, -100, 78, 78, 92, 92) @(-100)
$layout.Controls.Add($tools, 0, 1)
$tools.Controls.Add((New-WordLabel '查找' $muted $uiFont), 0, 0)
$searchBox = New-WordTextBox '按词条或编码查找'
$searchBox.Margin = [Windows.Forms.Padding]::new(0, 8, 4, 0)
$tools.Controls.Add($searchBox, 1, 0)
$editButton = New-WordButton '编辑'
$deleteButton = New-WordButton '删除'
$reloadButton = New-WordButton '重新读取'
$tools.Controls.Add($editButton, 2, 0)
$tools.Controls.Add($deleteButton, 3, 0)
$tools.Controls.Add($reloadButton, 4, 0)
$statsButton = New-WordButton '打字统计'
$tools.Controls.Add($statsButton, 5, 0)

$tableFrame = New-Object Windows.Forms.Panel
$tableFrame.Dock = 'Fill'
$tableFrame.Margin = [Windows.Forms.Padding]::new(0, 5, 0, 6)
$tableFrame.Padding = [Windows.Forms.Padding]::new(1)
$tableFrame.BackColor = $line
$layout.Controls.Add($tableFrame, 0, 2)
$grid = New-Object Windows.Forms.DataGridView
$grid.Dock = 'Fill'
$grid.BackgroundColor = $panel
$grid.BorderStyle = 'None'
$grid.GridColor = $line
$grid.CellBorderStyle = 'SingleHorizontal'
$grid.ColumnHeadersBorderStyle = 'Single'
$grid.EnableHeadersVisualStyles = $false
$grid.ColumnHeadersHeight = 34
$grid.RowTemplate.Height = 29
$grid.RowHeadersVisible = $false
$grid.ReadOnly = $true
$grid.AllowUserToAddRows = $false
$grid.AllowUserToDeleteRows = $false
$grid.AllowUserToResizeRows = $false
$grid.MultiSelect = $false
$grid.SelectionMode = 'FullRowSelect'
$grid.AutoSizeColumnsMode = 'Fill'
$grid.ColumnHeadersDefaultCellStyle.BackColor = $selection
$grid.ColumnHeadersDefaultCellStyle.ForeColor = $ink
$grid.ColumnHeadersDefaultCellStyle.Font = $smallFont
$grid.ColumnHeadersDefaultCellStyle.SelectionBackColor = $selection
$grid.DefaultCellStyle.BackColor = $panel
$grid.DefaultCellStyle.ForeColor = $ink
$grid.DefaultCellStyle.SelectionBackColor = $selection
$grid.DefaultCellStyle.SelectionForeColor = $ink
$grid.AlternatingRowsDefaultCellStyle.BackColor = $paper
[void]$grid.Columns.Add('Word', '标准词')
[void]$grid.Columns.Add('Code', '编码')
[void]$grid.Columns.Add('Weight', '权重')
$grid.Columns['Word'].FillWeight = 50
$grid.Columns['Code'].FillWeight = 36
$grid.Columns['Weight'].FillWeight = 14
$tableFrame.Controls.Add($grid)

$editor = New-WordLayout @(-48, -36, -16, 108, 70) @(26, 36, -100)
$editor.BackColor = $panel
$editor.Padding = [Windows.Forms.Padding]::new(12, 7, 12, 4)
$layout.Controls.Add($editor, 0, 3)
$fieldLabels = @('标准词', '编码', '权重')
for ($column = 0; $column -lt $fieldLabels.Count; ++$column) {
    $editor.Controls.Add((New-WordLabel $fieldLabels[$column] $muted $smallFont), $column, 0)
}
$wordBox = New-WordTextBox '标准词'
$codeBox = New-WordTextBox '编码'
$weightBox = New-WordTextBox '权重'
$weightBox.MaxLength = 10
$editor.Controls.Add($wordBox, 0, 1)
$editor.Controls.Add($codeBox, 1, 1)
$editor.Controls.Add($weightBox, 2, 1)
$addButton = New-WordButton '添加词条' -Primary
$cancelButton = New-WordButton '清空'
$editor.Controls.Add($addButton, 3, 1)
$editor.Controls.Add($cancelButton, 4, 1)
$help = New-WordLabel '英文编码留空时使用小写拼写。权重可留空。' $muted $smallFont
$editor.Controls.Add($help, 0, 2)
$editor.SetColumnSpan($help, 5)

$footer = New-WordLayout @(-100, 119) @(-100)
$layout.Controls.Add($footer, 0, 4)
$status = New-WordLabel '正在读取词条' $muted $smallFont
$footer.Controls.Add($status, 0, 0)
$deployButton = New-WordButton '部署词条' -Primary
$footer.Controls.Add($deployButton, 1, 0)
$script:worker = $null
$script:editKey = $null
$script:revision = ''
$script:entries = @()

function Show-WordStatus([string]$Message, [switch]$ErrorMessage) {
    $status.Text = $Message
    $status.ForeColor = if ($ErrorMessage) { $accent } else { $muted }
}

function Reset-WordEditor {
    $script:editKey = $null
    $addButton.Text = '添加词条'
    $cancelButton.Text = '清空'
    $wordBox.Clear()
    $codeBox.Clear()
    $weightBox.Clear()
}

function Get-SelectedWord {
    if ($grid.SelectedRows.Count -eq 1) { $grid.SelectedRows[0].Tag }
}

function Render-WordRows {
    $query = $searchBox.Text.Trim()
    $grid.SuspendLayout()
    try {
        $grid.Rows.Clear()
        foreach ($entry in $script:entries) {
            if ($query -and $entry.Word.IndexOf($query, [StringComparison]::OrdinalIgnoreCase) -lt 0 -and
                            $entry.Code.IndexOf($query, [StringComparison]::OrdinalIgnoreCase) -lt 0) { continue }
            $i = $grid.Rows.Add($entry.Word, $entry.Code, $entry.Weight)
            $grid.Rows[$i].Tag = $entry
        }
        $grid.ClearSelection()
    } finally { $grid.ResumeLayout() }
    $count = if ($query) { "找到 $($grid.Rows.Count) 条" } else { "共 $($script:entries.Count) 条词条" }
    Show-WordStatus $count
}

function Read-WordManagerGrid {
    $script:revision = Get-WordTableRevision $target.Phrase
    $script:entries = @(Read-WordEntries $target.Phrase | Sort-Object Word, Code)
    Render-WordRows
}

function Show-StatsWindow {
    $statsPath = Join-Path $target.Directory 'stats.log'
    if (-not (Test-Path $statsPath)) {
        [void][Windows.Forms.MessageBox]::Show(
            "还没有统计数据。`n`n请先在 rime_ice.custom.yaml 的 engine/processors 里启用 stats_recorder（可选功能，默认关闭）。",
            '寒淋输入法')
        return
    }
    $daily = @{}
    foreach ($statsLine in (Get-Content $statsPath -Encoding UTF8)) {
        $parts = $statsLine -split "`t"
        if ($parts.Count -lt 2) { continue }
        $date = $parts[0].Trim()
        $n = 0
        if (-not [int]::TryParse($parts[1].Trim(), [ref]$n)) { continue }
        if (-not $daily.ContainsKey($date)) { $daily[$date] = 0 }
        $daily[$date] += $n
    }
    $today = Get-Date -Format 'yyyy-MM-dd'
    $todayCount = if ($daily.ContainsKey($today)) { $daily[$today] } else { 0 }
    $weekStart = (Get-Date).AddDays(-(([int](Get-Date).DayOfWeek + 6) % 7)).ToString('yyyy-MM-dd')
    $weekCount = 0
    foreach ($k in $daily.Keys) { if ($k -ge $weekStart) { $weekCount += $daily[$k] } }
    $monthStart = (Get-Date -Format 'yyyy-MM') + '-01'
    $monthCount = 0
    foreach ($k in $daily.Keys) { if ($k -ge $monthStart) { $monthCount += $daily[$k] } }
    $total = 0
    foreach ($v in $daily.Values) { $total += $v }

    $statsForm = New-Object Windows.Forms.Form
    $statsForm.Text = '打字统计'
    $statsForm.Size = [Drawing.Size]::new(520, 520)
    $statsForm.StartPosition = 'CenterParent'
    $statsForm.BackColor = $paper
    $statsForm.Font = $uiFont

    $statsLayout = New-WordLayout @(-100) @(70, 70, 70, 70, -100)
    $statsLayout.Padding = [Windows.Forms.Padding]::new(18, 14, 18, 14)
    $statsForm.Controls.Add($statsLayout)

    $cards = @(
        @('今日', $todayCount),
        @('本周', $weekCount),
        @('本月', $monthCount),
        @('总计', $total)
    )
    for ($i = 0; $i -lt $cards.Count; ++$i) {
        $card = New-Object Windows.Forms.Panel
        $card.Dock = 'Fill'
        $card.BackColor = $panel
        $card.Margin = [Windows.Forms.Padding]::new(4)
        $card.Padding = [Windows.Forms.Padding]::new(12, 8, 12, 8)
        $cardLayout = New-WordLayout @(-100) @(-100, -100)
        $card.Controls.Add($cardLayout)
        $cardLayout.Controls.Add((New-WordLabel $cards[$i][0] $muted $smallFont), 0, 0)
        $cardLayout.Controls.Add((New-WordLabel ([string]$cards[$i][1]) $accent $headingFont), 0, 1)
        $statsLayout.Controls.Add($card, 0, $i)
    }

    $recent = New-Object Windows.Forms.DataGridView
    $recent.Dock = 'Fill'
    $recent.BackgroundColor = $panel
    $recent.BorderStyle = 'None'
    $recent.GridColor = $line
    $recent.CellBorderStyle = 'SingleHorizontal'
    $recent.ColumnHeadersBorderStyle = 'Single'
    $recent.EnableHeadersVisualStyles = $false
    $recent.ColumnHeadersHeight = 30
    $recent.RowTemplate.Height = 26
    $recent.RowHeadersVisible = $false
    $recent.ReadOnly = $true
    $recent.AllowUserToAddRows = $false
    $recent.AllowUserToDeleteRows = $false
    $recent.AllowUserToResizeRows = $false
    $recent.MultiSelect = $false
    $recent.SelectionMode = 'FullRowSelect'
    $recent.AutoSizeColumnsMode = 'Fill'
    $recent.ColumnHeadersDefaultCellStyle.BackColor = $selection
    $recent.ColumnHeadersDefaultCellStyle.ForeColor = $ink
    $recent.ColumnHeadersDefaultCellStyle.Font = $smallFont
    $recent.DefaultCellStyle.BackColor = $panel
    $recent.DefaultCellStyle.ForeColor = $ink
    $recent.DefaultCellStyle.SelectionBackColor = $selection
    $recent.DefaultCellStyle.SelectionForeColor = $ink
    [void]$recent.Columns.Add('Date', '日期')
    [void]$recent.Columns.Add('Count', '字数')
    $recent.Columns['Date'].FillWeight = 60
    $recent.Columns['Count'].FillWeight = 40
    $statsLayout.Controls.Add($recent, 0, 4)

    $sorted = $daily.Keys | Sort-Object -Descending | Select-Object -First 14
    foreach ($d in $sorted) {
        [void]$recent.Rows.Add($d, $daily[$d])
    }

    [void]$statsForm.ShowDialog($form)
}

function Start-WordEdit {
    $entry = Get-SelectedWord
    if (-not $entry -or $script:worker) { return }
    $script:editKey = $entry.Key
    $wordBox.Text = $entry.Word
    $codeBox.Text = $entry.Code
    $weightBox.Text = $entry.Weight
    $addButton.Text = '保存修改'
    $cancelButton.Text = '取消'
    [void]$wordBox.Focus()
}

$editButton.Enabled = $deleteButton.Enabled = $false
$grid.Add_SelectionChanged({
    $editButton.Enabled = $deleteButton.Enabled = ($grid.SelectedRows.Count -eq 1 -and -not $script:worker)
})
$grid.Add_CellDoubleClick({ param($sender, $eventArgs) if ($eventArgs.RowIndex -ge 0) { Start-WordEdit } })
$searchBox.Add_TextChanged({ Render-WordRows })
$editButton.Add_Click({ Start-WordEdit })
$cancelButton.Add_Click({ Reset-WordEditor })
$statsButton.Add_Click({ Show-StatsWindow })
$reloadButton.Add_Click({
    try {
        Reset-WordEditor
        Read-WordManagerGrid
    } catch { Show-WordStatus $_.Exception.Message -ErrorMessage }
})
$addButton.Add_Click({
    try {
        $change = @{
            Path = $target.Phrase; Word = $wordBox.Text; Code = $codeBox.Text; Weight = $weightBox.Text
            OriginalKey = $script:editKey; ExpectedRevision = $script:revision; AddOnly = -not $script:editKey
        }
        Edit-WordEntry @change
        Reset-WordEditor
        Read-WordManagerGrid
        Show-WordStatus '已保存。点击「部署词条」应用改动。'
    } catch { Show-WordStatus $_.Exception.Message -ErrorMessage }
})
$deleteButton.Add_Click({
    $entry = Get-SelectedWord
    if (-not $entry) { return }
    $answer = [Windows.Forms.MessageBox]::Show($form, "删除「$($entry.Word)」的编码 $($entry.Code)？",
        '删除词条', 'YesNo', 'Warning', 'Button2')
    if ($answer -ne [Windows.Forms.DialogResult]::Yes) { return }
    try {
        Edit-WordEntry -Path $target.Phrase -Word $entry.Word -Code $entry.Code -Remove -ExpectedRevision $script:revision
        Reset-WordEditor
        Read-WordManagerGrid
        Show-WordStatus '已删除。点击「部署词条」应用改动。'
    } catch { Show-WordStatus $_.Exception.Message -ErrorMessage }
})

$timer = New-Object Windows.Forms.Timer
$timer.Interval = 250
$timer.Add_Tick({
    if (-not $script:worker -or -not $script:worker.HasExited) { return }
    $timer.Stop()
    [void]$script:workerOutput.Result
    $diagnostic = $script:workerError.Result.Trim()
    $exitCode = $script:worker.ExitCode
    $script:worker.Dispose()
    $script:worker = $null
    $tools.Enabled = $editor.Enabled = $grid.Enabled = $deployButton.Enabled = $true
    if ($exitCode -eq 0) {
        try {
            Read-WordManagerGrid
            Show-WordStatus "已部署 $($script:entries.Count) 条词条。"
        } catch { Show-WordStatus $_.Exception.Message -ErrorMessage }
    } else {
        if (-not $diagnostic) { $diagnostic = "部署失败，退出码 $exitCode。" }
        Show-WordStatus $diagnostic -ErrorMessage
        [void][Windows.Forms.MessageBox]::Show($form, $diagnostic, '部署失败', 'OK', 'Error')
    }
})
$script:managerPath = $PSCommandPath
$deployButton.Add_Click({
    try {
        $shellExe = [Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
        $arguments = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script:managerPath,
            '-Deploy', '-ExpectedTarget', $target.Directory, '-ExpectedRevision', $script:revision)
        $start = New-WordManagerProcess $shellExe $arguments
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $start.StandardOutputEncoding = [Text.Encoding]::UTF8
        $start.StandardErrorEncoding = [Text.Encoding]::UTF8
        $script:worker = [Diagnostics.Process]::Start($start)
        $script:workerOutput = $script:worker.StandardOutput.ReadToEndAsync()
        $script:workerError = $script:worker.StandardError.ReadToEndAsync()
        $tools.Enabled = $editor.Enabled = $grid.Enabled = $deployButton.Enabled = $false
        Show-WordStatus '正在编译并部署词条…'
        $timer.Start()
    } catch { Show-WordStatus $_.Exception.Message -ErrorMessage }
})
$form.Add_FormClosing({
    param($sender, $eventArgs)
    if ($script:worker) { $eventArgs.Cancel = $true }
})
$form.Add_KeyDown({
    param($sender, $eventArgs)
    if ($eventArgs.Control -and $eventArgs.KeyCode -eq 'F') {
        [void]$searchBox.Focus()
        $searchBox.SelectAll()
        $eventArgs.SuppressKeyPress = $true
    }
})

Read-WordManagerGrid
try { [void]$form.ShowDialog() }
finally {
    $timer.Dispose()
    $form.Dispose()
    if ($brandBitmap) { $brandBitmap.Dispose() }
    if ($brandIcon) { $brandIcon.Dispose() }
    $smallFont.Dispose()
    $headingFont.Dispose()
    $uiFont.Dispose()
}
