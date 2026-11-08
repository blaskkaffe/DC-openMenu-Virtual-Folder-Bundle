namespace GDMENUCardManager.Core.Scraper
{
    /// <summary>
    /// ScreenScraper developer credentials built into the program. Empty in the repository: they must not be published.
    /// A release build fills them from the repository secrets SS_DEVID and SS_DEVPASSWORD (see the Card Manager
    /// workflow). Without them, users enter their own pair under Advanced in the scrape window, or only the libretro
    /// thumbnails are used.
    /// </summary>
    internal static class ScreenScraperDevCredentials
    {
        public const string DevId = "";
        public const string DevPassword = "";
        public const string SoftwareName = "GDMenuCardManager";
    }
}
