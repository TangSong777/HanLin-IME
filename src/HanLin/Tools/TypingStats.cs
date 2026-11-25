using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;

namespace HanLin.Tools
{
    internal sealed class TypingStats
    {
        internal readonly DateTime Date;
        internal readonly SortedDictionary<DateTime, long> Days = new SortedDictionary<DateTime, long>();
        internal long Today, Week, Month, Total;

        private TypingStats(DateTime today) { Date = today.Date; }

        internal long On(DateTime date)
        {
            long count;
            return Days.TryGetValue(date.Date, out count) ? count : 0;
        }

        internal static TypingStats Read(string filename, DateTime today)
        {
            var result = new TypingStats(today);
            var week = result.Date.AddDays(-((int)result.Date.DayOfWeek + 6) % 7);
            var month = new DateTime(today.Year, today.Month, 1);
            try
            {
                using (var file = new FileStream(filename, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                using (var reader = new StreamReader(file))
                {
                    string line;
                    while ((line = reader.ReadLine()) != null)
                    {
                        var fields = line.Split('\t');
                        DateTime date;
                        long count;
                        if (fields.Length != 2 ||
                            !DateTime.TryParseExact(fields[0], "yyyy-MM-dd", CultureInfo.InvariantCulture, DateTimeStyles.None, out date) ||
                            date > result.Date ||
                            !long.TryParse(fields[1], NumberStyles.None, CultureInfo.InvariantCulture, out count)) continue;
                        result.Days[date] = Add(result.On(date), count);
                        result.Total = Add(result.Total, count);
                        if (date == result.Date) result.Today = Add(result.Today, count);
                        if (date >= week) result.Week = Add(result.Week, count);
                        if (date >= month) result.Month = Add(result.Month, count);
                    }
                }
            }
            catch (FileNotFoundException) { }
            catch (DirectoryNotFoundException) { }
            return result;
        }

        private static long Add(long left, long right) { return left > long.MaxValue - right ? long.MaxValue : left + right; }
    }
}
