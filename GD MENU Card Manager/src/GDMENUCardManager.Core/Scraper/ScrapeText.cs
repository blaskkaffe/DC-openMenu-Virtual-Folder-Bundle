using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace GDMENUCardManager.Core.Scraper
{
    /// <summary>Turns scraped text into META.DAT and INFO.DAT entries (ASCII only: the console's font has nothing else).</summary>
    public static class ScrapeText
    {
        /// <summary>META.DAT entry: description, genre bits and players where empty (or all, with Overwrite).</summary>
        public static byte[] BuildMeta(byte[] existing, ScrapedGame g, bool overwrite)
        {
            var raw = existing != null && existing.Length == MetaDatManager.EntrySize
                ? (byte[])existing.Clone()
                : new byte[MetaDatManager.EntrySize];
            int players = MaxPlayers(g.Players);
            if (players > 0 && (overwrite || raw[0] == 0))
                raw[0] = (byte)Math.Min(players, 255);
            ushort genre = GenreBits(g.Genres);
            if (genre != 0 && (overwrite || BitConverter.ToUInt16(raw, 5) == 0))
                BitConverter.GetBytes(genre).CopyTo(raw, 5);
            bool hasDescription = raw[8] != 0;
            var synopsis = CleanSynopsis(g.Synopsis, MetaDatManager.DescriptionLength - 1);
            if (synopsis.Length > 0 && (overwrite || !hasDescription))
            {
                Array.Clear(raw, 8, MetaDatManager.DescriptionLength);
                Encoding.ASCII.GetBytes(synopsis).CopyTo(raw, 8);
            }
            return raw;
        }

        public static InfoDatEntry BuildInfo(ScrapedGame g, IReadOnlyList<string> regions)
        {
            var e = new InfoDatEntry
            {
                Developer = g.Developer ?? string.Empty,
                Publisher = g.Publisher ?? string.Empty,
                Genre = string.Join(", ", g.Genres),
                Players = g.Players ?? string.Empty,
                Rating = (byte)Math.Clamp(g.Rating, 0, 20),
                Title = (regions.Select(r => g.Names.TryGetValue(r, out var n) ? n : null).FirstOrDefault(n => n != null)
                         ?? g.MatchedName) ?? string.Empty,
            };
            var m = Regex.Match(g.ReleaseDate ?? string.Empty, @"^(\d{4})(?:-(\d{1,2}))?(?:-(\d{1,2}))?");
            if (m.Success)
            {
                e.Year = ushort.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
                if (m.Groups[2].Success)
                    e.Month = byte.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture);
                if (m.Groups[3].Success)
                    e.Day = byte.Parse(m.Groups[3].Value, CultureInfo.InvariantCulture);
            }
            return e.IsEmpty ? null : e;
        }

        /// <summary>"1-4" -> 4, "2" -> 2, "" -> 0.</summary>
        public static int MaxPlayers(string players)
        {
            if (string.IsNullOrWhiteSpace(players))
                return 0;
            return Regex.Matches(players, @"\d+").Select(x => int.TryParse(x.Value, out var v) ? v : 0).DefaultIfEmpty(0).Max();
        }

        /// <summary>openMenu's genre bits (db_item.h FLAGS_GENRE) for genre names in English or French.</summary>
        public static ushort GenreBits(IEnumerable<string> genres)
        {
            ushort bits = 0;
            foreach (var raw in genres)
            {
                var g = TitleMatcher.ToAscii(raw).ToLowerInvariant();
                if (g.Contains("shoot'em") || g.Contains("shoot 'em") || g.Contains("shmup") || g.Contains("shoot-em"))
                    bits |= 1 << 11;
                else if (g.Contains("light gun") || g.Contains("lightgun") || g.Contains("gun"))
                    bits |= 1 << 4;
                else if (g.Contains("shooter") || g.Contains("fps") || g.Contains("tir"))
                    bits |= 1 << 6;
                if (g.Contains("action"))
                    bits |= 1 << 0;
                if (g.Contains("racing") || g.Contains("driving") || g.Contains("course") || g.Contains("pilotage"))
                    bits |= 1 << 1;
                if (g.Contains("simulation"))
                    bits |= 1 << 2;
                if (g.Contains("sport"))
                    bits |= 1 << 3;
                if (g.Contains("fight") || g.Contains("combat") || g.Contains("beat"))
                    bits |= 1 << 5;
                if (g.Contains("survival") || g.Contains("horror"))
                    bits |= 1 << 7;
                if (g.Contains("adventure") || g.Contains("aventure"))
                    bits |= 1 << 8;
                if (g.Contains("platform") || g.Contains("plate-forme") || g.Contains("plateforme"))
                    bits |= 1 << 9;
                if (g.Contains("role") || g.Contains("rpg") || g.Contains("jeu de role"))
                    bits |= 1 << 10;
                if (g.Contains("strateg"))
                    bits |= 1 << 12;
                if (g.Contains("puzzle") || g.Contains("reflexion"))
                    bits |= 1 << 13;
                if (g.Contains("arcade") || g.Contains("pinball") || g.Contains("flipper"))
                    bits |= 1 << 14;
                if (g.Contains("music") || g.Contains("rhythm") || g.Contains("musique") || g.Contains("danc"))
                    bits |= 1 << 15;
            }
            return bits;
        }

        /// <summary>ASCII, one paragraph, cut at the last sentence or word that fits.</summary>
        public static string CleanSynopsis(string text, int max)
        {
            var t = TitleMatcher.ToAscii(System.Net.WebUtility.HtmlDecode(text ?? string.Empty));
            t = Regex.Replace(t, @"\s+", " ").Trim();
            if (t.Length <= max)
                return t;
            var cut = t.Substring(0, max - 3);
            int sentence = cut.LastIndexOf(". ", StringComparison.Ordinal);
            if (sentence > max / 2)
                return cut.Substring(0, sentence + 1);
            int space = cut.LastIndexOf(' ');
            return (space > max / 2 ? cut.Substring(0, space) : cut).TrimEnd(',', ';', ':', '-', ' ') + "...";
        }
    }
}
