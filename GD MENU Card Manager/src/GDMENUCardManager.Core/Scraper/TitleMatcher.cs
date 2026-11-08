using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace GDMENUCardManager.Core.Scraper
{
    /// <summary>
    /// Matches a game's title against Redump/No-Intro style names ("Crazy Taxi 2 (Europe) (En,Ja,Fr,De,Es)"): the part
    /// before the first bracket is compared after normalising, the bracket tags choose between regions and editions.
    /// </summary>
    public static class TitleMatcher
    {
        private static readonly Regex Brackets = new(@"\s*[\(\[][^\)\]]*[\)\]]", RegexOptions.Compiled);
        private static readonly Regex Tags = new(@"\(([^\)]*)\)", RegexOptions.Compiled);

        private static readonly Dictionary<string, string> Numerals = new()
        {
            ["ii"] = "2", ["iii"] = "3", ["iv"] = "4", ["v"] = "5", ["vi"] = "6", ["vii"] = "7", ["viii"] = "8",
            ["ix"] = "9", ["x"] = "10",
        };

        /// <summary>Folds accents away and keeps printable ASCII (the console's font has nothing else).</summary>
        public static string ToAscii(string text)
        {
            if (string.IsNullOrEmpty(text))
                return string.Empty;
            var sb = new StringBuilder(text.Length);
            foreach (var c in text.Normalize(NormalizationForm.FormD))
            {
                if (CharUnicodeInfo.GetUnicodeCategory(c) == UnicodeCategory.NonSpacingMark)
                    continue;
                switch (c)
                {
                    case '‘': case '’': case '´': case '`': sb.Append('\''); break;
                    case '“': case '”': case '«': case '»': sb.Append('"'); break;
                    case '–': case '—': case '−': sb.Append('-'); break;
                    case '…': sb.Append("..."); break;
                    case 'ß': sb.Append("ss"); break;
                    case 'Æ': sb.Append("AE"); break;
                    case 'æ': sb.Append("ae"); break;
                    case 'Œ': sb.Append("OE"); break;
                    case 'œ': sb.Append("oe"); break;
                    case 'Ø': sb.Append('O'); break;
                    case 'ø': sb.Append('o'); break;
                    case '™': case '®': case '©': break;
                    case '\r': break;
                    case '\n': case '\t': case ' ': sb.Append(' '); break;
                    default:
                        if (c >= 0x20 && c < 0x7F)
                            sb.Append(c);
                        break;
                }
            }
            return Regex.Replace(sb.ToString(), @" {2,}", " ").Trim();
        }

        /// <summary>The comparable core of a title: lower case words, no brackets, no punctuation, roman numerals as digits.</summary>
        public static string Normalize(string title)
        {
            if (string.IsNullOrWhiteSpace(title))
                return string.Empty;
            var t = ToAscii(Brackets.Replace(title, " ")).ToLowerInvariant();
            t = t.Replace("&", " and ").Replace("'", "");
            // "Title, The" and "The Title" compare alike
            t = Regex.Replace(t, @",\s*the\b", " ");
            t = Regex.Replace(t, @"[^a-z0-9]+", " ").Trim();
            // "and" is dropped: libretro file names have '_' where the title has '&'
            var words = t.Split(' ', StringSplitOptions.RemoveEmptyEntries)
                .Where(w => w != "and")
                .Select(w => Numerals.TryGetValue(w, out var n) ? n : w)
                .ToList();
            if (words.Count > 1 && words[0] == "the")
                words.RemoveAt(0);
            return string.Join(" ", words);
        }

        /// <summary>The bracket tags of a name: "(USA)", "(Disc 1)", "(Rev A)" ...</summary>
        public static IReadOnlyList<string> GetTags(string name) =>
            Tags.Matches(name ?? string.Empty).Select(m => m.Groups[1].Value.Trim()).ToList();

        /// <summary>0..1: how alike two titles are (1 = the same after normalising).</summary>
        public static double Similarity(string a, string b)
        {
            var na = Normalize(a);
            var nb = Normalize(b);
            if (na.Length == 0 || nb.Length == 0)
                return 0;
            if (na == nb)
                return 1;
            if (na.Replace(" ", "") == nb.Replace(" ", ""))
                return 0.98;

            // Words in common (Dice), and letter pairs for typos and joined words; numbers must agree
            var wa = na.Split(' ');
            var wb = nb.Split(' ');
            var numsA = wa.Where(w => w.All(char.IsDigit)).ToHashSet();
            var numsB = wb.Where(w => w.All(char.IsDigit)).ToHashSet();
            if (!numsA.SetEquals(numsB))
                return Math.Min(0.6, Dice(wa, wb)); // "Crazy Taxi" is not "Crazy Taxi 2"
            double words = Dice(wa, wb);
            double pairs = Dice(Pairs(na.Replace(" ", "")), Pairs(nb.Replace(" ", "")));
            return 0.5 * words + 0.5 * pairs;
        }

        private static double Dice(IEnumerable<string> a, IEnumerable<string> b)
        {
            var la = a.ToList();
            var lb = b.ToList();
            if (la.Count + lb.Count == 0)
                return 0;
            var pool = lb.ToList();
            int common = 0;
            foreach (var x in la)
            {
                int i = pool.IndexOf(x);
                if (i >= 0)
                {
                    common++;
                    pool.RemoveAt(i);
                }
            }
            return 2.0 * common / (la.Count + lb.Count);
        }

        private static IEnumerable<string> Pairs(string s)
        {
            for (int i = 0; i + 1 < s.Length; i++)
                yield return s.Substring(i, 2);
        }

        /// <summary>The regions to look for, best first, as ScreenScraper region codes.</summary>
        public static IReadOnlyList<string> RegionOrder(ScrapeRegion setting, string ipRegion)
        {
            var region = setting;
            if (region == ScrapeRegion.Auto)
            {
                var r = (ipRegion ?? string.Empty).ToUpperInvariant();
                // IP.BIN area symbols: J (Japan/Asia), U (America), E (Europe). One letter alone says the release.
                if (r.Contains('E') && !r.Contains('U') && !r.Contains('J'))
                    region = ScrapeRegion.Europe;
                else if (r.Contains('J') && !r.Contains('U') && !r.Contains('E'))
                    region = ScrapeRegion.Japan;
                else
                    region = ScrapeRegion.Usa;
            }
            return region switch
            {
                ScrapeRegion.Europe => new[] { "eu", "uk", "fr", "de", "es", "it", "wor", "us", "ss", "jp" },
                ScrapeRegion.Japan => new[] { "jp", "wor", "ss", "us", "eu" },
                _ => new[] { "us", "wor", "ss", "eu", "uk", "jp" },
            };
        }

        /// <summary>The region words of Redump names for a ScreenScraper region code.</summary>
        private static string[] RedumpRegions(string code) => code switch
        {
            "us" => new[] { "USA" },
            "eu" => new[] { "Europe" },
            "uk" => new[] { "UK", "Europe" },
            "fr" => new[] { "France", "Europe" },
            "de" => new[] { "Germany", "Europe" },
            "es" => new[] { "Spain", "Europe" },
            "it" => new[] { "Italy", "Europe" },
            "jp" => new[] { "Japan" },
            "wor" => new[] { "World" },
            _ => Array.Empty<string>(),
        };

        /// <summary>
        /// The best of `candidates` (Redump-style file names without extension) for the titles of a game, or null when
        /// none is alike enough. `disc` is the disc number of a multi-disc game (0: none or unknown).
        /// </summary>
        public static string BestMatch(IEnumerable<string> candidates, IEnumerable<string> titles,
            IReadOnlyList<string> regionOrder, int disc = 0, double threshold = 0.82)
        {
            var wanted = titles.Where(t => !string.IsNullOrWhiteSpace(t)).Distinct().ToList();
            if (wanted.Count == 0)
                return null;
            string best = null;
            double bestScore = 0;
            foreach (var name in candidates)
            {
                double sim = wanted.Max(t => Similarity(t, name));
                if (sim < threshold)
                    continue;
                double score = sim * 10;
                var tags = GetTags(name);
                var regionTag = tags.FirstOrDefault() ?? string.Empty;
                for (int i = 0; i < regionOrder.Count; i++)
                {
                    if (RedumpRegions(regionOrder[i]).Any(r => regionTag.Split(',').Select(x => x.Trim()).Contains(r)))
                    {
                        score += (regionOrder.Count - i) * 0.1;
                        break;
                    }
                }
                foreach (var tag in tags)
                {
                    var t = tag.ToLowerInvariant();
                    if (t.StartsWith("disc "))
                        score += disc > 0 && t == $"disc {disc}" ? 0.3 : -0.05;
                    else if (t is "beta" or "proto" or "demo" or "taikenban" or "sample" || t.StartsWith("beta") ||
                             t.Contains("passport") || t.Contains("trial"))
                        score -= 0.8; // a demo's cover only when nothing else matches
                    else if (t.StartsWith("rev "))
                        score -= 0.01;
                }
                if (score > bestScore)
                {
                    bestScore = score;
                    best = name;
                }
            }
            return best;
        }
    }
}
