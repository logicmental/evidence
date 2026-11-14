using Avalonia.Controls;
using Avalonia.Platform.Storage;
using Avalonia.Media;
using Avalonia;
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace Evidence.Setup;

public partial class MainWindow : Window
{
    private const string CoreInstallerResource = "Evidence.Setup.CoreInstaller.exe";
    private bool _installing;
    private bool _installed;
    private bool _applicationLaunched;
    private string _installDirectory = string.Empty;
    private string _language = "en";

    public MainWindow()
    {
        InitializeComponent();

        _language = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "ru" ? "ru" : "en";
        LanguageSelector.SelectedIndex = _language == "ru" ? 1 : 0;
        InstallDirectoryBox.Text = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Programs", "evidence");
        ApplyLanguage();

        LanguageSelector.SelectionChanged += (_, _) =>
        {
            if (LanguageSelector.SelectedItem is ComboBoxItem item && item.Tag is string language)
            {
                _language = language;
                ApplyLanguage();
            }
        };
        BrowseButton.Click += async (_, _) => await BrowseForInstallDirectoryAsync();
        InstallButton.Click += async (_, _) =>
        {
            if (_installed)
            {
                if (_applicationLaunched)
                    Close();
                else
                    LaunchApplication();
            }
            else
                await InstallAsync();
        };
        CancelButton.Click += (_, _) => Close();
        Closing += (_, args) =>
        {
            if (_installing)
                args.Cancel = true;
        };
    }

    private void ApplyLanguage()
    {
        var russian = _language == "ru";
        Title = russian ? "Установка evidence" : "Install evidence";
        HeaderSubtitle.Text = russian ? "УСТАНОВЩИК" : "INSTALLER";
        Heading.Text = russian ? "Установить evidence" : "Install evidence";
        Description.Text = russian
            ? "Спокойное и надежное пространство для ваших заметок."
            : "A calm, private home for your notes.";
        LocationLabel.Text = russian ? "Папка установки" : "Install location";
        BrowseButton.Content = russian ? "Обзор..." : "Browse...";
        LocationHint.Text = russian
            ? "Здесь будут установлены приложение и дополнительные инструменты."
            : "The app and its bundled tools will be installed here.";
        LanguageLabel.Text = russian ? "Язык интерфейса" : "Interface language";
        DesktopShortcutCheckBox.Content = russian ? "Создать ярлык на рабочем столе" : "Create a desktop shortcut";
        LaunchWhenFinishedCheckBox.Content = russian ? "Запустить приложение после установки" : "Launch when finished";
        FooterHint.Text = russian
            ? "Учетная запись и подключение к интернету не требуются."
            : "No account or internet connection required.";
        CancelButton.Content = russian ? (_installed ? "Закрыть" : "Отмена") : (_installed ? "Close" : "Cancel");
        InstallButton.Content = russian
            ? (_installed ? (_applicationLaunched ? "Готово" : "Запустить evidence") : "Установить")
            : (_installed ? (_applicationLaunched ? "Done" : "Launch evidence") : "Install");

        if (!_installing && !_installed)
            StatusText.Text = string.Empty;
    }

    private async Task BrowseForInstallDirectoryAsync()
    {
        var selection = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = _language == "ru" ? "Выберите папку установки" : "Choose install location",
            AllowMultiple = false
        });
        if (selection.Count > 0)
            InstallDirectoryBox.Text = selection[0].Path.LocalPath;
    }

    private async Task InstallAsync()
    {
        if (_installing)
            return;

        try
        {
            var requestedPath = InstallDirectoryBox.Text?.Trim();
            if (string.IsNullOrWhiteSpace(requestedPath) || !Path.IsPathFullyQualified(requestedPath))
            {
                SetStatus(_language == "ru"
                    ? "Укажите полный путь к папке установки."
                    : "Enter a full path for the install location.", true);
                return;
            }
            _installDirectory = Path.GetFullPath(requestedPath);
        }
        catch (Exception exception) when (exception is ArgumentException or NotSupportedException or PathTooLongException)
        {
            SetStatus(exception.Message, true);
            return;
        }

        SetBusy(true);
        SetStatus(_language == "ru" ? "Подготовка установки..." : "Preparing installation...", false);
        string? temporaryDirectory = null;
        try
        {
            await using var coreInstaller = Assembly.GetExecutingAssembly()
                .GetManifestResourceStream(CoreInstallerResource);
            if (coreInstaller is null)
                throw new InvalidOperationException(
                    "The embedded Inno Setup package is missing. Build the installer with installer/build.ps1.");

            temporaryDirectory = Path.Combine(Path.GetTempPath(), "evidence-setup-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(temporaryDirectory);
            var coreInstallerPath = Path.Combine(temporaryDirectory, "evidence-install-core.exe");
            await using (var output = new FileStream(coreInstallerPath, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                await coreInstaller.CopyToAsync(output);

            SetStatus(_language == "ru" ? "Устанавливаем evidence..." : "Installing evidence...", false);
            var logPath = Path.Combine(temporaryDirectory, "install.log");
            var startInfo = new ProcessStartInfo
            {
                FileName = coreInstallerPath,
                WorkingDirectory = temporaryDirectory,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            startInfo.ArgumentList.Add("/VERYSILENT");
            startInfo.ArgumentList.Add("/SUPPRESSMSGBOXES");
            startInfo.ArgumentList.Add("/NORESTART");
            startInfo.ArgumentList.Add("/LOG=" + logPath);
            startInfo.ArgumentList.Add("/DIR=" + _installDirectory);
            startInfo.ArgumentList.Add("/LANG=" + (_language == "ru" ? "russian" : "english"));
            if (DesktopShortcutCheckBox.IsChecked == true)
                startInfo.ArgumentList.Add("/TASKS=desktopicon");

            using var process = Process.Start(startInfo)
                ?? throw new InvalidOperationException("The installation process could not be started.");
            await process.WaitForExitAsync();
            if (process.ExitCode != 0)
            {
                var failure = _language == "ru"
                    ? $"Установка завершилась с ошибкой (код {process.ExitCode})."
                    : $"Installation failed with exit code {process.ExitCode}.";
                if (File.Exists(logPath))
                    failure += Environment.NewLine + string.Join(Environment.NewLine, File.ReadLines(logPath).TakeLast(8));
                SetStatus(failure, true);
                return;
            }

            var vaultMetadataError = WriteVaultPathMetadata();
            _installed = true;
            ApplyLanguage();
            if (vaultMetadataError is null)
                SetStatus(_language == "ru" ? "Установка завершена." : "Installation is complete.", false, true);
            else
                SetStatus(_language == "ru"
                    ? $"Установка завершена, но данные хранилища не обновлены: {vaultMetadataError}"
                    : $"Installation completed, but vault metadata could not be updated: {vaultMetadataError}",
                    true);
            if (LaunchWhenFinishedCheckBox.IsChecked == true)
                LaunchApplication();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                           InvalidOperationException or Win32Exception or ArgumentException)
        {
            SetStatus(exception.Message, true);
        }
        finally
        {
            if (temporaryDirectory is not null && Directory.Exists(temporaryDirectory))
            {
                try
                {
                    Directory.Delete(temporaryDirectory, true);
                }
                catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
                {
                    SetStatus(_language == "ru"
                        ? "Установка завершена, но временные файлы не удалось удалить: " + exception.Message
                        : "Installation finished, but temporary files could not be removed: " + exception.Message,
                        true);
                }
            }
            SetBusy(false);
        }
    }

    private void LaunchApplication()
    {
        var executable = Path.Combine(_installDirectory, "evidence.exe");
        try
        {
            using var process = Process.Start(new ProcessStartInfo
            {
                FileName = executable,
                WorkingDirectory = _installDirectory,
                UseShellExecute = true
            }) ?? throw new InvalidOperationException("The application process could not be started.");
            _applicationLaunched = true;
            ApplyLanguage();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                           Win32Exception or InvalidOperationException)
        {
            SetStatus(exception.Message, true);
        }
    }

    private static string? WriteVaultPathMetadata()
    {
        var appDataPath = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        var settingsPath = Path.Combine(appDataPath, "evidence", "settings.json");
        if (!File.Exists(settingsPath))
            return null;

        try
        {
            using var settings = JsonDocument.Parse(File.ReadAllText(settingsPath));
            if (!settings.RootElement.TryGetProperty("VaultPath", out var vaultPath) ||
                vaultPath.ValueKind != JsonValueKind.String ||
                string.IsNullOrWhiteSpace(vaultPath.GetString()))
                return null;

            var metadataPath = Path.Combine(appDataPath, "evidence", "vault-path.txt");
            File.WriteAllText(metadataPath, vaultPath.GetString()!, new UTF8Encoding(false));
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or
                                           JsonException or InvalidOperationException)
        {
            return exception.Message;
        }
    }

    private void SetBusy(bool busy)
    {
        _installing = busy;
        var canConfigure = !busy && !_installed;
        InstallDirectoryBox.IsEnabled = canConfigure;
        BrowseButton.IsEnabled = canConfigure;
        LanguageSelector.IsEnabled = canConfigure;
        DesktopShortcutCheckBox.IsEnabled = canConfigure;
        LaunchWhenFinishedCheckBox.IsEnabled = canConfigure;
        CancelButton.IsEnabled = !busy;
        InstallButton.IsEnabled = !busy;
        ProgressPanel.IsVisible = busy;
    }

    private void SetStatus(string message, bool isError, bool isSuccess = false)
    {
        StatusText.Text = message;
        StatusText.Foreground = new SolidColorBrush(Color.Parse(isError
            ? "#B42318"
            : isSuccess ? "#18794E" : "#6E6E73"));
    }
}
