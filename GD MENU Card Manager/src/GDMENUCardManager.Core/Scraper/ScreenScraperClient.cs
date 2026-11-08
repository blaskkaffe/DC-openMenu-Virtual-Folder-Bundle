using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace GDMENUCardManager.Core.Scraper
{
    /// <summary>
    /// ScreenScraper API v2 (https://www.screenscraper.fr/webapi2.php): jeuInfos.php finds a Dreamcast game (system 23) by
    /// its serial and file name; jeuRecherche.php by its title when that fails. One request at a time, as a free account
    /// allows.
    /// </summary>
    public sealed class ScreenScraperClient
    {
        public const int DreamcastSystemId = 23;
        private const string Api = "https://api.screenscraper.fr/api2/";

        private readonly HttpClient _http;
        private readonly string _devId, _devPassword, _user, _password, _language;
        private DateTime _nextRequest = DateTime.MinValue;

        /// <summary>Requests used today and the account's daily limit, when the server said (0 = not known).</summary>
        public int RequestsToday { get; private set; }
        public int MaxRequestsPerDay { get; private set; }

        public bool IsConfigured => !string.IsNullOrEmpty(_devId) && !string.IsNullOrEmpty(_devPassword);

        public ScreenScraperClient(HttpClient http, ScraperSettings settings)
        {
            _http = http;
            _devId = string.IsNullOrWhiteSpace(settings.DeveloperId) ? ScreenScraperDevCredentials.DevId : settings.DeveloperId.Trim();
            _devPassword = string.IsNullOrWhiteSpace(settings.DeveloperId) ? ScreenScraperDevCredentials.DevPassword : settings.DeveloperPassword;
            _user = settings.ScreenScraperUser?.Trim() ?? string.Empty;
            _password = settings.ScreenScraperPassword ?? string.Empty;
            _language = string.IsNullOrWhiteSpace(settings.Language) ? "en" : settings.Language.Trim().ToLowerInvariant();
        }

        private string Credentials()
        {
            var q = $"devid={Uri.EscapeDataString(_devId)}&devpassword={Uri.EscapeDataString(_devPassword)}" +
                    $"&softname={Uri.EscapeDataString(ScreenScraperDevCredentials.SoftwareName)}&output=json";
            if (!string.IsNullOrEmpty(_user))
                q += $"&ssid={Uri.EscapeDataString(_user)}&sspassword={Uri.EscapeDataString(_password)}";
            return q;
        }

        /// <summary>
        /// Logs in (ssuserInfos.php) and returns the account's limits, as EmulationStation's and Recalbox's account
        /// screens do. Throws ScraperException with a readable reason when the login or the developer ID is refused.
        /// </summary>
        public async Task<ScreenScraperAccount> CheckAccountAsync(CancellationToken ct)
        {
            if (!IsConfigured)
                throw new ScraperException("No ScreenScraper developer ID: enter one under Advanced.", true);
            if (string.IsNullOrEmpty(_user))
                throw new ScraperException("Enter your ScreenScraper user name and password.", true);
            var json = await GetAsync($"{Api}ssuserInfos.php?{Credentials()}", ct);
            if (json == null)
                throw new ScraperException("ScreenScraper does not know this user name.", true);
            var account = ParseAccount(json);
            if (account == null)
                throw new ScraperException("ScreenScraper's answer had no account in it.", false);
            return account;
        }

        /// <summary>The account of an ssuserInfos.php answer (or of any answer that carries "ssuser").</summary>
        public ScreenScraperAccount ParseAccount(string json)
        {
            using var doc = JsonDocument.Parse(json);
            if (!doc.RootElement.TryGetProperty("response", out var response) ||
                !response.TryGetProperty("ssuser", out var user) || user.ValueKind != JsonValueKind.Object)
                return null;
            ReadUser(response);
            return new ScreenScraperAccount
            {
                User = Str(user, "id") ?? _user,
                Level = Int(Str(user, "niveau")),
                MaxThreads = Math.Max(1, Int(Str(user, "maxthreads"))),
                RequestsToday = Int(Str(user, "requeststoday")),
                MaxRequestsPerDay = Int(Str(user, "maxrequestsperday")),
                FavouriteRegion = Str(user, "favregion") ?? string.Empty,
            };
        }

        /// <summary>The game, or null when ScreenScraper does not know it. Throws ScraperException when it cannot answer.</summary>
        public async Task<(ScrapedGame game, string rawJson)> LookupAsync(string serial, string fileName, string title,
            IReadOnlyList<string> regionOrder, CancellationToken ct)
        {
            var q = $"{Api}jeuInfos.php?{Credentials()}&systemeid={DreamcastSystemId}&romtype=rom";
            if (!string.IsNullOrWhiteSpace(serial))
                q += $"&serialnum={Uri.EscapeDataString(serial.Trim())}";
            if (!string.IsNullOrWhiteSpace(fileName))
                q += $"&romnom={Uri.EscapeDataString(fileName)}";
            var json = await GetAsync(q, ct);
            if (json != null)
            {
                var game = Parse(json, regionOrder, _language);
                if (game != null)
                    return (game, json);
            }

            if (string.IsNullOrWhiteSpace(title))
                return (null, null);
            var search = $"{Api}jeuRecherche.php?{Credentials()}&systemeid={DreamcastSystemId}&recherche={Uri.EscapeDataString(title)}";
            json = await GetAsync(search, ct);
            if (json == null)
                return (null, null);
            var found = ParseSearch(json, title, regionOrder, _language);
            return (found, found != null ? json : null);
        }

        /// <summary>The answer's text, or null for "not found". Throws ScraperException for the other errors.</summary>
        private async Task<string> GetAsync(string url, CancellationToken ct)
        {
            for (int attempt = 0; attempt < 3; attempt++)
            {
                var wait = _nextRequest - DateTime.UtcNow;
                if (wait > TimeSpan.Zero)
                    await Task.Delay(wait, ct);
                _nextRequest = DateTime.UtcNow.AddMilliseconds(1200); // a free account has one thread: stay gentle

                HttpResponseMessage resp;
                try
                {
                    resp = await _http.GetAsync(url, ct);
                }
                catch (HttpRequestException ex)
                {
                    throw new ScraperException($"ScreenScraper could not be reached ({ex.Message})", true);
                }
                catch (TaskCanceledException) when (!ct.IsCancellationRequested)
                {
                    throw new ScraperException("ScreenScraper did not answer in time", false);
                }

                using (resp)
                {
                    var body = await resp.Content.ReadAsStringAsync();
                    switch ((int)resp.StatusCode)
                    {
                        case 200:
                            if (body.TrimStart().StartsWith("{"))
                                return body;
                            // A 200 with an error text instead of JSON
                            if (body.IndexOf("non trouv", StringComparison.OrdinalIgnoreCase) >= 0)
                                return null;
                            throw new ScraperException($"ScreenScraper: {Short(body)}", false);
                        case 404:
                            return null;
                        case 429: // too many threads: wait and try again
                            await Task.Delay(TimeSpan.FromSeconds(5 * (attempt + 1)), ct);
                            continue;
                        case 401:
                            throw new ScraperException("ScreenScraper's API is closed for non-members right now (server load). Try later or log in.", true);
                        case 403:
                            throw new ScraperException("ScreenScraper refused the login: check the user name and password (and the developer ID under Advanced).", true);
                        case 423:
                            throw new ScraperException("ScreenScraper's API is closed right now. Try again later.", true);
                        case 426:
                            throw new ScraperException("ScreenScraper refuses this program's developer ID. Enter your own under Advanced.", true);
                        case 430:
                            throw new ScraperException("Your ScreenScraper quota for today is used up. It resets at midnight (French time).", true);
                        case 431:
                            throw new ScraperException("ScreenScraper stopped answering: too many games not found today.", true);
                        default:
                            throw new ScraperException($"ScreenScraper answered {(int)resp.StatusCode}: {Short(body)}", (int)resp.StatusCode >= 500);
                    }
                }
            }
            throw new ScraperException("ScreenScraper is busy (too many requests). Try again later.", true);
        }

        private static string Short(string s)
        {
            s = (s ?? string.Empty).Trim();
            return s.Length > 160 ? s.Substring(0, 160) + "..." : s;
        }

        private void ReadUser(JsonElement response)
        {
            if (response.TryGetProperty("ssuser", out var user))
            {
                RequestsToday = Int(Str(user, "requeststoday"));
                MaxRequestsPerDay = Int(Str(user, "maxrequestsperday"));
            }
        }

        /// <summary>The game of a jeuInfos.php answer.</summary>
        public ScrapedGame Parse(string json, IReadOnlyList<string> regionOrder, string language)
        {
            using var doc = JsonDocument.Parse(json);
            if (!doc.RootElement.TryGetProperty("response", out var response))
                return null;
            ReadUser(response);
            return response.TryGetProperty("jeu", out var jeu) && jeu.ValueKind == JsonValueKind.Object
                ? FromJeu(jeu, regionOrder, language)
                : null;
        }

        /// <summary>The best of a jeuRecherche.php answer's games for `title`.</summary>
        public ScrapedGame ParseSearch(string json, string title, IReadOnlyList<string> regionOrder, string language)
        {
            using var doc = JsonDocument.Parse(json);
            if (!doc.RootElement.TryGetProperty("response", out var response))
                return null;
            ReadUser(response);
            if (!response.TryGetProperty("jeux", out var jeux) || jeux.ValueKind != JsonValueKind.Array)
                return null;
            ScrapedGame best = null;
            double bestSim = 0.82; // a search answer must really be the game
            foreach (var jeu in jeux.EnumerateArray())
            {
                if (jeu.ValueKind != JsonValueKind.Object || !jeu.TryGetProperty("noms", out _))
                    continue; // an empty game marks "nothing found"
                var g = FromJeu(jeu, regionOrder, language);
                double sim = g.Names.Values.Select(n => TitleMatcher.Similarity(title, n)).DefaultIfEmpty(0).Max();
                if (sim > bestSim)
                {
                    bestSim = sim;
                    best = g;
                }
            }
            return best;
        }

        private static ScrapedGame FromJeu(JsonElement jeu, IReadOnlyList<string> regionOrder, string language)
        {
            var g = new ScrapedGame { Source = "ScreenScraper" };
            if (jeu.TryGetProperty("noms", out var noms) && noms.ValueKind == JsonValueKind.Array)
            {
                foreach (var n in noms.EnumerateArray())
                {
                    var region = Str(n, "region");
                    var text = Str(n, "text");
                    if (!string.IsNullOrEmpty(region) && !string.IsNullOrEmpty(text) && !g.Names.ContainsKey(region))
                        g.Names[region] = text;
                }
            }
            g.MatchedName = ByRegion(g.Names, regionOrder) ?? g.Names.Values.FirstOrDefault() ?? string.Empty;

            g.Synopsis = ByLanguage(jeu, "synopsis", language);
            g.Developer = Text(jeu, "developpeur");
            g.Publisher = Text(jeu, "editeur");
            g.Players = Text(jeu, "joueurs");
            g.Rating = Int(Text(jeu, "note"));

            if (jeu.TryGetProperty("dates", out var dates) && dates.ValueKind == JsonValueKind.Array)
            {
                var byRegion = new Dictionary<string, string>();
                foreach (var d in dates.EnumerateArray())
                {
                    var region = Str(d, "region");
                    var text = Str(d, "text");
                    if (!string.IsNullOrEmpty(region) && !string.IsNullOrEmpty(text) && !byRegion.ContainsKey(region))
                        byRegion[region] = text;
                }
                g.ReleaseDate = ByRegion(byRegion, regionOrder) ?? byRegion.Values.OrderBy(x => x).FirstOrDefault();
            }

            if (jeu.TryGetProperty("genres", out var genres) && genres.ValueKind == JsonValueKind.Array)
            {
                foreach (var genre in genres.EnumerateArray())
                {
                    var name = ByLanguage(genre, "noms", language) ?? ByLanguage(genre, "noms", "en");
                    if (!string.IsNullOrWhiteSpace(name) && !g.Genres.Contains(name))
                        g.Genres.Add(name);
                }
            }

            if (jeu.TryGetProperty("medias", out var medias) && medias.ValueKind == JsonValueKind.Array)
            {
                g.FrontUrl = Media(medias, regionOrder, "box-2D");
                g.BackUrl = Media(medias, regionOrder, "box-2D-back");
                g.DiscUrl = Media(medias, regionOrder, "support-2D");
            }
            return g;
        }

        /// <summary>The URL of the best region's picture of a type, shrunk on the server to what a DAT can use.</summary>
        private static string Media(JsonElement medias, IReadOnlyList<string> regionOrder, string type)
        {
            var byRegion = new Dictionary<string, string>();
            foreach (var m in medias.EnumerateArray())
            {
                if (Str(m, "type") != type)
                    continue;
                var url = Str(m, "url");
                if (string.IsNullOrEmpty(url))
                    continue;
                var region = Str(m, "region");
                if (string.IsNullOrEmpty(region))
                    region = "wor";
                if (!byRegion.ContainsKey(region))
                    byRegion[region] = url;
            }
            var pick = ByRegion(byRegion, regionOrder) ?? byRegion.Values.FirstOrDefault();
            if (pick != null && pick.Contains("mediaJeu.php") && !pick.Contains("maxwidth="))
                pick += "&maxwidth=512&maxheight=512";
            return pick;
        }

        private static string ByRegion(Dictionary<string, string> values, IReadOnlyList<string> regionOrder)
        {
            foreach (var r in regionOrder)
                if (values.TryGetValue(r, out var v))
                    return v;
            return null;
        }

        /// <summary>The text of a [{langue, text}] list in a language, else English, else the first.</summary>
        private static string ByLanguage(JsonElement parent, string property, string language)
        {
            if (!parent.TryGetProperty(property, out var list) || list.ValueKind != JsonValueKind.Array)
                return null;
            string english = null, first = null;
            foreach (var e in list.EnumerateArray())
            {
                var lang = Str(e, "langue");
                var text = Str(e, "text");
                if (string.IsNullOrWhiteSpace(text))
                    continue;
                if (lang == language)
                    return text;
                if (lang == "en")
                    english ??= text;
                first ??= text;
            }
            return english ?? first;
        }

        /// <summary>{"id": .., "text": ".."} objects.</summary>
        private static string Text(JsonElement parent, string property) =>
            parent.TryGetProperty(property, out var o) && o.ValueKind == JsonValueKind.Object ? Str(o, "text") : null;

        private static string Str(JsonElement e, string property)
        {
            if (e.ValueKind != JsonValueKind.Object || !e.TryGetProperty(property, out var v))
                return null;
            return v.ValueKind switch
            {
                JsonValueKind.String => v.GetString(),
                JsonValueKind.Number => v.GetRawText(),
                _ => null,
            };
        }

        private static int Int(string s) =>
            int.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out var v) ? v : 0;
    }
}
