using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Threading;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using GDMENUCardManager.Core;
using GDMENUCardManager.Core.Scraper;

namespace GDMENUCardManager
{
    /// <summary>Scrape Artwork and Info: settings, a run with progress and log, the results put into the DATs.</summary>
    public partial class ScrapeWindow : Window
    {
        private readonly Core.Manager _manager;
        private readonly IList<GdItem> _selected;
        private CancellationTokenSource _cts;

        private CheckBox CheckScreenScraper, CheckLibretro, CheckFront, CheckBack, CheckDisc, CheckInfo, CheckOverwrite;
        private TextBox TextSsUser, TextSsPassword, TextDevId, TextDevPassword, TextLog;
        private RadioButton RadioMissing, RadioSelected, RadioAll;
        private ComboBox ComboRegion, ComboLanguage;
        private ProgressBar Progress;
        private Button ButtonStart, ButtonClose;

        public ScrapeWindow()
        {
            InitializeComponent();
        }

        public ScrapeWindow(Core.Manager manager, IList<GdItem> selected)
        {
            InitializeComponent();
            _manager = manager;
            _selected = selected ?? new List<GdItem>();

            CheckScreenScraper = this.FindControl<CheckBox>("CheckScreenScraper");
            CheckLibretro = this.FindControl<CheckBox>("CheckLibretro");
            CheckFront = this.FindControl<CheckBox>("CheckFront");
            CheckBack = this.FindControl<CheckBox>("CheckBack");
            CheckDisc = this.FindControl<CheckBox>("CheckDisc");
            CheckInfo = this.FindControl<CheckBox>("CheckInfo");
            CheckOverwrite = this.FindControl<CheckBox>("CheckOverwrite");
            TextSsUser = this.FindControl<TextBox>("TextSsUser");
            TextSsPassword = this.FindControl<TextBox>("TextSsPassword");
            TextDevId = this.FindControl<TextBox>("TextDevId");
            TextDevPassword = this.FindControl<TextBox>("TextDevPassword");
            TextLog = this.FindControl<TextBox>("TextLog");
            RadioMissing = this.FindControl<RadioButton>("RadioMissing");
            RadioSelected = this.FindControl<RadioButton>("RadioSelected");
            RadioAll = this.FindControl<RadioButton>("RadioAll");
            ComboRegion = this.FindControl<ComboBox>("ComboRegion");
            ComboLanguage = this.FindControl<ComboBox>("ComboLanguage");
            Progress = this.FindControl<ProgressBar>("Progress");
            ButtonStart = this.FindControl<Button>("ButtonStart");
            ButtonClose = this.FindControl<Button>("ButtonClose");

            LoadSettings(ScraperSettings.Load());
            RadioSelected.IsEnabled = _selected.Count > 0;
            RadioSelected.Content = $"Selected games ({_selected.Count})";

            if (_manager.ArtworkDisabled || _manager.BoxDat == null)
            {
                ButtonStart.IsEnabled = false;
                Log("Artwork is turned off or the DAT files could not be loaded: nothing can be scraped.");
            }

            Closing += ScrapeWindow_Closing;
        }

        private void InitializeComponent()
        {
            AvaloniaXamlLoader.Load(this);
        }

        private void LoadSettings(ScraperSettings s)
        {
            CheckScreenScraper.IsChecked = s.UseScreenScraper;
            CheckLibretro.IsChecked = s.UseLibretro;
            TextSsUser.Text = s.ScreenScraperUser;
            TextSsPassword.Text = s.ScreenScraperPassword;
            TextDevId.Text = s.DeveloperId;
            TextDevPassword.Text = s.DeveloperPassword;
            CheckFront.IsChecked = s.FrontCover;
            CheckBack.IsChecked = s.BackCover;
            CheckDisc.IsChecked = s.DiscLabel;
            CheckInfo.IsChecked = s.Info;
            CheckOverwrite.IsChecked = s.Overwrite;
            ComboRegion.SelectedIndex = (int)s.Region;
            var languages = new[] { "en", "fr", "de", "es", "it", "pt" };
            ComboLanguage.SelectedIndex = Math.Max(0, Array.IndexOf(languages, s.Language));
            var scope = s.Scope == ScrapeScope.Selected && _selected.Count == 0 ? ScrapeScope.MissingArtwork : s.Scope;
            if (_selected.Count > 1 && s.Scope != ScrapeScope.All)
                scope = ScrapeScope.Selected; // several games selected: that is what is meant
            RadioMissing.IsChecked = scope == ScrapeScope.MissingArtwork;
            RadioSelected.IsChecked = scope == ScrapeScope.Selected;
            RadioAll.IsChecked = scope == ScrapeScope.All;
        }

        private ScraperSettings ReadSettings()
        {
            var languages = new[] { "en", "fr", "de", "es", "it", "pt" };
            return new ScraperSettings
            {
                UseScreenScraper = CheckScreenScraper.IsChecked == true,
                UseLibretro = CheckLibretro.IsChecked == true,
                ScreenScraperUser = TextSsUser.Text ?? string.Empty,
                ScreenScraperPassword = TextSsPassword.Text ?? string.Empty,
                DeveloperId = TextDevId.Text ?? string.Empty,
                DeveloperPassword = TextDevPassword.Text ?? string.Empty,
                FrontCover = CheckFront.IsChecked == true,
                BackCover = CheckBack.IsChecked == true,
                DiscLabel = CheckDisc.IsChecked == true,
                Info = CheckInfo.IsChecked == true,
                Overwrite = CheckOverwrite.IsChecked == true,
                Region = (ScrapeRegion)Math.Max(0, ComboRegion.SelectedIndex),
                Language = languages[Math.Clamp(ComboLanguage.SelectedIndex, 0, languages.Length - 1)],
                Scope = RadioAll.IsChecked == true ? ScrapeScope.All
                    : RadioSelected.IsChecked == true ? ScrapeScope.Selected
                    : ScrapeScope.MissingArtwork,
            };
        }

        private void Log(string line)
        {
            TextLog.Text = string.IsNullOrEmpty(TextLog.Text) ? line : TextLog.Text + Environment.NewLine + line;
            TextLog.CaretIndex = TextLog.Text.Length; // keep the newest line in view
        }

        private void SetRunning(bool running)
        {
            ButtonStart.IsEnabled = !running;
            ButtonClose.Content = running ? "Stop" : "Close";
            foreach (var c in new Control[] { CheckScreenScraper, CheckLibretro, CheckFront, CheckBack, CheckDisc, CheckInfo,
                         CheckOverwrite, TextSsUser, TextSsPassword, TextDevId, TextDevPassword, RadioMissing, RadioAll,
                         ComboRegion, ComboLanguage })
                c.IsEnabled = !running;
            RadioSelected.IsEnabled = !running && _selected.Count > 0;
        }

        private async void ButtonStart_Click(object sender, RoutedEventArgs e)
        {
            var settings = ReadSettings();
            try
            {
                settings.Save();
            }
            catch (Exception ex)
            {
                Log($"The settings could not be saved ({ex.Message}).");
            }
            if (!settings.FrontCover && !settings.BackCover && !settings.DiscLabel && !settings.Info)
            {
                Log("Tick at least one thing to get.");
                return;
            }

            var items = ArtScraper.SelectItems(_manager, settings.Scope, _selected);
            if (items.Count == 0)
            {
                Log("No games to scrape.");
                return;
            }

            TextLog.Text = string.Empty;
            Log($"Scraping {items.Count} game{(items.Count == 1 ? "" : "s")}...");
            Progress.Maximum = items.Count;
            Progress.Value = 0;
            SetRunning(true);
            _cts = new CancellationTokenSource();
            List<ScrapeOutcome> outcomes = null;
            try
            {
                var scraper = new ArtScraper(_manager, settings);
                var progress = new Progress<ScrapeProgress>(p =>
                {
                    Progress.Value = p.Done;
                    if (!string.IsNullOrEmpty(p.Line))
                        Log(p.Line);
                });
                outcomes = await scraper.RunAsync(items, progress, _cts.Token);
            }
            catch (Exception ex)
            {
                Log($"Stopped: {ex.Message}");
            }
            finally
            {
                bool stopped = _cts.IsCancellationRequested;
                _cts.Dispose();
                _cts = null;
                SetRunning(false);
                if (stopped)
                    Log("Stopped.");
            }

            if (outcomes != null && outcomes.Count > 0)
            {
                // The Progress<T> callbacks may still be queued: give them a moment before the summary
                await System.Threading.Tasks.Task.Delay(100);
                int applied = ArtScraper.Apply(_manager, outcomes);
                Log($"Added to {applied} game{(applied == 1 ? "" : "s")}. Press Save Changes to write them to the SD card (Undo takes the whole run back).");
            }
            else if (outcomes != null)
            {
                Log("Nothing new was found.");
            }
        }

        private void ButtonClose_Click(object sender, RoutedEventArgs e)
        {
            if (_cts != null)
            {
                _cts.Cancel();
                Log("Stopping after the current game...");
                return;
            }
            Close();
        }

        private void ScrapeWindow_Closing(object sender, CancelEventArgs e)
        {
            if (_cts != null)
            {
                _cts.Cancel(); // the run ends after its current game; the window stays until then
                e.Cancel = true;
            }
        }
    }
}
