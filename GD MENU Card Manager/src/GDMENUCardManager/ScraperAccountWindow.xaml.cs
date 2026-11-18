using System;
using System.Diagnostics;
using System.Threading;
using System.Windows;
using System.Windows.Navigation;
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
        public ScraperAccountWindow()
        {
            InitializeComponent();
            var s = ScraperSettings.Load();
            TextUser.Text = s.ScreenScraperUser;
            TextPassword.Password = s.ScreenScraperPassword;
            TextDevId.Text = s.DeveloperId;
            TextDevPassword.Password = s.DeveloperPassword;
            TextStatus.Text = string.IsNullOrEmpty(s.ScreenScraperUser)
                ? "No account set: Scrape works without one, with ScreenScraper's lower limits."
                : "Not checked yet. Press Test login.";
        }

        private string Password => CheckShowPassword.IsChecked == true ? TextPasswordShown.Text : TextPassword.Password;

        private ScraperSettings Current()
        {
            var s = ScraperSettings.Load();
            s.ScreenScraperUser = (TextUser.Text ?? string.Empty).Trim();
            s.ScreenScraperPassword = Password ?? string.Empty;
            s.DeveloperId = (TextDevId.Text ?? string.Empty).Trim();
            s.DeveloperPassword = TextDevPassword.Password ?? string.Empty;
            return s;
        }

        private void CheckShowPassword_Click(object sender, RoutedEventArgs e)
        {
            bool show = CheckShowPassword.IsChecked == true;
            if (show)
                TextPasswordShown.Text = TextPassword.Password;
            else
                TextPassword.Password = TextPasswordShown.Text;
            TextPasswordShown.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
            TextPassword.Visibility = show ? Visibility.Collapsed : Visibility.Visible;
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

        private void Hyperlink_RequestNavigate(object sender, RequestNavigateEventArgs e)
        {
            try
            {
                Process.Start(new ProcessStartInfo(e.Uri.AbsoluteUri) { UseShellExecute = true });
            }
            catch
            {
                TextStatus.Text = "Open " + e.Uri.AbsoluteUri + " in your browser.";
            }
            e.Handled = true;
        }

        private void ButtonSave_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                Current().Save();
                DialogResult = true;
            }
            catch (Exception ex)
            {
                TextStatus.Text = $"Could not save: {ex.Message}";
            }
        }
    }
}
