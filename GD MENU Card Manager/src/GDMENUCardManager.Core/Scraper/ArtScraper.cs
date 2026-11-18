using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;
using SixLabors.ImageSharp.Processing;

namespace GDMENUCardManager.Core.Scraper
{
    /// <summary>What a run found for one game, ready to be put into the DATs (Apply, on the UI thread).</summary>
    public sealed class ScrapeOutcome
    {
        public GdItem Item { get; set; }
        public string Serial { get; set; }
        public byte[] BoxPvr { get; set; }    // 256x256, BOX.DAT
        public byte[] IconPvr { get; set; }   // 128x128, ICON.DAT
        public byte[] BackPvr { get; set; }   // 256x256, BACK.DAT
        public byte[] DiscPvr { get; set; }   // 128x128, DISCART.DAT
        public byte[] MetaRaw { get; set; }   // 384 bytes, META.DAT
        public InfoDatEntry Info { get; set; }

        public bool HasAnything =>
            BoxPvr != null || BackPvr != null || DiscPvr != null || MetaRaw != null || Info != null;
    }

    public sealed class ScrapeProgress
    {
        public int Done { get; set; }
        public int Total { get; set; }
        public string Line { get; set; }
    }

    /// <summary>
    /// Looks the games up (ScreenScraper first, libretro thumbnails for front covers it lacks), downloads the pictures
    /// into a cache, shrinks and converts them to the Dreamcast's PVR format, and turns the text into META.DAT and
    /// INFO.DAT entries. RunAsync only reads the DATs; Apply writes the outcomes into them in memory, as one undo step
    /// (saved with Save Changes, like hand-picked artwork).
    /// </summary>
    public sealed class ArtScraper
    {
        private readonly Manager _manager;
        private readonly ScraperSettings _settings;
        private readonly HttpClient _http;
        private readonly ScreenScraperClient _ss;
        private readonly LibretroThumbnailsClient _libretro;
        private bool _ssStopped;

        public ArtScraper(Manager manager, ScraperSettings settings, HttpClient http = null)
        {
            _manager = manager;
            _settings = settings;
            _http = http ?? CreateHttpClient();
            _ss = new ScreenScraperClient(_http, settings);
            _libretro = new LibretroThumbnailsClient(_http, ScraperSettings.CacheFolder);
        }

        public static HttpClient CreateHttpClient()
        {
            var http = new HttpClient { Timeout = TimeSpan.FromSeconds(45) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("GDMenuCardManager-Scraper/1.0");
            return http;
        }

        /// <summary>The games of a scope, one per serial (the discs of a set share their art).</summary>
        public static List<GdItem> SelectItems(Manager manager, ScrapeScope scope, IEnumerable<GdItem> selected)
        {
            var source = scope == ScrapeScope.Selected ? (selected ?? Enumerable.Empty<GdItem>()) : manager.ItemList;
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var list = new List<GdItem>();
            foreach (var item in source)
            {
                if (item == null || !item.CanManageArtwork)
                    continue;
                if (scope == ScrapeScope.MissingArtwork && item.HasArtwork && !MissingAnything(manager, item))
                    continue;
                if (seen.Add(BoxDatManager.NormalizeSerial(item.ProductNumber)))
                    list.Add(item);
            }
            return list;
        }

        /// <summary>A back cover, disc label or text is missing (front art is checked through HasArtwork).</summary>
        private static bool MissingAnything(Manager m, GdItem item)
        {
            var s = item.ProductNumber;
            return m.BackDat?.GetPvrDataForSerial(s) == null || m.DiscArtDat?.GetPvrDataForSerial(s) == null ||
                   m.InfoDat?.Get(s) == null || string.IsNullOrEmpty(m.MetaDat?.GetEntryForSerial(s)?.Description);
        }

        /// <summary>Scrapes the games one after the other. Cancelling stops after the current game and returns what was found.</summary>
        public async Task<List<ScrapeOutcome>> RunAsync(IList<GdItem> items, IProgress<ScrapeProgress> progress,
            CancellationToken ct)
        {
            var outcomes = new List<ScrapeOutcome>();
            int done = 0;
            void Report(string line) => progress?.Report(new ScrapeProgress { Done = done, Total = items.Count, Line = line });

            bool ssUsable = _settings.UseScreenScraper && _ss.IsConfigured;
            if (_settings.UseScreenScraper && !_ss.IsConfigured)
                Report("ScreenScraper skipped: no developer ID (enter one under Advanced). Only libretro front covers can be found.");
            if (!ssUsable && !_settings.UseLibretro)
            {
                Report("No source to ask.");
                return outcomes;
            }

            foreach (var item in items)
            {
                if (ct.IsCancellationRequested)
                    break; // what was found so far is still returned and can be applied
                ScrapeOutcome outcome = null;
                try
                {
                    outcome = await ScrapeOneAsync(item, ssUsable, Report, ct);
                }
                catch (OperationCanceledException) when (ct.IsCancellationRequested)
                {
                    break;
                }
                catch (ScraperException ex)
                {
                    Report($"{item.Name}: {ex.Message}");
                    if (ex.StopSource)
                    {
                        _ssStopped = true;
                        if (!_settings.UseLibretro)
                            break;
                    }
                }
                catch (Exception ex)
                {
                    Report($"{item.Name}: error, {ex.Message}");
                }
                if (outcome?.HasAnything == true)
                    outcomes.Add(outcome);
                done++;
                Report(null);
            }
            if (_ss.MaxRequestsPerDay > 0)
                Report($"ScreenScraper requests today: {_ss.RequestsToday} of {_ss.MaxRequestsPerDay}.");
            return outcomes;
        }

        private async Task<ScrapeOutcome> ScrapeOneAsync(GdItem item, bool ssUsable, Action<string> report, CancellationToken ct)
        {
            var serial = item.ProductNumber;
            var key = BoxDatManager.NormalizeSerial(serial);
            bool over = _settings.Overwrite;
            bool needFront = _settings.FrontCover && (over || _manager.BoxDat?.GetPvrDataForSerial(serial) == null);
            bool needBack = _settings.BackCover && (over || _manager.BackDat?.GetPvrDataForSerial(serial) == null);
            bool needDisc = _settings.DiscLabel && (over || _manager.DiscArtDat?.GetPvrDataForSerial(serial) == null);
            var meta = _manager.MetaDat?.GetEntryForSerial(serial);
            bool needInfo = _settings.Info && (over || _manager.InfoDat?.Get(serial) == null || string.IsNullOrEmpty(meta?.Description));
            if (!needFront && !needBack && !needDisc && !needInfo)
            {
                report($"{item.Name}: has everything already");
                return null;
            }

            var regions = TitleMatcher.RegionOrder(_settings.Region, item.Ip?.Region);
            var titles = new List<string> { item.Name, item.Ip?.Name, Path.GetFileNameWithoutExtension(item.ImageFile ?? string.Empty) };
            titles = titles.Where(t => !string.IsNullOrWhiteSpace(t)).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            var cacheDir = Path.Combine(ScraperSettings.CacheFolder, key);
            Directory.CreateDirectory(cacheDir);

            // ScreenScraper: the answer is kept, so a second run costs no requests
            ScrapedGame game = null;
            if (ssUsable && !_ssStopped && (needBack || needDisc || needInfo || needFront))
            {
                var cached = Path.Combine(cacheDir, "screenscraper.json");
                if (File.Exists(cached) && File.GetLastWriteTimeUtc(cached) > DateTime.UtcNow.AddDays(-30))
                    game = _ss.Parse(File.ReadAllText(cached), regions, _settings.Language);
                if (game == null)
                {
                    var (found, json) = await _ss.LookupAsync(serial, Path.GetFileName(item.ImageFile ?? string.Empty),
                        titles.FirstOrDefault(), regions, ct);
                    game = found;
                    if (json != null && json.Contains("\"jeu\"")) // keep only single-game answers
                        File.WriteAllText(cached, json);
                }
            }

            // libretro: front covers ScreenScraper did not have
            if (needFront && game?.FrontUrl == null && _settings.UseLibretro)
            {
                var names = titles.Concat(game?.Names.Values ?? Enumerable.Empty<string>()).ToList();
                var lr = await _libretro.LookupAsync(names, regions, DiscNumber(item), ct);
                if (lr != null)
                {
                    game ??= new ScrapedGame { Source = lr.Source, MatchedName = lr.MatchedName };
                    game.FrontUrl = lr.FrontUrl;
                    if (game.Source != lr.Source)
                        game.Source += " + " + lr.Source;
                }
            }

            if (game == null)
            {
                report($"{item.Name}: not found");
                return null;
            }

            var outcome = new ScrapeOutcome { Item = item, Serial = serial };
            var got = new List<string>();
            if (needFront && game.FrontUrl != null)
            {
                var file = await DownloadAsync(game.FrontUrl, cacheDir, "front", ct);
                if (file != null)
                {
                    outcome.BoxPvr = await Task.Run(() => PvrEncoder.EncodeFromFile(file), ct);
                    outcome.IconPvr = await Task.Run(() => PvrEncoder.EncodeIconFromFile(file), ct);
                    got.Add("front");
                }
            }
            if (needBack && game.BackUrl != null)
            {
                var file = await DownloadAsync(game.BackUrl, cacheDir, "back", ct);
                if (file != null)
                {
                    outcome.BackPvr = await Task.Run(() => PvrEncoder.EncodeFromFile(file), ct);
                    got.Add("back");
                }
            }
            if (needDisc && game.DiscUrl != null)
            {
                var file = await DownloadAsync(game.DiscUrl, cacheDir, "disc", ct);
                if (file != null)
                {
                    outcome.DiscPvr = await Task.Run(() => EncodeDiscLabel(file), ct);
                    got.Add("disc");
                }
            }
            if (needInfo && game.HasAnyText)
            {
                outcome.MetaRaw = ScrapeText.BuildMeta(meta?.RawData, game, over);
                outcome.Info = ScrapeText.BuildInfo(game, regions);
                got.Add("info");
            }

            var match = string.IsNullOrEmpty(game.MatchedName) || game.MatchedName == item.Name ? "" : $" as \"{game.MatchedName}\"";
            report(got.Count > 0
                ? $"{item.Name}: {string.Join(", ", got)} from {game.Source}{match}"
                : $"{item.Name}: found{match} on {game.Source}, but not the art that is missing");
            return outcome;
        }

        /// <summary>The disc number of a multi-disc game from IP.BIN ("1/2"), 0 when not known.</summary>
        private static int DiscNumber(GdItem item)
        {
            var disc = item.Ip?.Disc;
            if (string.IsNullOrEmpty(disc))
                return 0;
            var m = Regex.Match(disc, @"^(\d+)\s*/\s*(\d+)$");
            return m.Success && int.Parse(m.Groups[2].Value) > 1 ? int.Parse(m.Groups[1].Value) : 0;
        }

        private async Task<string> DownloadAsync(string url, string dir, string name, CancellationToken ct)
        {
            var existing = Directory.GetFiles(dir, name + ".*").FirstOrDefault();
            if (existing != null && new FileInfo(existing).Length > 0 && File.Exists(existing + ".url") &&
                File.ReadAllText(existing + ".url") == url)
                return existing;
            try
            {
                using var resp = await _http.GetAsync(url, ct);
                if (!resp.IsSuccessStatusCode)
                    return null;
                var bytes = await resp.Content.ReadAsByteArrayAsync();
                var ext = SniffImage(bytes);
                if (ext == null)
                    return null; // an error page, not a picture
                foreach (var old in Directory.GetFiles(dir, name + ".*"))
                    File.Delete(old);
                var path = Path.Combine(dir, name + ext);
                File.WriteAllBytes(path, bytes);
                File.WriteAllText(path + ".url", url);
                return path;
            }
            catch (HttpRequestException)
            {
                return null;
            }
        }

        private static string SniffImage(byte[] b)
        {
            if (b.Length > 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G')
                return ".png";
            if (b.Length > 3 && b[0] == 0xFF && b[1] == 0xD8)
                return ".jpg";
            if (b.Length > 12 && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F' && b[8] == 'W' && b[9] == 'E')
                return ".webp";
            if (b.Length > 6 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F')
                return ".gif";
            return null;
        }

        /// <summary>
        /// A disc picture is round: it is centred on a transparent square (not stretched like the covers) before it
        /// is shrunk to 128x128, so the menu's disc model gets a round label.
        /// </summary>
        public static byte[] EncodeDiscLabel(string file)
        {
            using var image = Image.Load<Bgra32>(file);
            int side = Math.Max(image.Width, image.Height);
            if (image.Width != image.Height)
            {
                image.Mutate(x => x.Resize(new ResizeOptions
                {
                    Size = new Size(side, side),
                    Mode = ResizeMode.BoxPad,
                    PadColor = Color.Transparent,
                }));
            }
            var pixels = new byte[image.Width * image.Height * 4];
            image.CopyPixelDataTo(pixels);
            return PvrEncoder.EncodeIconFromPixels(pixels, image.Width, image.Height);
        }

        // ---- writing into the DATs ----------------------------------------------------------------------------------

        /// <summary>Puts the outcomes into the DATs (in memory) as one undo step. Call on the UI thread.</summary>
        public static int Apply(Manager manager, IReadOnlyList<ScrapeOutcome> outcomes)
        {
            var op = new ScrapeBatchOperation(manager);
            foreach (var o in outcomes.Where(o => o.HasAnything))
            {
                var before = ScrapeSnapshot.Take(manager, o.Serial);
                if (o.BoxPvr != null)
                {
                    manager.BoxDat.SetArtworkForSerial(o.Serial, o.BoxPvr);
                    if (o.IconPvr != null)
                        manager.IconDat?.SetIconForSerial(o.Serial, o.IconPvr);
                }
                if (o.BackPvr != null)
                    manager.BackDat.SetArtworkForSerial(o.Serial, o.BackPvr);
                if (o.DiscPvr != null)
                    manager.DiscArtDat.SetIconForSerial(o.Serial, o.DiscPvr);
                if (o.MetaRaw != null)
                    manager.MetaDat?.SetEntryForSerial(o.Serial, o.MetaRaw);
                if (o.Info != null)
                    manager.InfoDat?.Set(o.Serial, o.Info);
                op.Add(before, ScrapeSnapshot.Take(manager, o.Serial));
                manager.RefreshArtworkStatusForSerial(o.Serial);
            }
            if (op.Count > 0)
                manager.UndoManager.RecordChange(op);
            return op.Count;
        }
    }

    /// <summary>Everything the scraper may change for one serial.</summary>
    internal sealed class ScrapeSnapshot
    {
        public string Serial;
        public byte[] Box, Icon, Back, Disc, Meta, Info;

        public static ScrapeSnapshot Take(Manager m, string serial) => new()
        {
            Serial = serial,
            Box = m.BoxDat?.GetPvrDataForSerial(serial),
            Icon = m.IconDat?.GetPvrDataForSerial(serial),
            Back = m.BackDat?.GetPvrDataForSerial(serial),
            Disc = m.DiscArtDat?.GetPvrDataForSerial(serial),
            Meta = m.MetaDat?.GetRawDataForSerial(serial)?.ToArray(),
            Info = m.InfoDat?.GetRaw(serial),
        };

        public void Restore(Manager m)
        {
            SetBox(m.BoxDat, Box);
            if (m.IconDat != null)
            {
                if (Icon != null) m.IconDat.SetIconForSerial(Serial, Icon);
                else m.IconDat.DeleteEntryForSerial(Serial);
            }
            SetBox(m.BackDat, Back);
            if (m.DiscArtDat != null)
            {
                if (Disc != null) m.DiscArtDat.SetIconForSerial(Serial, Disc);
                else m.DiscArtDat.DeleteEntryForSerial(Serial);
            }
            if (m.MetaDat != null)
            {
                if (Meta != null) m.MetaDat.SetEntryForSerial(Serial, Meta);
                else m.MetaDat.DeleteEntryForSerial(Serial);
            }
            m.InfoDat?.SetRaw(Serial, Info);
            m.RefreshArtworkStatusForSerial(Serial);
        }

        private void SetBox(BoxDatManager dat, byte[] data)
        {
            if (dat == null)
                return;
            if (data != null) dat.SetArtworkForSerial(Serial, data);
            else dat.DeleteEntryForSerial(Serial);
        }
    }

    /// <summary>One scrape run as a single undo step.</summary>
    internal sealed class ScrapeBatchOperation : UndoOperation
    {
        private readonly Manager _manager;
        private readonly List<(ScrapeSnapshot before, ScrapeSnapshot after)> _games = new();

        public ScrapeBatchOperation(Manager manager)
        {
            _manager = manager;
        }

        public int Count => _games.Count;

        public void Add(ScrapeSnapshot before, ScrapeSnapshot after) => _games.Add((before, after));

        public override string Description => $"Scrape Artwork ({_games.Count} games)";

        public override void Undo()
        {
            foreach (var (before, _) in _games)
                before.Restore(_manager);
        }

        public override void Redo()
        {
            foreach (var (_, after) in _games)
                after.Restore(_manager);
        }
    }
}
