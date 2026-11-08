using System.Collections.Generic;

namespace GDMENUCardManager.Core.Scraper
{
    /// <summary>What a source knows about one game. Picture fields are URLs until downloaded, then local files.</summary>
    public sealed class ScrapedGame
    {
        public string Source { get; set; } = string.Empty;

        /// <summary>The name the source matched (for the log, and to look the game up in the next source).</summary>
        public string MatchedName { get; set; } = string.Empty;

        /// <summary>The game's names by region code (us, eu, jp, wor, ss ...), from ScreenScraper.</summary>
        public Dictionary<string, string> Names { get; } = new();

        public string FrontUrl { get; set; }
        public string BackUrl { get; set; }
        public string DiscUrl { get; set; }

        public string FrontFile { get; set; }
        public string BackFile { get; set; }
        public string DiscFile { get; set; }

        public string Synopsis { get; set; }
        public string Developer { get; set; }
        public string Publisher { get; set; }

        /// <summary>yyyy, yyyy-mm or yyyy-mm-dd.</summary>
        public string ReleaseDate { get; set; }

        public List<string> Genres { get; } = new();

        /// <summary>"1", "1-4" ...</summary>
        public string Players { get; set; }

        /// <summary>0..20, ScreenScraper's note; 0 when unknown.</summary>
        public int Rating { get; set; }

        public bool HasAnyPicture => FrontUrl != null || BackUrl != null || DiscUrl != null;

        public bool HasAnyText =>
            !string.IsNullOrWhiteSpace(Synopsis) || !string.IsNullOrWhiteSpace(Developer) ||
            !string.IsNullOrWhiteSpace(Publisher) || !string.IsNullOrWhiteSpace(ReleaseDate) || Genres.Count > 0;
    }

    /// <summary>A lookup that could not be done at all (as opposed to a game that was not found).</summary>
    public sealed class ScraperException : System.Exception
    {
        /// <summary>The source will not answer again in this run (bad login, daily quota, API closed): stop asking it.</summary>
        public bool StopSource { get; }

        public ScraperException(string message, bool stopSource) : base(message)
        {
            StopSource = stopSource;
        }
    }
}
