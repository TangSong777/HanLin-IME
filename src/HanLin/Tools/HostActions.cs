using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Text;
using System.Web.Script.Serialization;
using Microsoft.Win32;

namespace HanLin.Tools
{
    internal sealed class HostActions
    {
        internal readonly string DirectoryPath, Shared, Preset;
        internal readonly WordStore Words;
        private readonly string executable;
        private readonly bool noTray;

        internal HostActions(string userData, string sharedData, string install, bool noTray = false)
        {
            this.noTray = noTray;
            if (userData == null)
            {
                userData = Registry.GetValue(@"HKEY_CURRENT_USER\Software\HanLin", "UserDataDir", null) as string;
                if (string.IsNullOrWhiteSpace(userData))
                    userData = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "HanLin");
            }
            DirectoryPath = Path.GetFullPath(Environment.ExpandEnvironmentVariables(userData));
            string runtime = Path.Combine(install, "RuntimeData");
            Shared = Path.GetFullPath(sharedData ?? (Directory.Exists(runtime) ? runtime : Path.Combine(install, "data")));
            Preset = Path.Combine(Shared, "preset");
            executable = Path.Combine(install, "HanLinHost.exe");
            Words = new WordStore(DirectoryPath);
        }

        internal Task<Tuple<int, string>> CloudSyncJob { get; private set; }
        internal string CloudSyncOperation { get; private set; }

        internal void StartCloudSync(string operation, string server, string code)
        {
            if (CloudSyncJob != null && !CloudSyncJob.IsCompleted) return;
            CloudSyncOperation = operation;
            CloudSyncJob = Task.Run(() => CloudSync(operation, server, Environment.MachineName, code));
        }

        internal void WaitForCloudSync()
        {
            if (CloudSyncJob == null) return;
            try { CloudSyncJob.GetAwaiter().GetResult(); }
            catch (Exception) { /* The sync window reports failures while open. */ }
        }

        internal Dictionary<string, object> ReadCloudSyncState()
        {
            try
            {
                var path = Path.Combine(DirectoryPath, ".hanlin-sync.json");
                return new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(File.ReadAllText(path));
            }
            catch (Exception error)
            {
                if (error is IOException || error is UnauthorizedAccessException ||
                    error is ArgumentException || error is InvalidOperationException) return null;
                throw;
            }
        }

        private string AutoSyncFlag { get { return Path.Combine(DirectoryPath, ".hanlin-auto-sync-off"); } }

        internal bool AutoSyncEnabled { get { return !File.Exists(AutoSyncFlag); } }

        internal void SetAutoSync(bool enabled)
        {
            if (enabled) File.Delete(AutoSyncFlag);
            else
            {
                Directory.CreateDirectory(DirectoryPath);
                using (File.Open(AutoSyncFlag, FileMode.OpenOrCreate, FileAccess.Write)) { }
            }
        }

        private Tuple<int, string> CloudSync(string action, string server, string name, string code)
        {
            string script = Path.Combine(Path.GetDirectoryName(executable), "Sync-HanLin.ps1");
            if (!File.Exists(script)) throw new FileNotFoundException("安装目录里缺少云同步脚本，请重新安装。");
            var args = new List<string> { "-NoLogo", "-NoProfile", "-NonInteractive", "-File", script,
                action, "-UserData", DirectoryPath, "-HostPath", executable, "-SharedData", Shared,
                "-Server", server, "-DeviceName", name };
            if (code != null) args.Add("-PairFromStdin");
            if (action == "sync")
            {
                args.Add("-Live");
                if (!noTray) args.Add("-ResumeTray");
            }
            string pwsh = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                "PowerShell", "7", "pwsh.exe");
            if (!File.Exists(pwsh)) pwsh = "pwsh.exe";
            using (var process = Process.Start(new ProcessStartInfo(pwsh, string.Join(" ", args.Select(Quote))) {
                UseShellExecute = false, CreateNoWindow = true, RedirectStandardInput = true,
                RedirectStandardOutput = true, RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8,
                WorkingDirectory = Path.GetDirectoryName(script)
            }))
            {
                var stdout = process.StandardOutput.ReadToEndAsync();
                var stderr = process.StandardError.ReadToEndAsync();
                if (code != null) process.StandardInput.WriteLine(code);
                process.StandardInput.Close();
                process.WaitForExit();
                var text = (stdout.Result + stderr.Result).Trim();
                return Tuple.Create(process.ExitCode, text);
            }
        }

        internal TypingStats Stats() { return TypingStats.Read(Path.Combine(DirectoryPath, "stats.log"), DateTime.Today); }

        internal void Stop()
        {
            if (RunHost("--stop") != 0) throw new InvalidOperationException("寒淋服务尚未退出。");
        }

        internal string Deploy(string revision)
        {
            return Words.Locked(() =>
            {
                var snapshot = Words.CheckedRead(revision);
                var updates = new List<KeyValuePair<string, byte[]>>();
                var patch = "rime_ice.custom.yaml";
                var path = Path.Combine(DirectoryPath, patch);
                var preset = Path.Combine(Preset, patch);
                var original = File.Exists(path) ? File.ReadAllText(path, WordStore.Utf8)
                    : File.Exists(preset) ? File.ReadAllText(preset, WordStore.Utf8) : "";
                var adjusted = Regex.Replace(original,
                    @"(?m)^([ \t]*-[ \t]*)table_translator@(custom_phrase(?:_complete)?)([ \t]*(?:#[^\r\n]*)?)(?=\r?$)",
                    "${1}lua_translator@*custom_phrase_case@${2}${3}");
                var useCase = adjusted.Contains("lua_translator@*custom_phrase_case@custom_phrase_complete");
                if (adjusted != original)
                {
                    foreach (var module in new[] { "autocap_filter", "custom_phrase_case", "short_prefix_filter", "lowercase_candidate_filter" })
                    {
                        var relative = Path.Combine("lua", module + ".lua");
                        updates.Add(Update(relative, File.ReadAllBytes(Path.Combine(Shared, relative))));
                    }
                    updates.Add(Update(patch, WordStore.Utf8.GetBytes(adjusted)));
                }
                var lines = useCase ? WordStore.Normalize(snapshot.Lines) : snapshot.Lines;
                if (!lines.SequenceEqual(snapshot.Lines)) updates.Add(Update("custom_phrase.txt", WordStore.EncodeLines(lines)));
                updates.Add(Update("mixed_tokens.dict.yaml", WordStore.MixedDictionary(lines)));

                using (var maintenance = new EventWaitHandle(false, EventResetMode.ManualReset, @"Local\HanLinMaintenance"))
                    return DeployFiles(updates, lines, useCase);
            });
        }

        private string DeployFiles(List<KeyValuePair<string, byte[]>> updates, string[] lines, bool useCase)
        {
            Stop();
            var backups = Path.Combine(DirectoryPath, "hanlin-word-backups",
                DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 6));
            var written = new List<FileChange>();
            bool built = false;
            try
            {
                foreach (var update in updates)
                {
                    var target = Path.Combine(DirectoryPath, update.Key);
                    var before = File.Exists(target) ? File.ReadAllBytes(target) : null;
                    if (before != null && before.SequenceEqual(update.Value)) continue;
                    if (before != null) WordStore.Write(Path.Combine(backups, update.Key), before);
                    WordStore.Write(target, update.Value);
                    written.Add(new FileChange(target, before));
                }
                Compile();
                built = true;
                var schema = Path.Combine(DirectoryPath, "build", "rime_ice.schema.yaml");
                if (!File.Exists(schema))
                    schema = Path.Combine(Shared, "build", "rime_ice.schema.yaml");
                if (!File.Exists(schema) || new FileInfo(schema).Length == 0 ||
                    (useCase && !File.ReadAllText(schema, WordStore.Utf8)
                        .Contains("lua_translator@*custom_phrase_case@custom_phrase_complete")))
                    throw new InvalidOperationException("编译后的方案没有加载词条配置。");
                var count = WordStore.Rows(lines).Select(row => row.Key).Distinct(StringComparer.Ordinal).Count();
                return "已部署 " + count + " 条词条。";
            }
            catch (Exception failure)
            {
                var rollbackErrors = new List<string>();
                for (int index = written.Count - 1; index >= 0; index--)
                {
                    try { written[index].Restore(); }
                    catch (Exception rollback) { rollbackErrors.Add(rollback.Message); }
                }
                if (built)
                {
                    try { Compile(); }
                    catch (Exception rebuild) { rollbackErrors.Add(rebuild.Message); }
                }
                if (rollbackErrors.Count != 0)
                    throw new InvalidOperationException(failure.Message + " 恢复失败。备份目录 " + backups
                        + "。 " + string.Join(" ", rollbackErrors), failure);
                throw;
            }
            finally
            {
                var arguments = new List<string> { "--user-data", DirectoryPath, "--shared-data", Shared };
                if (noTray) arguments.Add("--no-tray");
                LaunchHost(arguments.ToArray()).Dispose();
            }
        }

        private void Compile()
        {
            var result = RunHost("--deploy", "--user-data", DirectoryPath, "--shared-data", Shared);
            if (result == 3)
            {
                Thread.Sleep(300);
                Stop();
                result = RunHost("--deploy", "--user-data", DirectoryPath, "--shared-data", Shared);
            }
            if (result != 0) throw new InvalidOperationException("词库编译失败，退出码 " + result + "。");
        }

        private static KeyValuePair<string, byte[]> Update(string relative, byte[] bytes)
        {
            return new KeyValuePair<string, byte[]>(relative, bytes);
        }

        private sealed class FileChange
        {
            private readonly string path;
            private readonly byte[] before;

            internal FileChange(string target, byte[] content) { path = target; before = content; }
            internal void Restore()
            {
                if (before == null) File.Delete(path);
                else WordStore.Write(path, before);
            }
        }

        private int RunHost(params string[] args)
        {
            using (var process = LaunchHost(args))
            {
                if (!process.WaitForExit(300000))
                {
                    process.Kill();
                    process.WaitForExit();
                    throw new TimeoutException("寒淋部署超时。");
                }
                return process.ExitCode;
            }
        }

        private Process LaunchHost(params string[] args)
        {
            return Process.Start(new ProcessStartInfo(executable, string.Join(" ", args.Select(Quote))) {
                UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = Path.GetDirectoryName(executable)
            });
        }

        private static string Quote(string argument)
        {
            return "\"" + Regex.Replace(argument, "(\\\\*)(\"|$)", match =>
                match.Groups[1].Value + match.Groups[1].Value +
                (match.Groups[2].Value.Length == 0 ? "" : "\\\"")) + "\"";
        }
    }
}
