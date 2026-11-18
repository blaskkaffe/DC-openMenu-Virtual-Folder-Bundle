using System;
using System.IO;
using System.Text.Json;

namespace GDMENUCardManager.Core.Scraper
{
    /// <summary>Which region's art and dates to prefer.</summary>
    public enum ScrapeRegion
    {
        /// <summary>From the disc's own IP.BIN region (PAL discs get European art, and so on).</summary>
        Auto,
        Usa,
        Europe,
        Japan,
    }

    /// <summary>Which games a run looks at.</summary>
    public enum ScrapeScope
    {
        All,
        Selected,
        MissingArtwork,
    }

    /// <summary>
    /// The scraper's settings, kept in the user's application data folder (scraper.json), never next to the program:
    /// the ScreenScraper password is in it. Unknown or broken files fall back to the defaults.
    /// </summary>
    public sealed class ScraperSettings
    {
        public string ScreenScraperUser { get; set; } = string.Empty;
        public string ScreenScraperPassword { get; set; } = string.Empty;

        /// <summary>
        /// ScreenScraper's developer credentials (every program that uses its API needs a pair). Empty: the pair built
        /// into this program is used, if the build has one (ScreenScraperDevCredentials).
        /// </summary>
        public string DeveloperId { get; set; } = string.Empty;
        public string DeveloperPassword { get; set; } = string.Empty;

        public bool UseScreenScraper { get; set; } = true;
        public bool UseLibretro { get; set; } = true;

        public ScrapeRegion Region { get; set; } = ScrapeRegion.Auto;

        /// <summary>Synopsis and genre language, as ScreenScraper names them (en, fr, de, es, it, pt, ja).</summary>
        public string Language { get; set; } = "en";

        public bool FrontCover { get; set; } = true;
        public bool BackCover { get; set; } = true;
        public bool DiscLabel { get; set; } = true;
        public bool Info { get; set; } = true;

        /// <summary>False: art and text a game already has are kept; only what is missing is filled in.</summary>
        public bool Overwrite { get; set; }

        public ScrapeScope Scope { get; set; } = ScrapeScope.MissingArtwork;

        public static string SettingsFolder =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "GDMENUCardManager");

        public static string SettingsPath => Path.Combine(SettingsFolder, "scraper.json");

        /// <summary>Downloaded pictures and answers, so a second run costs no ScreenScraper requests.</summary>
        public static string CacheFolder => Path.Combine(SettingsFolder, "scrape_cache");

        public static ScraperSettings Load()
        {
            try
            {
                if (File.Exists(SettingsPath))
                    return JsonSerializer.Deserialize<ScraperSettings>(File.ReadAllText(SettingsPath)) ?? new ScraperSettings();
            }
            catch
            {
                // A broken file: start from the defaults.
            }
            return new ScraperSettings();
        }

        public void Save()
        {
            Directory.CreateDirectory(SettingsFolder);
            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
        }
    }
}
