using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Platform;
using SukiUI;
using System;
using System.Linq;

namespace Evidence;

public partial class App : Application
{
    public SukiTheme Theme { get; private set; } = null!;

    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
        Theme = Styles.OfType<SukiTheme>().Single();
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var mainWindow = new MainWindow();
            using var iconStream = AssetLoader.Open(new Uri("avares://evidence/Assets/evidence.ico"));
            mainWindow.Icon = new WindowIcon(iconStream);
            desktop.MainWindow = mainWindow;
            mainWindow.Closed += (_, _) => desktop.Shutdown();
        }

        base.OnFrameworkInitializationCompleted();
    }
}