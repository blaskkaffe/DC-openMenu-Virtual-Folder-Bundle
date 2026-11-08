using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace GDMENUCardManager.Core.Scraper
{
    /// <summary>
    /// The libretro thumbnail collection for the Dreamcast (github.com/libretro-thumbnails/Sega_-_Dreamcast): front covers
    /// named as Redump names them, free to use and without an account. The list of names is read once from GitHub and
    /// kept for a week.
    /// </summary>
    public sealed class LibretroThumbnailsClient
    {
        private const string Repo = "libretro-thumbnails/Sega_-_Dreamcast";
        private const string TreeUrl = "https://api.github.com/repos/" + Repo + "/git/trees/master?recursive=1";
        private const string RawBase = "https://raw.githubusercontent.com/" + Repo + "/master/";
        private const string BoxFolder = "Named_Boxarts/";

        private readonly HttpClient _http;
        private readonly string _cacheFile;
        private List<string> _names; // without folder and extension

        public LibretroThumbnailsClient(HttpClient http, string cacheFolder)
        {
            _http = http;
            _cacheFile = Path.Combine(cacheFolder, "libretro_dreamcast_boxarts.txt");
        }

        /// <summary>The cover names; empty when neither GitHub nor the kept list can be read.</summary>
        public async Task<IReadOnlyList<string>> GetNamesAsync(CancellationToken ct)
        {
            if (_names != null)
                return _names;
            try
            {
                if (File.Exists(_cacheFile) && File.GetLastWriteTimeUtc(_cacheFile) > DateTime.UtcNow.AddDays(-7))
                {
                    _names = File.ReadAllLines(_cacheFile).Where(l => l.Length > 0).ToList();
                    if (_names.Count > 0)
                        return _names;
                }
            }
            catch
            {
                // read it again from GitHub
            }

            try
            {
                using var req = new HttpRequestMessage(HttpMethod.Get, TreeUrl);
                req.Headers.Accept.ParseAdd("application/vnd.github+json");
                using var resp = await _http.SendAsync(req, ct);
                resp.EnsureSuccessStatusCode();
                _names = ParseTree(await resp.Content.ReadAsStringAsync());
                if (_names.Count > 0)
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(_cacheFile));
                    File.WriteAllLines(_cacheFile, _names);
                }
            }
            catch (Exception) when (!ct.IsCancellationRequested)
            {
                // An old list is better than none (GitHub allows 60 unauthenticated API calls an hour)
                try
                {
                    _names = File.Exists(_cacheFile) ? File.ReadAllLines(_cacheFile).Where(l => l.Length > 0).ToList() : new List<string>();
                }
                catch
                {
                    _names = new List<string>();
                }
            }
            return _names;
        }

        /// <summary>The box art names of a GitHub tree answer.</summary>
        public static List<string> ParseTree(string json)
        {
            using var doc = JsonDocument.Parse(json);
            var names = new List<string>();
            if (!doc.RootElement.TryGetProperty("tree", out var tree))
                return names;
            foreach (var e in tree.EnumerateArray())
            {
                var path = e.TryGetProperty("path", out var p) ? p.GetString() : null;
                if (path != null && path.StartsWith(BoxFolder, StringComparison.Ordinal) &&
                    path.EndsWith(".png", StringComparison.OrdinalIgnoreCase))
                    names.Add(path.Substring(BoxFolder.Length, path.Length - BoxFolder.Length - 4));
            }
            return names;
        }

        /// <summary>The cover for the titles of a game, or null when none matches well.</summary>
        public async Task<ScrapedGame> LookupAsync(IEnumerable<string> titles, IReadOnlyList<string> regionOrder, int disc,
            CancellationToken ct)
        {
            var names = await GetNamesAsync(ct);
            var match = TitleMatcher.BestMatch(names, titles, regionOrder, disc);
            if (match == null)
                return null;
            return new ScrapedGame
            {
                Source = "libretro thumbnails",
                MatchedName = match,
                FrontUrl = RawBase + BoxFolder + Uri.EscapeDataString(match) + ".png",
            };
        }
    }
}
