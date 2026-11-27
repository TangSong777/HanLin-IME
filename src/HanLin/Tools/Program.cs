using System;
using System.Drawing;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Windows.Forms;

namespace HanLin.Tools
{
    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            try
            {
                Action action = Action.Words;
                Point? anchor = null;
                string user = null, shared = null, revision = null, expectedDirectory = null;
                bool deploy = false, noTray = false;
                for (int i = 0; i < args.Length; i++)
                {
                    switch (args[i])
                    {
                        case "--menu":
                            anchor = new Point(int.Parse(Value(args, ref i), CultureInfo.InvariantCulture),
                                               int.Parse(Value(args, ref i), CultureInfo.InvariantCulture));
                            break;
                        case "--words": action = Action.Words; break;
                        case "--stats": action = Action.Stats; break;
                        case "--settings": action = Action.Settings; break;
                        case "--cloud-sync": action = Action.CloudSync; break;
                        case "--user-data": user = Value(args, ref i); break;
                        case "--shared-data": shared = Value(args, ref i); break;
                        case "--deploy-words": deploy = true; break;
                        case "--no-tray": noTray = true; break;
                        case "--revision": revision = Value(args, ref i); break;
                        case "--expect-directory": expectedDirectory = Value(args, ref i); break;
                        default: throw new ArgumentException("未知的寒淋工具参数。");
                    }
                }

                var host = new HostActions(user, shared, AppDomain.CurrentDomain.BaseDirectory, noTray);
                if (deploy)
                {
                    if (expectedDirectory != null && !string.Equals(Path.GetFullPath(expectedDirectory), host.DirectoryPath,
                        StringComparison.OrdinalIgnoreCase)) return 2;
                    host.Words.Seed(Path.Combine(host.Preset, "custom_phrase.txt"));
                    host.Deploy(revision);
                    return 0;
                }
                if (anchor.HasValue)
                {
                    bool first;
                    using (var instance = new Mutex(true, @"Local\HanLinTrayMenu", out first))
                    {
                        if (!first) return 0;
                        action = ShowTrayMenu(host, anchor.Value);
                    }
                }
                switch (action)
                {
                    case Action.Stats: using (var window = new StatsWindow(host)) Application.Run(window); break;
                    case Action.Words: using (var window = new WordWindow(host)) Application.Run(window); break;
                    case Action.Settings: using (var window = new SettingsWindow(host)) Application.Run(window); break;
                    case Action.CloudSync: using (var window = new SyncWindow(host)) Application.Run(window); break;
                    case Action.Data: Process.Start(new ProcessStartInfo(host.DirectoryPath) { UseShellExecute = true }); break;
                }
                // Let a sync finish even when the user closes all tool windows.
                host.WaitForCloudSync();
                return 0;
            }
            catch (Exception error)
            {
                if (Array.IndexOf(args, "--deploy-words") >= 0) Console.Error.WriteLine(error.Message);
                else Notice.Show(null, "寒淋输入法", error.Message);
                return 1;
            }
        }

        private static Action ShowTrayMenu(HostActions host, Point anchor)
        {
            Action choice = Action.None;
            using (var menu = new TrayMenu())
            using (var context = new ApplicationContext())
            {
                string today;
                try { today = host.Stats().Today.ToString("N0"); }
                catch (IOException) { today = "读取失败"; }
                catch (UnauthorizedAccessException) { today = "读取失败"; }
                menu.AddAction("今日打字  " + today, "today", () => { }).Enabled = false;
                menu.AddAction("打字统计", "stats", () => choice = Action.Stats);
                menu.AddAction("词库管理", "words", () => choice = Action.Words);
                menu.AddAction("设置…", "settings", () => choice = Action.Settings);
                menu.Items.Add(new ToolStripSeparator());
                menu.AddAction("打开数据文件夹", "file", () => choice = Action.Data);

                // Show once the WinForms message loop owns popup keyboard and dismissal.
                EventHandler open = null;
                open = (sender, args) =>
                {
                    Application.Idle -= open;
                    menu.Show(anchor, ToolStripDropDownDirection.AboveLeft);
                };
                Application.Idle += open;
                menu.Closed += (sender, args) => context.ExitThread();
                try { Application.Run(context); }
                finally { Application.Idle -= open; }
                return choice;
            }
        }

        private static string Value(string[] args, ref int index)
        {
            if (++index >= args.Length) throw new ArgumentException("寒淋工具缺少参数。");
            return args[index];
        }
    }
}
