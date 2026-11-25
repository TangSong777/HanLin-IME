using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace HanLin.Tools
{
    internal sealed class WordEntry
    {
        internal readonly string Word, Code, Suffix, Weight, Key;
        internal readonly int Line;

        internal WordEntry(string word, string code, string suffix, int line)
        {
            Word = word;
            Key = (WordStore.Fold(word, code) ? word.ToLowerInvariant() : word) + "\t" + code;
            Code = code;
            Suffix = suffix;
            Weight = suffix.Length == 0 ? "" : suffix.Substring(1).Split('\t')[0];
            Line = line;
        }
    }

    internal sealed class WordSnapshot
    {
        internal readonly string Revision;
        internal readonly string[] Lines;
        internal readonly WordEntry[] Entries;

        internal WordSnapshot(string revision, string[] lines)
        {
            Revision = revision;
            Lines = lines;
            Entries = WordStore.Rows(lines).GroupBy(row => row.Key, StringComparer.Ordinal)
                .Select(group => group.First()).ToArray();
        }
    }

    internal sealed class WordStore
    {
        private static readonly Regex Entry = new Regex(@"^([^#\t][^\t]*)\t([^\t]+)(\t.*)?$");
        private static readonly Regex English = new Regex(@"\A[A-Za-z0-9]+\z");
        private static readonly Regex Letter = new Regex(@"[A-Za-z]");
        private static readonly Regex EnglishCode = new Regex(@"\A[A-Za-z][A-Za-z0-9]*\z");
        private static readonly Regex MixedToken = new Regex(@"^(?!#)([\x20-\x7E]+)\t([a-zA-Z0-9]+)(?:\t|$)");
        internal static readonly UTF8Encoding Utf8 = new UTF8Encoding(false, true);
        internal readonly string Pathname;

        internal WordStore(string directory) { Pathname = Path.Combine(directory, "custom_phrase.txt"); }

        internal static bool Fold(string word, string code)
        {
            return English.IsMatch(word) && Letter.IsMatch(word) && EnglishCode.IsMatch(code);
        }

        internal static IEnumerable<WordEntry> Rows(string[] lines)
        {
            for (int index = 0; index < lines.Length; index++)
            {
                var match = Entry.Match(lines[index]);
                if (!match.Success) continue;
                var word = match.Groups[1].Value;
                var code = match.Groups[2].Value.Trim();
                if (Fold(word, code)) code = code.ToLowerInvariant();
                yield return new WordEntry(word, code, match.Groups[3].Value, index);
            }
        }

        internal WordSnapshot Read()
        {
            byte[] bytes;
            try { bytes = File.ReadAllBytes(Pathname); }
            catch (FileNotFoundException) { return new WordSnapshot("", new string[0]); }
            catch (DirectoryNotFoundException) { return new WordSnapshot("", new string[0]); }
            var lines = new List<string>();
            using (var reader = new StreamReader(new MemoryStream(bytes), Utf8, true))
            {
                string line;
                while ((line = reader.ReadLine()) != null) lines.Add(line);
            }
            return new WordSnapshot(Hash(bytes), lines.ToArray());
        }

        internal void Seed(string source)
        {
            if (!File.Exists(source) || File.Exists(Pathname)) return;
            Locked(() =>
            {
                if (!File.Exists(Pathname)) Write(Pathname, File.ReadAllBytes(source));
                return true;
            });
        }

        internal WordSnapshot CheckedRead(string revision)
        {
            var snapshot = Read();
            if (revision != null && snapshot.Revision != revision)
                throw new InvalidOperationException("词表已在别处更新，请重新读取。");
            return snapshot;
        }

        internal WordSnapshot Edit(string revision, string originalKey, bool remove,
            string word, string code, string weight)
        {
            if (remove && originalKey == null) throw new ArgumentNullException("originalKey");
            word = word.Trim();
            code = code.Trim();
            weight = weight.Trim();
            if (code.Length == 0 && Fold(word, word)) code = word.ToLowerInvariant();
            if (!remove && (word.Length == 0 || code.Length == 0 || word.StartsWith("#", StringComparison.Ordinal)
                || (word + code).IndexOfAny(new[] { '\t', '\r', '\n', '\0' }) >= 0))
                throw new InvalidOperationException("请输入有效的词条和编码。");
            if (Fold(word, code)) code = code.ToLowerInvariant();
            var entry = new WordEntry(word, code, "", -1);
            return Locked(() =>
            {
                var snapshot = CheckedRead(revision);
                var rows = Rows(snapshot.Lines).ToArray();
                var originals = rows.Where(row => row.Key == originalKey).ToArray();
                if (originalKey != null && originals.Length == 0)
                    throw new InvalidOperationException("这条词已不存在，请重新读取。");
                if (!remove && entry.Key != originalKey && rows.Any(row => row.Key == entry.Key))
                    throw new InvalidOperationException("这个词条和编码已经存在。");
                var old = originals.FirstOrDefault();
                string suffix = old == null ? "" : old.Suffix;
                if (!remove && (old == null || old.Weight != weight))
                {
                    int parsed = 0;
                    if (weight.Length > 0 && !int.TryParse(weight, NumberStyles.None, CultureInfo.InvariantCulture, out parsed))
                        throw new InvalidOperationException("权重请输入非负整数，留空使用默认权重。");
                    int tail = suffix.Length == 0 ? -1 : suffix.IndexOf('\t', 1);
                    string number = weight.Length == 0 ? "" : parsed.ToString(CultureInfo.InvariantCulture);
                    suffix = (number.Length > 0 || tail >= 0 ? "\t" + number : "") +
                        (tail >= 0 ? suffix.Substring(tail) : "");
                }
                var replacement = word + "\t" + code + suffix;
                var affected = new HashSet<int>(originals.Select(row => row.Line));
                var lines = new List<string>();
                for (int index = 0; index < snapshot.Lines.Length; index++)
                {
                    if (!affected.Contains(index)) lines.Add(snapshot.Lines[index]);
                    else if (!remove && index == old.Line) lines.Add(replacement);
                }
                if (old == null && !remove) lines.Add(replacement);
                Write(Pathname, EncodeLines(lines));
                return Read();
            });
        }

        internal T Locked<T>(Func<T> action)
        {
            var key = Hash(Utf8.GetBytes(Path.GetFullPath(Pathname).ToUpperInvariant()));
            using (var mutex = new Mutex(false, @"Local\HanLinWords." + key))
            {
                bool entered;
                try { entered = mutex.WaitOne(0); }
                catch (AbandonedMutexException) { entered = true; }
                if (!entered) throw new InvalidOperationException("已有词条操作正在运行。");
                try { return action(); }
                finally { mutex.ReleaseMutex(); }
            }
        }

        internal static string[] Normalize(string[] lines)
        {
            var output = (string[])lines.Clone();
            foreach (var group in Rows(lines).GroupBy(row => row.Key, StringComparer.Ordinal))
            {
                if (group.Select(row => row.Suffix).Distinct(StringComparer.Ordinal).Skip(1).Any()) continue;
                var first = group.First();
                output[first.Line] = first.Word + "\t" + first.Code + first.Suffix;
                foreach (var row in group.Skip(1)) output[row.Line] = null;
            }
            return output.Where(line => line != null).ToArray();
        }

        internal static byte[] MixedDictionary(IEnumerable<string> lines)
        {
            var output = new List<string> {
                "# Rime dictionary", "# encoding: utf-8", "#",
                "# 由 src/build-support/gen-mixed-tokens.ps1 生成，请勿手工编辑。",
                "# 内容：英文白名单（来自 custom_phrase.txt）+ 含数字型号。",
                "# 作用：让这些词能像音节一样参与 Rime 造句。",
                "---", "name: mixed_tokens", "version: \"1.0\"", "sort: by_weight", "...", ""
            };
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (string extra in new[] { "ESP32\tesp32", "STM32\tstm32", "杨寒淋\tyanghanlin" })
            {
                seen.Add(extra);
                output.Add(extra + "\t100000");
            }
            foreach (var line in lines)
            {
                var match = MixedToken.Match(line);
                if (!match.Success) continue;
                var code = match.Groups[2].Value.ToLowerInvariant();
                if ("aeiouv".IndexOf(code[0]) >= 0) continue;
                var pair = match.Groups[1].Value + "\t" + code;
                if (seen.Add(pair)) output.Add(pair + "\t1000");
            }
            return EncodeLines(output);
        }

        internal static byte[] EncodeLines(IEnumerable<string> lines)
        {
            var rows = lines as string[] ?? lines.ToArray();
            return Utf8.GetBytes(rows.Length == 0 ? "" : string.Join("\r\n", rows) + "\r\n");
        }

        internal static string Hash(byte[] bytes)
        {
            using (var sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "");
        }

        internal static void Write(string path, byte[] bytes)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                File.WriteAllBytes(temporary, bytes);
                if (File.Exists(path)) File.Replace(temporary, path, null);
                else File.Move(temporary, path);
            }
            finally { File.Delete(temporary); }
        }
    }
}
