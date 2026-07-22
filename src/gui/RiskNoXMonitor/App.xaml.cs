using System;
using System.IO;
using System.Windows;

namespace RisknoxMonitor;

/// <summary>
/// Interaction logic for App.xaml
/// </summary>
public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        this.DispatcherUnhandledException += (s, args) =>
        {
            string logPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "monitor_crash.log");
            File.WriteAllText(logPath, $"{DateTime.Now}\n{args.Exception}\n{args.Exception.InnerException}");
            MessageBox.Show(args.Exception.ToString(), "Risknox Monitor Error", MessageBoxButton.OK, MessageBoxImage.Error);
            args.Handled = true;
        };

        base.OnStartup(e);
    }
}
