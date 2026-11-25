using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Windows.Forms;

namespace HanLin.Tools
{
    internal static class ToolsRegression
    {
        [STAThread]
        private static int Main(string[] args)
        {
            if (args.Length != 1) return 2;
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            var directory = Path.Combine(args[0], "tools-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            var today = new DateTime(2026, 11, 25);
            var log = Path.Combine(directory, "stats.log");
            Check(TypingStats.Read(log, today).Total == 0, "missing statistics");
            File.WriteAllLines(log, new[] {
                "2026-10-31\t7", "2026-11-22\t19", "2026-11-23\t42", "2026-11-25\t35",
                "2026-11-25\t65", "2026-11-25\t-43", "2026-11-26\t400", "2026-11-31\t80", "broken"
            });
            var stats = TypingStats.Read(log, today);
            Check(stats.Today == 100 && stats.Week == 142 && stats.Month == 161 && stats.Total == 168, "day, week, month and total");
            Check(stats.On(today.AddDays(-1)) == 0 && stats.Days.Count == 4, "gaps and invalid rows");

            var words = new WordStore(directory);
            File.WriteAllLines(words.Pathname, new[] { "# comment", "ChatGPT\tCHATGPT\t15\tnote", "chatgpt\tchatgpt\t15\tnote",
                "一条\tyitiao", "Codec\tcodec\t10", "CODEC\tCODEC\t20" });
            var before = words.Read();
            Check(before.Entries.Length == 3 && before.Entries[0].Code == "chatgpt", "case-insensitive listing");
            var normalized = WordStore.Normalize(before.Lines);
            Check(normalized.Count(line => line.StartsWith("ChatGPT\tchatgpt")) == 1 &&
                normalized.Count(line => line.IndexOf("codec", StringComparison.OrdinalIgnoreCase) >= 0) == 2, "keep conflicting weights");
            Reject(() => words.Edit(before.Revision, null, false, "CHATGPT", "", ""), "duplicate");
            var added = words.Edit(before.Revision, null, false, "Demo12", "", "");
            Check(added.Entries.Any(entry => entry.Code == "demo12"), "auto lowercase English code");
            Reject(() => words.Edit(before.Revision, null, false, "Other", "", ""), "stale revision");
            var changed = words.Edit(added.Revision, before.Entries[0].Key, false, "ChatGPT", "chatgpt", "23");
            Check(changed.Lines.Contains("ChatGPT\tchatgpt\t23\tnote") &&
                !changed.Lines.Any(line => line.StartsWith("chatgpt\t")), "editing preserves suffix and merges aliases");
            var after = words.Edit(changed.Revision, added.Entries.Last().Key, true, "", "", "");
            Check(after.Entries.Length == 3 && after.Lines[0] == "# comment", "delete preserves unrelated rows");
            var mixed = WordStore.Utf8.GetString(WordStore.MixedDictionary(new[] { "ChatGPT\tCHATGPT",
                "Ubuntu\tubuntu", "ESP32\tesp32", "ChatGPT\tchatgpt" }));
            Check(mixed.Contains("ChatGPT\tchatgpt\t1000") && !mixed.Contains("Ubuntu") &&
                mixed.Split(new[] { "ESP32\tesp32" }, StringSplitOptions.None).Length == 2, "mixed dictionary filters and deduplicates");

            File.WriteAllLines(log, Enumerable.Range(0, 10).Select(index =>
                DateTime.Today.AddDays(-index).ToString("yyyy-MM-dd") + "\t" + (1284 + 157 * index % 1209)));
            var host = new HostActions(directory, directory, directory);
            using (var menu = new TrayMenu(host, new Point(400, 550)))
            {
                var labels = Children(menu).Select(control => control.Text).ToArray();
                foreach (var label in new[] { "今日打字", "查看更多", "词库管理", "设置" })
                    Check(labels.Contains(label), "menu: " + label);
                Check(!labels.Contains("退出"), "the menu cannot stop the input service");
                Render(menu, Path.Combine(directory, "tray.png"));
            }
            using (var history = new StatsWindow(host))
            {
                Check(Children(history).OfType<DataGridView>().Single().Rows.Count == 14, "14 calendar days");
                Render(history, Path.Combine(directory, "stats.png"));
            }
            using (var manager = new WordWindow(host)) Render(manager, Path.Combine(directory, "words.png"));
            using (var settings = new SettingsWindow(host)) Render(settings, Path.Combine(directory, "settings.png"));
            Console.WriteLine("HanLin tools regression: PASS");
            Console.WriteLine("Heading font: " + Theme.Heading.FontFamily.Name);
            Console.WriteLine(directory);
            return 0;
        }

        private static IEnumerable<Control> Children(Control owner)
        {
            foreach (Control child in owner.Controls)
            {
                yield return child;
                foreach (var nested in Children(child)) yield return nested;
            }
        }

        private static void Render(Form window, string path)
        {
            var create = typeof(Control).GetMethod("CreateControl", BindingFlags.Instance | BindingFlags.NonPublic,
                null, new[] { typeof(bool) }, null);
            create.Invoke(window, new object[] { true });
            window.PerformLayout();
            using (var bitmap = new Bitmap(window.Width, window.Height))
            {
                window.DrawToBitmap(bitmap, window.ClientRectangle);
                bitmap.Save(path, ImageFormat.Png);
            }
        }

        private static void Reject(System.Action action, string label)
        {
            try { action(); }
            catch (InvalidOperationException) { return; }
            throw new InvalidOperationException("Expected rejection: " + label);
        }

        private static void Check(bool result, string label)
        {
            if (!result) throw new InvalidOperationException("Regression: " + label);
        }

    }
}
