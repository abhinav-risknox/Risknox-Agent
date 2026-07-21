using System.IO;
using System.Windows;
using System.Windows.Threading;

namespace ThreatNotification;

public partial class App : Application
{
    public App()
    {
        DispatcherUnhandledException += (_, e) =>
        {
            try
            {
                string logDir = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                    "Risknox Pulse", "logs");
                Directory.CreateDirectory(logDir);
                File.AppendAllText(
                    Path.Combine(logDir, "notification_crash.log"),
                    $"[{DateTime.Now:o}] UNHANDLED: {e.Exception}\n\n");
            }
            catch { }
            e.Handled = true; // prevent OS crash dialog; app will still exit
            Shutdown(1);
        };
    }
}
