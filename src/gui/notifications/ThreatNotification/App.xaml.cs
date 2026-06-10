using System.Windows;

namespace ThreatNotification;

public partial class App : Application
{
    // CLI args are parsed in NotificationWindow constructor
    // Usage: ThreatNotification.exe --file "x" --threat "y" --path "z" --source "u" --slot 0 --timeout 10
}
