using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using GDMENUCardManager.Core.Scraper;

namespace GDMENUCardManager
{
    /// <summary>
    /// ScreenScraper account settings (menu: Scrape > ScreenScraper Account), as EmulationStation's and Recalbox's scraper
    /// menus have them: user name, password, a login test that shows the daily quota. Saved in scraper.json; the other
    /// scraper settings in that file are kept.
    /// </summary>
    public partial class ScraperAccountWindow : Window
    {
        private TextBox TextUser, TextPassword, TextDevId, TextDevPassword;
        private CheckBox CheckShowPassword;
        private TextBlock TextStatus;
        private Button ButtonTest, ButtonSave;

        /// <summary>True when Save was pressed.</summary>
        public bool Saved { get; private set; }

        public ScraperAccountWindow()
        {
            InitializeComponent();
            TextUser = this.FindControl<TextBox>("TextUser");
            TextPassword = this.FindControl<TextBox>("TextPassword");
            TextDevId = this.FindControl<TextBox>("TextDevId");
            TextDevPassword = this.FindControl<TextBox>("TextDevPassword");
            CheckShowPassword = this.FindControl<CheckBox>("CheckShowPassword");
            TextStatus = this.FindControl<TextBlock>("TextStatus");
            ButtonTest = this.FindControl<Button>("ButtonTest");
            ButtonSave = this.FindControl<Button>("ButtonSave");

            var s = ScraperSettings.Load();
            TextUser.Text = s.ScreenScraperUser;
            TextPassword.Text = s.ScreenScraperPassword;
            TextDevId.Text = s.DeveloperId;
            TextDevPassword.Text = s.DeveloperPassword;
            TextStatus.Text = string.IsNullOrEmpty(s.ScreenScraperUser)
                ? "No account set: Scrape works without one, with ScreenScraper's lower limits."
                : "Not checked yet. Press Test login.";
        }

        private void InitializeComponent()
        {
            AvaloniaXamlLoader.Load(this);
        }

        private ScraperSettings Current()
        {
            var s = ScraperSettings.Load();
            s.ScreenScraperUser = (TextUser.Text ?? string.Empty).Trim();
            s.ScreenScraperPassword = TextPassword.Text ?? string.Empty;
            s.DeveloperId = (TextDevId.Text ?? string.Empty).Trim();
            s.DeveloperPassword = TextDevPassword.Text ?? string.Empty;
            return s;
        }

        private void CheckShowPassword_Click(object sender, RoutedEventArgs e)
        {
            TextPassword.PasswordChar = CheckShowPassword.IsChecked == true ? '\0' : '*';
        }

        private async void ButtonTest_Click(object sender, RoutedEventArgs e)
        {
            ButtonTest.IsEnabled = ButtonSave.IsEnabled = false;
            TextStatus.Text = "Logging in...";
            try
            {
                using var http = ArtScraper.CreateHttpClient();
                using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(30));
                var account = await new ScreenScraperClient(http, Current()).CheckAccountAsync(cts.Token);
                TextStatus.Text = account.Summary;
            }
            catch (ScraperException ex)
            {
                TextStatus.Text = ex.Message;
            }
            catch (Exception ex)
            {
                TextStatus.Text = $"The login could not be checked ({ex.Message}).";
            }
            finally
            {
                ButtonTest.IsEnabled = ButtonSave.IsEnabled = true;
            }
        }

        private void ButtonCreateAccount_Click(object sender, RoutedEventArgs e)
        {
            const string url = "https://www.screenscraper.fr/membreinscription.php";
            try
            {
                if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                    Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
                else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
                    Process.Start("xdg-open", url);
                else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
                    Process.Start("open", url);
            }
            catch
            {
                TextStatus.Text = "Open " + url + " in your browser.";
            }
        }

        private void ButtonSave_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                Current().Save();
                Saved = true;
                Close();
            }
            catch (Exception ex)
            {
                TextStatus.Text = $"Could not save: {ex.Message}";
            }
        }

        private void ButtonCancel_Click(object sender, RoutedEventArgs e) => Close();
    }
}
