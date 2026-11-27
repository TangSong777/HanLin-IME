using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace HanLin.Tools
{
    internal enum Action { None, Stats, Words, Settings, Data, CloudSync }

    internal sealed class StatsWindow : HanLinForm
    {
        private readonly HostActions host;
        private readonly Label[] totals = new Label[4];
        private readonly Label summary = Theme.Label("", Theme.Small, Theme.Muted);
        private readonly Label footer = Theme.Label("", Theme.Small, Theme.Muted);
        private readonly DataGridView rows = Theme.Grid();
        private readonly InkButton[] ranges = new InkButton[3];
        private readonly int[] days = { 7, 14, 30 };
        private TypingStats stats;
        private int range = 14;
        private long peak;

        internal StatsWindow(HostActions host) : base("打字统计", new Size(700, 610))
        {
            this.host = host;
            var layout = Theme.Layout(1, 28, 82, 46, 0, 28);
            layout.Controls.Add(summary, 0, 0);
            var metrics = Theme.Layout(4, 0);
            var names = new[] { "今日", "本周", "本月", "总计" };
            for (int i = 0; i < totals.Length; i++)
            {
                var tile = Theme.Layout(1, 26, 0);
                tile.BackColor = i == 0 ? Theme.Tint : Theme.Paper;
                tile.Margin = new Padding(0, 2, 7, 5);
                tile.Padding = new Padding(10, 5, 6, 5);
                tile.Controls.Add(Theme.Label(names[i], Theme.Small, Theme.Muted), 0, 0);
                totals[i] = Theme.Label("", Theme.Heading);
                tile.Controls.Add(totals[i], 0, 1);
                metrics.Controls.Add(tile, i, 0);
            }
            layout.Controls.Add(metrics, 0, 1);
            var toolbar = Theme.Layout(5, 0);
            toolbar.ColumnStyles[0] = new ColumnStyle(SizeType.Percent, 100);
            toolbar.Controls.Add(Theme.Label("每日趋势"), 0, 0);
            for (int i = 0; i < days.Length; i++)
            {
                int selected = days[i];
                ranges[i] = new InkButton(selected + " 天");
                ranges[i].Click += (sender, args) => { range = selected; RenderRange(); };
                toolbar.ColumnStyles[i + 1] = new ColumnStyle(SizeType.Absolute, 66);
                toolbar.Controls.Add(ranges[i], i + 1, 0);
            }
            var refresh = new InkButton("刷新");
            refresh.Click += (sender, args) => Reload();
            toolbar.ColumnStyles[4] = new ColumnStyle(SizeType.Absolute, 76);
            toolbar.Controls.Add(refresh, 4, 0);
            layout.Controls.Add(toolbar, 0, 2);
            Tips.AddGrid(rows);
            rows.AccessibleName = "每日打字统计";
            rows.Columns.Add("date", "日期");
            rows.Columns.Add("count", "字数");
            rows.Columns.Add("trend", "趋势");
            rows.Columns[0].FillWeight = 28;
            rows.Columns[1].FillWeight = 24;
            rows.Columns[2].FillWeight = 48;
            rows.Columns[1].DefaultCellStyle.Alignment = DataGridViewContentAlignment.MiddleRight;
            rows.CellPainting += DrawBar;
            layout.Controls.Add(rows, 0, 3);
            layout.Controls.Add(footer, 0, 4);
            Body.Controls.Add(layout);
            Reload();
        }

        private void Reload()
        {
            stats = host.Stats();
            var counts = new[] { stats.Today, stats.Week, stats.Month, stats.Total };
            for (int i = 0; i < totals.Length; i++)
                totals[i].Text = counts[i].ToString("N0");
            summary.Text = "截至 " + stats.Date.ToString("yyyy-MM-dd");
            RenderRange();
        }

        private void RenderRange()
        {
            for (int i = 0; i < ranges.Length; i++)
            {
                var button = ranges[i];
                button.Selected = days[i] == range;
                button.AccessibleDescription = button.Selected ? "当前时间范围" : "";
                button.Invalidate();
            }
            var dates = Enumerable.Range(0, range).Select(offset => stats.Date.AddDays(-offset)).ToArray();
            peak = dates.Select(day => stats.On(day)).Max();
            rows.Rows.Clear();
            foreach (DateTime day in dates)
            {
                long count = stats.On(day);
                int row = rows.Rows.Add(day.ToString("yyyy-MM-dd"), count.ToString("N0"), "");
                rows.Rows[row].Tag = count;
            }
            rows.ClearSelection();
            footer.Text = peak == 0 ? "这段时间暂无打字记录" : "近 " + range + " 天 · 单日最高 " + peak.ToString("N0") + " 字";
        }

        private void DrawBar(object sender, DataGridViewCellPaintingEventArgs e)
        {
            if (e.RowIndex < 0 || e.ColumnIndex != 2 || peak == 0 ||
                !(rows.Rows[e.RowIndex].Tag is long) || (long)rows.Rows[e.RowIndex].Tag == 0)
                return;
            e.PaintBackground(e.ClipBounds, true);
            var bar = Rectangle.Inflate(e.CellBounds, -Theme.Scale(10, DeviceDpi), -Theme.Scale(8, DeviceDpi));
            if (bar.Width > 0 && bar.Height > 0)
            {
                bar.Width = Math.Max(2, (int)Math.Round(bar.Width * ((long)rows.Rows[e.RowIndex].Tag / (double)peak)));
                using (var brush = new SolidBrush(Theme.Accent))
                using (var path = Theme.Rounded(bar, bar.Height / 2))
                    e.Graphics.FillPath(brush, path);
            }
            e.Paint(e.ClipBounds, DataGridViewPaintParts.Border | DataGridViewPaintParts.Focus);
            e.Handled = true;
        }
    }

    internal sealed class WordWindow : HanLinForm
    {
        private readonly HostActions host;
        private readonly DataGridView table = Theme.Grid();
        private readonly InkField search = Theme.Field("查找词条或编码");
        private readonly InkField word = Theme.Field("标准词");
        private readonly InkField code = Theme.Field("编码");
        private readonly InkField weight = Theme.Field("权重");
        private readonly Label status = Theme.Label("", Theme.Small, Theme.Muted);
        private readonly Label editorTitle = Theme.Label("添加词条", Theme.Heading);
        private readonly InkButton save = new InkButton("添加", true);
        private readonly InkButton delete = new InkButton("删除");
        private WordSnapshot snapshot;
        private WordEntry selected;
        private bool busy;

        internal WordWindow(HostActions host) : base("词库管理", new Size(860, 546))
        {
            Tips.AddGrid(table);
            this.host = host;
            host.Words.Seed(Path.Combine(host.Preset, "custom_phrase.txt"));
            var layout = Theme.Layout(1, 43, 0, 47);
            var top = Theme.Layout(3, 0);
            top.ColumnStyles[0] = new ColumnStyle(SizeType.Absolute, 132);
            top.ColumnStyles[1] = new ColumnStyle(SizeType.Percent, 100);
            top.ColumnStyles[2] = new ColumnStyle(SizeType.Absolute, 93);
            top.Controls.Add(Theme.Label("查找词条或编码", Theme.Small, Theme.Muted), 0, 0);
            top.Controls.Add(search, 1, 0);
            var reload = new InkButton("重新读取");
            Tips.Add(reload, "从数据文件夹重新读取自定义词条");
            reload.Click += (sender, args) => Act(Reload);
            top.Controls.Add(reload, 2, 0);
            layout.Controls.Add(top, 0, 0);

            var panes = Theme.Layout(2, 0);
            panes.ColumnStyles[0] = new ColumnStyle(SizeType.Percent, 100);
            panes.ColumnStyles[1] = new ColumnStyle(SizeType.Absolute, 250);
            table.Columns.Add("word", "标准词");
            table.Columns.Add("code", "编码");
            table.Columns.Add("weight", "权重");
            table.Columns[0].FillWeight = table.Columns[1].FillWeight = 43;
            table.Columns[2].FillWeight = 14;
            table.Margin = new Padding(0, 4, 8, 3);
            panes.Controls.Add(table, 0, 0);

            var editor = Theme.Layout(1, 45, 23, 34, 23, 34, 23, 34, 42, 28);
            editor.BackColor = Theme.Tint;
            editor.Margin = new Padding(4, 4, 0, 3);
            editor.Padding = new Padding(11, 5, 11, 5);
            editor.Controls.Add(editorTitle, 0, 0);
            editor.Controls.Add(Theme.Label("标准词"), 0, 1);
            editor.Controls.Add(word, 0, 2);
            editor.Controls.Add(Theme.Label("编码 · 英文可留空"), 0, 3);
            editor.Controls.Add(code, 0, 4);
            editor.Controls.Add(Theme.Label("权重 · 留空使用默认值"), 0, 5);
            editor.Controls.Add(weight, 0, 6);
            var actions = Theme.Layout(3, 0);
            var reset = new InkButton("新建");
            reset.Click += (sender, args) => { SelectWord(null); word.Focus(); };
            save.Click += (sender, args) => Act(() => ChangeWord(false));
            delete.Click += (sender, args) =>
            {
                if (selected != null && Notice.Confirm(this, "删除词条", "删除「" + selected.Word + "」及其同编码记录？", "删除"))
                    Act(() => ChangeWord(true));
            };
            actions.Controls.Add(reset, 0, 0);
            actions.Controls.Add(delete, 1, 0);
            actions.Controls.Add(save, 2, 0);
            editor.Controls.Add(actions, 0, 7);
            editor.Controls.Add(Theme.Label("选中一行可编辑", Theme.Small, Theme.Muted), 0, 8);
            panes.Controls.Add(editor, 1, 0);
            layout.Controls.Add(panes, 0, 1);

            var footer = Theme.Layout(2, 0);
            footer.ColumnStyles[0] = new ColumnStyle(SizeType.Percent, 100);
            footer.ColumnStyles[1] = new ColumnStyle(SizeType.Absolute, 106);
            footer.Controls.Add(status, 0, 0);
            var deploy = new InkButton("一键部署", true);
            Tips.Add(deploy, "应用已保存的词条和输入方案配置");
            deploy.Click += async (sender, args) => await DeployWords();
            footer.Controls.Add(deploy, 1, 0);
            layout.Controls.Add(footer, 0, 2);
            Body.Controls.Add(layout);
            search.TextChanged += (sender, args) => RenderRows();
            table.CellClick += (sender, args) => { if (args.RowIndex >= 0) SelectWord((WordEntry)table.Rows[args.RowIndex].Tag); };
            table.KeyDown += (sender, args) =>
            {
                if (args.KeyCode == Keys.Enter && table.CurrentRow != null)
                {
                    SelectWord((WordEntry)table.CurrentRow.Tag);
                    word.Focus();
                    args.SuppressKeyPress = true;
                }
            };
            Reload();
        }

        private void ShowStatus(string text, bool error = false)
        {
            status.Text = text;
            status.ForeColor = error ? Theme.Alert : Theme.Muted;
        }

        private void Act(System.Action action)
        {
            try { action(); }
            catch (Exception problem) { ShowStatus(problem.Message, true); }
        }

        private void Reload()
        {
            snapshot = host.Words.Read();
            RenderRows();
            SelectWord(null);
        }

        private void RenderRows()
        {
            if (snapshot == null) return;
            string query = search.Text.Trim();
            table.Rows.Clear();
            foreach (var entry in snapshot.Entries.Where(item =>
                item.Word.IndexOf(query, StringComparison.OrdinalIgnoreCase) >= 0 ||
                item.Code.IndexOf(query, StringComparison.OrdinalIgnoreCase) >= 0))
            {
                int index = table.Rows.Add(entry.Word, entry.Code, entry.Weight);
                table.Rows[index].Tag = entry;
            }
            table.ClearSelection();
            ShowStatus(table.Rows.Count + " 条词条");
        }

        private void SelectWord(WordEntry entry)
        {
            selected = entry;
            editorTitle.Text = entry == null ? "添加词条" : "编辑词条";
            word.Text = entry == null ? "" : entry.Word;
            code.Text = entry == null ? "" : entry.Code;
            weight.Text = entry == null ? "" : entry.Weight;
            save.Text = entry == null ? "添加" : "保存";
            save.AccessibleName = save.Text;
            delete.Enabled = entry != null;
        }

        private void ChangeWord(bool remove)
        {
            snapshot = host.Words.Edit(snapshot.Revision, selected == null ? null : selected.Key,
                remove, word.Text, code.Text, weight.Text);
            RenderRows();
            SelectWord(null);
            ShowStatus(remove ? "词条已删除，部署后生效。" : "词条已保存，部署后生效。");
        }

        private async Task DeployWords()
        {
            busy = true;
            Body.Enabled = false;
            ShowStatus("正在编译词库…");
            string revision = snapshot.Revision;
            try
            {
                string result = await Task.Run(() => host.Deploy(revision));
                Reload();
                ShowStatus(result);
            }
            catch (Exception failure) { ShowStatus(failure.Message, true); }
            finally { Body.Enabled = true; busy = false; }
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (busy && e.CloseReason == CloseReason.UserClosing) e.Cancel = true;
            base.OnFormClosing(e);
        }

        protected override bool ProcessCmdKey(ref Message message, Keys keys)
        {
            if (keys != (Keys.Control | Keys.F) || busy) return base.ProcessCmdKey(ref message, keys);
            search.Focus();
            return true;
        }
    }

    internal sealed class SettingsWindow : HanLinForm
    {
        internal SettingsWindow(HostActions host) : base("设置", new Size(600, 454))
        {
            Text = "寒淋输入法 · 设置";
            Body.Padding = new Padding(20, 12, 20, 14);
            var layout = Theme.Layout(1, 0, 12, 36);
            var tabs = new SettingsTabs { Dock = DockStyle.Fill, Margin = Padding.Empty };
            layout.Controls.Add(tabs, 0, 0);

            var general = AddSection(tabs, "常规", 24, 29, 44, 34, 0);
            general.Controls.Add(Theme.Label("输入方案", Theme.Small, Theme.Muted), 0, 0);
            general.Controls.Add(Theme.Label("雾凇拼音"), 0, 1);
            var generalActions = Theme.Layout(2, 0);
            generalActions.ColumnStyles[0] = new ColumnStyle(SizeType.Absolute, 152);
            generalActions.ColumnStyles[1] = new ColumnStyle(SizeType.Percent, 100);
            Add(generalActions, 0, "编辑输入方案…", () => Edit(host, "rime_ice.custom.yaml"));
            general.Controls.Add(generalActions, 0, 2);
            general.Controls.Add(Theme.Label("保存文件后，在「词库管理」中执行「一键部署」。", Theme.Small, Theme.Muted), 0, 3);

            var appearance = AddSection(tabs, "外观", 30, 29, 29, 29, 34, 0);
            appearance.Controls.Add(Theme.Label("候选窗", Theme.Heading), 0, 0);
            var localSkin = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                "HanLin", "Skin", "skin.ini");
            appearance.Controls.Add(Theme.Label(File.Exists(localSkin) ? "主题 · 本机 SSF 皮肤" : "主题 · HanLin 暖白珊瑚"), 0, 1);
            appearance.Controls.Add(Theme.Label("字体 · 宋体 / Arial"), 0, 2);
            appearance.Controls.Add(Theme.Label("基准字号 · 16 px，随 Windows 显示缩放"), 0, 3);
            appearance.Controls.Add(Theme.Label("已有本机皮肤会优先加载。", Theme.Small, Theme.Muted), 0, 4);

            var data = AddSection(tabs, "数据", 24, 34, 44, 32, 42, 0);
            data.Controls.Add(Theme.Label("数据文件夹", Theme.Small, Theme.Muted), 0, 0);
            var directory = Theme.Field("数据文件夹");
            directory.ReadOnly = true;
            directory.Text = host.DirectoryPath;
            data.Controls.Add(directory, 0, 1);
            var buttons = Theme.Layout(3, 0);
            buttons.ColumnStyles[0] = new ColumnStyle(SizeType.Absolute, 152);
            buttons.ColumnStyles[1] = new ColumnStyle(SizeType.Absolute, 152);
            buttons.ColumnStyles[2] = new ColumnStyle(SizeType.Percent, 100);
            Add(buttons, 0, "打开数据文件夹", () => {
                Directory.CreateDirectory(host.DirectoryPath);
                using (Process.Start(new ProcessStartInfo(host.DirectoryPath) { UseShellExecute = true })) { }
            });
            Add(buttons, 1, "编辑进阶配置…", () => Edit(host, "hanlin.custom.yaml"));
            data.Controls.Add(buttons, 0, 2);
            data.Controls.Add(Theme.Label("这里保存用户词库、方案配置和本地打字统计。", Theme.Small, Theme.Muted), 0, 3);
            var cloudRow = Theme.Layout(2, 0);
            cloudRow.ColumnStyles[0] = new ColumnStyle(SizeType.Absolute, 152);
            cloudRow.ColumnStyles[1] = new ColumnStyle(SizeType.Absolute, 216);
            var cloud = new InkButton("云同步…");
            cloud.Click += (sender, args) => { using (var window = new SyncWindow(host)) window.ShowDialog(this); };
            cloudRow.Controls.Add(cloud, 0, 0);
            var autoSync = new InkButton(host.AutoSyncEnabled ? "每日自动同步：开启" : "每日自动同步：关闭");
            autoSync.Click += (sender, args) =>
            {
                try
                {
                    host.SetAutoSync(!host.AutoSyncEnabled);
                    autoSync.Text = host.AutoSyncEnabled ? "每日自动同步：开启" : "每日自动同步：关闭";
                    autoSync.AccessibleName = autoSync.Text;
                }
                catch (Exception error)
                {
                    if (!(error is IOException) && !(error is UnauthorizedAccessException)) throw;
                    Notice.Show(this, "无法保存自动同步设置", error.Message);
                }
            };
            cloudRow.Controls.Add(autoSync, 1, 0);
            data.Controls.Add(cloudRow, 0, 4);

            var about = AddSection(tabs, "关于", 30, 26, 34, 0);
            about.Controls.Add(Theme.Label("寒淋输入法", Theme.Heading), 0, 0);
            about.Controls.Add(Theme.Label("版本 " + Application.ProductVersion, Theme.Small, Theme.Muted), 0, 1);
            about.Controls.Add(Theme.Label("Ctrl+F   在词库管理中搜索词条"), 0, 2);

            var footer = Theme.Layout(2, 0);
            footer.ColumnStyles[0] = new ColumnStyle(SizeType.Percent, 100);
            footer.ColumnStyles[1] = new ColumnStyle(SizeType.Absolute, 84);
            var close = new InkButton("关闭", true) { Margin = Padding.Empty };
            close.Click += (sender, args) => Close();
            AcceptButton = close;
            footer.Controls.Add(close, 1, 0);
            layout.Controls.Add(footer, 0, 2);
            Body.Controls.Add(layout);
            Shown += (sender, args) => tabs.FocusCurrentTab();
        }

        private static TableLayoutPanel AddSection(SettingsTabs tabs, string title, params float[] rows)
        {
            var fields = Theme.Layout(1, rows);
            fields.Padding = new Padding(0, 16, 4, 8);
            tabs.AddPage(title, fields);
            return fields;
        }

        private void Add(TableLayoutPanel row, int column, string title, System.Action action)
        {
            var button = new InkButton(title);
            Tips.Add(button, title == "打开数据文件夹" ? "在资源管理器中打开用户数据目录" : "用文本编辑器打开配置文件");
            button.Click += (sender, args) =>
            {
                try { action(); }
                catch (Exception error) { Notice.Show(this, "无法打开配置", error.Message); }
            };
            row.Controls.Add(button, column, 0);
        }

        private static void Edit(HostActions host, string name)
        {
            var file = Path.Combine(host.DirectoryPath, name);
            if (!File.Exists(file)) throw new FileNotFoundException("配置文件尚未生成。", file);
            var editor = Path.Combine(Environment.SystemDirectory, "notepad.exe");
            Process.Start(new ProcessStartInfo(editor, "\"" + file + "\"") { UseShellExecute = false }).Dispose();
        }

        private sealed class SettingsTabs : Panel
        {
            private readonly FlowLayoutPanel bar = new FlowLayoutPanel {
                Dock = DockStyle.Top, Height = 37, WrapContents = false,
                BackColor = Theme.Paper, AccessibleName = "设置分类", AccessibleRole = AccessibleRole.PageTabList
            };
            private readonly Panel pages = new Panel { Dock = DockStyle.Fill, BackColor = Theme.Paper };

            internal SettingsTabs()
            {
                Controls.Add(pages);
                Controls.Add(new Panel { Dock = DockStyle.Top, Height = 1, BackColor = Theme.Line });
                Controls.Add(bar);
            }

            internal void AddPage(string title, Control page)
            {
                var chip = new SettingTab(title);
                bar.Controls.Add(chip);
                page.Visible = false;
                pages.Controls.Add(page);
                chip.CheckedChanged += (sender, args) => {
                    page.Visible = chip.Checked;
                    if (chip.Checked) page.BringToFront();
                };
                if (bar.Controls.Count == 1) chip.Checked = true;
            }

            internal void FocusCurrentTab()
            {
                var tab = bar.Controls.OfType<SettingTab>().FirstOrDefault(item => item.Checked);
                if (tab != null) tab.Focus();
            }
        }

        private sealed class SettingTab : RadioButton
        {
            private bool hover;

            internal SettingTab(string title)
            {
                Text = AccessibleName = title;
                AccessibleRole = AccessibleRole.PageTab;
                Appearance = System.Windows.Forms.Appearance.Button;
                Width = 72;
                Height = 32;
                Margin = new Padding(0, 0, 4, 5);
                FlatStyle = FlatStyle.Flat;
                Cursor = Cursors.Hand;
                SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint, true);
                CheckedChanged += (sender, args) => Invalidate();
                GotFocus += (sender, args) => Invalidate();
                LostFocus += (sender, args) => Invalidate();
            }

            protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); hover = true; Invalidate(); }
            protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hover = false; Invalidate(); }

            protected override void OnPaint(PaintEventArgs e)
            {
                var graphics = e.Graphics;
                graphics.Clear(Theme.Paper);
                var bounds = new Rectangle(0, 0, Width - 1, Height - 1);
                if (bounds.Width <= 0 || bounds.Height <= 0) return;
                graphics.SmoothingMode = SmoothingMode.AntiAlias;
                using (var chip = Theme.Rounded(bounds, Theme.Scale(7, DeviceDpi)))
                using (var fill = new SolidBrush(Checked ? Theme.Tint : hover ? Theme.Hover : Theme.Paper))
                using (var focus = new Pen(Theme.Alert))
                {
                    graphics.FillPath(fill, chip);
                    if (Focused) graphics.DrawPath(focus, chip);
                }
                Color ink = Checked ? Theme.Alert : hover && SystemInformation.HighContrast ? Theme.AccentInk : Theme.Ink;
                TextRenderer.DrawText(graphics, Text, Theme.Body, bounds, ink,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            }
        }
    }

    internal static class Notice
    {
        internal static bool Confirm(Form owner, string title, string text, string action)
        {
            return Display(owner, title, text, action, true) == DialogResult.OK;
        }

        internal static void Show(Form owner, string title, string text)
        {
            Display(owner, title, text, "关闭", false);
        }

        private static DialogResult Display(Form owner, string title, string text, string action, bool cancel)
        {
            using (var window = new HanLinForm(title, new Size(468, 219)))
            {
                if (owner != null) window.StartPosition = FormStartPosition.CenterParent;
                var layout = Theme.Layout(1, 0, 42);
                layout.Controls.Add(Theme.Label(text), 0, 0);
                var controls = Theme.Layout(cancel ? 2 : 1, 0);
                var ok = new InkButton(action, true) { DialogResult = DialogResult.OK };
                controls.Controls.Add(ok, controls.ColumnCount - 1, 0);
                if (cancel)
                {
                    var no = new InkButton("取消") { DialogResult = DialogResult.Cancel };
                    controls.Controls.Add(no, 0, 0);
                    window.CancelButton = no;
                }
                layout.Controls.Add(controls, 0, 1);
                window.Body.Controls.Add(layout);
                return window.ShowDialog(owner);
            }
        }
    }
}
