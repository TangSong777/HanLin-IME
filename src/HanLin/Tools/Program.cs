using System;
using System.Drawing;
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
                        using (var menu = new TrayMenu(host, anchor.Value))
                        {
                            Application.Run(menu);
                            action = menu.Choice;
                        }
                    }
                }
                switch (action)
                {
                    case Action.Stats: using (var window = new StatsWindow(host)) Application.Run(window); break;
                    case Action.Words: using (var window = new WordWindow(host)) Application.Run(window); break;
                    case Action.Settings: using (var window = new SettingsWindow(host)) Application.Run(window); break;
                }
                return 0;
            }
            catch (Exception error)
            {
                if (Array.IndexOf(args, "--deploy-words") >= 0) Console.Error.WriteLine(error.Message);
                else Notice.Show(null, "寒淋输入法", error.Message);
                return 1;
            }
        }

        private static string Value(string[] args, ref int index)
        {
            if (++index >= args.Length) throw new ArgumentException("寒淋工具缺少参数。");
            return args[index];
        }
    }
}
