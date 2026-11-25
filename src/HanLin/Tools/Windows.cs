using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace HanLin.Tools
{
    internal enum Action { None, Stats, Words, Settings }

    internal sealed class TrayMenu : HanLinForm
    {
        private readonly Point anchor;
        internal Action Choice { get; private set; }

        internal TrayMenu(HostActions host, Point anchor) : base("寒淋输入法", new Size(280, 229), true)
        {
            this.anchor = anchor;
            Location = anchor;
            var layout = Theme.Layout(1, 27, 105, 37, 37);
            var header = Theme.Label("寒淋输入法", Theme.Small);
            header.Padding = new Padding(7, 0, 0, 0);
            var card = Theme.Layout(2, 27, 56);
            card.BackColor = Theme.Tint;
            card.Padding = new Padding(10, 8, 8, 6);
            card.Margin = new Padding(3, 2, 3, 8);
            card.ColumnStyles[0] = new ColumnStyle(SizeType.Percent, 100);
            card.ColumnStyles[1] = new ColumnStyle(SizeType.Absolute, 90);
            card.Controls.Add(Theme.Label("今日打字", Theme.Small, Theme.Muted), 0, 0);
            string number;
            try { number = host.Stats().Today.ToString("N0"); }
            catch (IOException) { number = "读取失败"; }
            catch (UnauthorizedAccessException) { number = "读取失败"; }
            card.Controls.Add(Theme.Label(number, Theme.Metric, Theme.Ink), 0, 1);
            var more = MenuButton("查看更多", Action.Stats);
            card.Controls.Add(more, 1, 1);
            layout.Controls.Add(header, 0, 0);
            layout.Controls.Add(card, 0, 1);
            layout.Controls.Add(MenuButton("词库管理", Action.Words), 0, 2);
            layout.Controls.Add(MenuButton("设置", Action.Settings), 0, 3);
            Body.Controls.Add(layout);
            ActiveControl = more;
        }

        private InkButton MenuButton(string text, Action action)
        {
            var button = new InkButton(text, action == Action.Stats, action);
            button.Click += (sender, args) => { Choice = action; Close(); };
            return button;
        }

        protected override void OnLoad(EventArgs e)
        {
            base.OnLoad(e);
            var area = Screen.FromPoint(anchor).WorkingArea;
            if (Width > area.Width) Width = area.Width;
            if (Height > area.Height) Height = area.Height;
            int x = Math.Max(area.Left, Math.Min(area.Right - Width, anchor.X - Width + 12));
            int y = anchor.Y - Height - 8;
            if (y < area.Top) y = anchor.Y + 10;
            Location = new Point(x, Math.Max(area.Top, Math.Min(area.Bottom - Height, y)));
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            Activate();
        }

        protected override void OnDeactivate(EventArgs e)
        {
            base.OnDeactivate(e);
            if (Visible) Close();
        }
    }

    internal sealed class StatsWindow : HanLinForm
    {
        internal StatsWindow(HostActions host) : base("打字统计", new Size(700, 646))
        {
            var stats = host.Stats();
            var layout = Theme.Layout(1, 28, 90, 38, 0, 28);
            layout.Controls.Add(Theme.Label("截至 " + stats.Date.ToString("yyyy-MM-dd"), Theme.Small, Theme.Muted), 0, 0);
            var metrics = Theme.Layout(4, 0);
            var counts = new[] { stats.Today, stats.Week, stats.Month, stats.Total };
            var names = new[] { "今日", "本周", "本月", "总计" };
            for (int i = 0; i < counts.Length; i++)
            {
                var tile = Theme.Layout(1, 26, 0);
                tile.BackColor = i == 0 ? Theme.Tint : Theme.Paper;
                tile.Margin = new Padding(0, 2, 7, 5);
                tile.Padding = new Padding(10, 5, 6, 5);
                tile.Controls.Add(Theme.Label(names[i], Theme.Small, Theme.Muted), 0, 0);
                tile.Controls.Add(Theme.Label(counts[i].ToString("N0"), Theme.Heading), 0, 1);
                metrics.Controls.Add(tile, i, 0);
            }
            layout.Controls.Add(metrics, 0, 1);
            layout.Controls.Add(Theme.Label("最近 14 天"), 0, 2);
            var rows = Theme.Grid();
            rows.Columns.Add("date", "日期");
            rows.Columns.Add("count", "字数");
            rows.Columns[1].DefaultCellStyle.Alignment = DataGridViewContentAlignment.MiddleRight;
            for (int offset = 0; offset < 14; offset++)
            {
                var day = stats.Date.AddDays(-offset);
                rows.Rows.Add(day.ToString("yyyy-MM-dd"), stats.On(day).ToString("N0"));
            }
            rows.ClearSelection();
            layout.Controls.Add(rows, 0, 3);
            layout.Controls.Add(Theme.Label(stats.Days.Count == 0 ? "暂无打字记录" : "本周从星期一开始", Theme.Small, Theme.Muted), 0, 4);
            Body.Controls.Add(layout);
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
    }

    internal sealed class SettingsWindow : HanLinForm
    {
        internal SettingsWindow(HostActions host) : base("设置", new Size(556, 268))
        {
            var layout = Theme.Layout(1, 28, 34, 14, 45, 44);
            layout.Controls.Add(Theme.Label("用户配置目录", Theme.Small, Theme.Muted), 0, 0);
            var directory = Theme.Field("用户配置目录");
            directory.ReadOnly = true;
            directory.Text = host.DirectoryPath;
            layout.Controls.Add(directory, 0, 1);
            var buttons = Theme.Layout(3, 0);
            Add(buttons, 0, "打开配置目录", () => {
                Directory.CreateDirectory(host.DirectoryPath);
                Process.Start(new ProcessStartInfo(host.DirectoryPath) { UseShellExecute = true }).Dispose();
            });
            Add(buttons, 1, "编辑候选窗配置", () => Edit(host, "hanlin.custom.yaml"));
            Add(buttons, 2, "编辑输入方案", () => Edit(host, "rime_ice.custom.yaml"));
            layout.Controls.Add(buttons, 0, 3);
            layout.Controls.Add(Theme.Label("改动配置后，可在词库管理中重新部署。", Theme.Small, Theme.Muted), 0, 4);
            Body.Controls.Add(layout);
        }

        private void Add(TableLayoutPanel row, int column, string title, System.Action action)
        {
            var button = new InkButton(title);
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
