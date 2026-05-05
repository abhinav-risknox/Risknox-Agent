using System;
using System.IO;
using System.Security.Cryptography.X509Certificates;
using System.ServiceProcess;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;
using System.Windows.Threading;

namespace RisknoxMonitor
{
    public partial class MainWindow : Window
    {
        private const string ServiceName = "ResolutePulse";
        private DispatcherTimer _timer;
        private int _activeTab = 0; // 0=Overview 1=Components 2=Logs

        private readonly Brush _colorLimeCream = (Brush)new BrushConverter().ConvertFromString("#FFFD98")!;
        private readonly Brush _colorPlatinum = (Brush)new BrushConverter().ConvertFromString("#EAEAEB")!;
        private readonly Brush _colorWhite = (Brush)new BrushConverter().ConvertFromString("#FFFFFF")!;
        private readonly Brush _colorOrange = (Brush)new BrushConverter().ConvertFromString("#FF5B00")!;
        private readonly Brush _colorRed = (Brush)new BrushConverter().ConvertFromString("#FF3333")!;

        public MainWindow()
        {
            InitializeComponent();
            
            _timer = new DispatcherTimer();
            _timer.Interval = TimeSpan.FromSeconds(2);
            _timer.Tick += Timer_Tick;
            _timer.Start();
            
            UpdateStatus();
            UpdateLicenseStatus();
        }

        private void Timer_Tick(object? sender, EventArgs e)
        {
            UpdateStatus();
            UpdateLicenseStatus();
            UpdateComponentsStatus();
            if (_activeTab == 2) RefreshLogs();
        }

        private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (e.LeftButton == MouseButtonState.Pressed)
            {
                this.DragMove();
            }
        }

        private void BtnClose_Click(object sender, RoutedEventArgs e)
        {
            this.Close();
        }

        private void UpdateLicenseStatus()
        {
            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                string[] possiblePaths = new string[]
                {
                    Path.Combine(baseDir, "status.json"),
                    Path.Combine(baseDir, "..", "..", "..", "..", "..", "build", "status.json"),
                    Path.Combine(baseDir, "..", "..", "..", "..", "build", "status.json"),
                    @"C:\Users\User\Desktop\Agent\build\status.json"
                };

                string statusPath = "";
                foreach (var p in possiblePaths)
                {
                    if (File.Exists(p)) { statusPath = p; break; }
                }

                if (!string.IsNullOrEmpty(statusPath))
                {
                    string json;
                    using (var fs = new FileStream(statusPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                    using (var sr = new StreamReader(fs))
                    {
                        json = sr.ReadToEnd();
                    }

                    using var doc = System.Text.Json.JsonDocument.Parse(json);
                    var root = doc.RootElement;

                    // License status
                    bool suspended = root.TryGetProperty("licenseSuspended", out var suspProp) && suspProp.GetBoolean();
                    string message = root.TryGetProperty("licenseMessage", out var msgProp) ? msgProp.GetString() ?? "" : "";
                    string lastError = root.TryGetProperty("lastError", out var errProp) ? errProp.GetString() ?? "" : "";
                    string licType = root.TryGetProperty("licenseType", out var typeProp) ? typeProp.GetString() ?? "" : "";
                    string licExpiry = root.TryGetProperty("licenseExpiry", out var expProp) ? expProp.GetString() ?? "" : "";

                    // Format license type for display
                    string typeDisplay = licType switch
                    {
                        "ENTERPRISE" => "Enterprise License",
                        "STANDARD" => "Standard License",
                        "TRIAL" => "Trial License",
                        "NONE" => "No License",
                        _ => "Agent Subscription"
                    };

                    // Calculate license days remaining
                    string expiryDetail = "";
                    if (!string.IsNullOrEmpty(licExpiry) && DateTime.TryParse(licExpiry, out var expDate))
                    {
                        int daysLeft = (expDate - DateTime.UtcNow).Days;
                        expiryDetail = daysLeft > 0 ? $"Expires in {daysLeft}d" : "Expired";
                    }

                    if (suspended)
                    {
                        TxtLicenseType.Text = typeDisplay;
                        TxtExpiry.Text = "⚠ Suspended";
                        TxtExpiry.Foreground = _colorOrange;
                        TxtLicenseDetail.Text = expiryDetail;
                    }
                    else if (message == "License active")
                    {
                        TxtLicenseType.Text = typeDisplay;
                        TxtExpiry.Text = "✓ Active";
                        TxtExpiry.Foreground = (Brush)new BrushConverter().ConvertFromString("#4CAF50")!;
                        TxtLicenseDetail.Text = expiryDetail;
                    }
                    else
                    {
                        TxtLicenseType.Text = typeDisplay;
                        TxtExpiry.Text = message;
                        TxtExpiry.Foreground = _colorWhite;
                        TxtLicenseDetail.Text = expiryDetail;
                    }

                    // Error banner
                    if (!string.IsNullOrEmpty(lastError))
                    {
                        TxtError.Text = lastError;
                        ErrorBanner.Visibility = Visibility.Visible;
                    }
                    else
                    {
                        ErrorBanner.Visibility = Visibility.Collapsed;
                    }

                    // Phase display in hero area
                    string phase = root.TryGetProperty("phase", out var phaseProp) ? phaseProp.GetString() ?? "" : "";
                    if (!string.IsNullOrEmpty(phase) && phase != "operational")
                    {
                        string phaseDisplay = phase switch
                        {
                            "initializing" => "⚙ Initializing...",
                            "registering" => "🔑 Registering...",
                            "connecting" => "🔌 Connecting...",
                            "starting" => "▶ Starting...",
                            "initialized" => "✅ Initialized",
                            "suspended" => "⚠ Suspended",
                            "degraded" => "⚠ Degraded",
                            "stopping" => "⏹ Stopping...",
                            _ => phase
                        };
                        TxtPhase.Text = phaseDisplay;
                    }
                    else
                    {
                        TxtPhase.Text = "";
                    }
                }
                else
                {
                    // Fallback: read cert expiry if no status.json
                    UpdateExpiryFromCert();
                }
            }
            catch
            {
                TxtLicenseType.Text = "Agent Subscription";
                TxtExpiry.Text = "Unknown";
                TxtExpiry.Foreground = _colorPlatinum;
            }
        }

        private void UpdateExpiryFromCert()
        {
            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                string[] possiblePaths = new string[]
                {
                    Path.Combine(baseDir, "certs", "agent.crt"),
                    Path.Combine(baseDir, "..", "..", "..", "..", "..", "certs", "agent.crt"),
                    Path.Combine(baseDir, "..", "..", "..", "..", "certs", "agent.crt"),
                    @"C:\Users\User\Desktop\Agent\certs\agent.crt"
                };

                string certPath = "";
                foreach (var p in possiblePaths)
                {
                    if (File.Exists(p)) { certPath = p; break; }
                }

                if (!string.IsNullOrEmpty(certPath))
                {
                    var cert = new X509Certificate2(certPath);
                    int daysLeft = (cert.NotAfter - DateTime.Now).Days;
                    TxtLicenseType.Text = "Agent Subscription";
                    TxtExpiry.Text = daysLeft > 0 ? $"{daysLeft} Days" : "Expired";
                    TxtExpiry.Foreground = daysLeft > 0 ? _colorWhite : _colorRed;
                    TxtLicenseDetail.Text = "Cert expiry (no status file)";
                }
                else
                {
                    TxtLicenseType.Text = "Not Registered";
                    TxtExpiry.Text = "No License";
                    TxtExpiry.Foreground = _colorPlatinum;
                    TxtLicenseDetail.Text = "";
                }
            }
            catch
            {
                TxtExpiry.Text = "Error";
                TxtExpiry.Foreground = _colorRed;
            }
        }

        private void UpdateComponentsStatus()
        {
            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                string[] possiblePaths = new string[]
                {
                    Path.Combine(baseDir, "status.json"),
                    Path.Combine(baseDir, "..", "..", "..", "..", "..", "build", "status.json"),
                    Path.Combine(baseDir, "..", "..", "..", "..", "build", "status.json"),
                    @"C:\Users\User\Desktop\Agent\build\status.json"
                };

                string statusPath = "";
                foreach (var p in possiblePaths)
                {
                    if (File.Exists(p)) { statusPath = p; break; }
                }

                if (string.IsNullOrEmpty(statusPath))
                {
                    ComponentsCard.Visibility = Visibility.Collapsed;
                    return;
                }

                string json;
                using (var fs = new FileStream(statusPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                using (var sr = new StreamReader(fs))
                {
                    json = sr.ReadToEnd();
                }

                using var doc = System.Text.Json.JsonDocument.Parse(json);
                var root = doc.RootElement;

                // Only show components card when enriched status is present
                if (!root.TryGetProperty("components", out var comps) &&
                    !root.TryGetProperty("connection", out _) &&
                    !root.TryGetProperty("workers", out _))
                {
                    ComponentsCard.Visibility = Visibility.Collapsed;
                    return;
                }

                ComponentsCard.Visibility = Visibility.Visible;
                ComponentsSections.Children.Clear();
                bool anySection = false;

                // ── CONNECTIONS ────────────────────────────────────────
                if (root.TryGetProperty("connection", out var connEl))
                {
                    var connTiles = new System.Collections.Generic.List<(string label, string icon, string status)>();

                    if (connEl.TryGetProperty("mTLS", out var mtlsProp) && mtlsProp.GetString() != "not_configured")
                        connTiles.Add(("mTLS", "⇄", mtlsProp.GetString() ?? "disconnected"));

                    if (connEl.TryGetProperty("telemetry", out var telProp) && telProp.GetString() != "not_configured")
                        connTiles.Add(("Telemetry", "◎", telProp.GetString() ?? "disconnected"));

                    if (connTiles.Count > 0)
                    {
                        AddSectionHeader("CONNECTIONS");
                        var wrap = CreateSectionPanel();
                        foreach (var t in connTiles) AddComponentTile(t.label, t.icon, t.status, wrap);
                        ComponentsSections.Children.Add(wrap);
                        anySection = true;
                    }
                }

                // ── COMPONENTS ─────────────────────────────────────────
                if (root.TryGetProperty("components", out var compsEl))
                {
                    var compTiles = new System.Collections.Generic.List<(string label, string icon, string status)>();
                    foreach (var comp in compsEl.EnumerateObject())
                    {
                        string displayName = comp.Name switch
                        {
                            "eventCollector" => "Events",
                            "batchSender"   => "Sender",
                            "fim"           => "Integrity",
                            "sysInfo"       => "Sys Info",
                            _               => comp.Name
                        };
                        string icon = comp.Name switch
                        {
                            "eventCollector" => "≡",
                            "batchSender"   => "⬆",
                            "fim"           => "⊟",
                            "sysInfo"       => "⊞",
                            _               => "◉"
                        };
                        string compStatus = comp.Value.TryGetProperty("status", out var csProp)
                            ? csProp.GetString() ?? "unknown" : "unknown";
                        compTiles.Add((displayName, icon, compStatus));
                    }
                    if (compTiles.Count > 0)
                    {
                        AddSectionHeader("COMPONENTS");
                        var wrap = CreateSectionPanel();
                        foreach (var t in compTiles) AddComponentTile(t.label, t.icon, t.status, wrap);
                        ComponentsSections.Children.Add(wrap);
                        anySection = true;
                    }
                }

                // ── WORKERS ────────────────────────────────────────────
                if (root.TryGetProperty("workers", out var workersEl) &&
                    workersEl.ValueKind == System.Text.Json.JsonValueKind.Object)
                {
                    var workerTiles = new System.Collections.Generic.List<(string label, string icon, string status)>();
                    foreach (var worker in workersEl.EnumerateObject())
                    {
                        if (worker.Value.ValueKind != System.Text.Json.JsonValueKind.Object) continue;
                        string displayName = worker.Name switch
                        {
                            "rp-webblock"  => "Web Block",
                            "rp-softblock" => "App Block",
                            "rp-patch"     => "Patching",
                            "rp-antivirus" => "Antivirus",
                            _              => worker.Name
                        };
                        string icon = worker.Name switch
                        {
                            "rp-webblock"  => "⊕",
                            "rp-softblock" => "⊘",
                            "rp-patch"     => "⬡",
                            "rp-antivirus" => "◈",
                            _              => "◉"
                        };
                        string wStatus = worker.Value.TryGetProperty("status", out var wsProp)
                            ? wsProp.GetString() ?? "idle" : "idle";
                        workerTiles.Add((displayName, icon, wStatus));
                    }
                    if (workerTiles.Count > 0)
                    {
                        AddSectionHeader("WORKERS");
                        var wrap = CreateSectionPanel();
                        foreach (var t in workerTiles) AddComponentTile(t.label, t.icon, t.status, wrap);
                        ComponentsSections.Children.Add(wrap);
                        anySection = true;
                    }
                }

                if (!anySection)
                    ComponentsCard.Visibility = Visibility.Collapsed;
            }
            catch
            {
                // Silently ignore — the card stays in its last known state
            }
        }

        // ── Status table helpers ─────────────────────────────────────────

        private StackPanel CreateSectionPanel() =>
            new StackPanel { Margin = new Thickness(0, 4, 0, 0) };

        private void AddSectionHeader(string title)
        {
            bool isFirst = ComponentsSections.Children.Count == 0;

            // Row: [label]  [──────────── line ────────────]
            var header = new Grid
            {
                Margin = new Thickness(0, isFirst ? 8 : 14, 0, 4)
            };
            header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            header.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            var label = new TextBlock
            {
                Text              = title,
                FontSize          = 9,
                FontWeight        = FontWeights.Bold,
                Foreground        = (Brush)new BrushConverter().ConvertFromString("#505056")!,
                VerticalAlignment = VerticalAlignment.Center,
                Margin            = new Thickness(0, 0, 8, 0)
            };
            Grid.SetColumn(label, 0);

            var line = new System.Windows.Shapes.Rectangle
            {
                Height            = 1,
                Fill              = (Brush)new BrushConverter().ConvertFromString("#272729")!,
                VerticalAlignment = VerticalAlignment.Center
            };
            Grid.SetColumn(line, 1);

            header.Children.Add(label);
            header.Children.Add(line);
            ComponentsSections.Children.Add(header);
        }

        private void AddComponentTile(string label, string icon, string status, Panel target)
        {
            // ── Colour semantics ────────────────────────────────────────
            Brush pipColor;
            string chipLabel;

            switch (status.ToLowerInvariant())
            {
                case "running":
                case "connected":
                case "active":
                    pipColor  = (Brush)new BrushConverter().ConvertFromString("#22C55E")!;
                    chipLabel = status == "connected" ? "connected" : "running";
                    break;
                case "idle":
                case "starting":
                    pipColor  = (Brush)new BrushConverter().ConvertFromString("#D4C84A")!;
                    chipLabel = status == "idle" ? "idle" : "starting";
                    break;
                case "stopped":
                case "disconnected":
                case "error":
                    pipColor  = (Brush)new BrushConverter().ConvertFromString("#EF4444")!;
                    chipLabel = status == "disconnected" ? "offline" : status == "error" ? "error" : "stopped";
                    break;
                case "degraded":
                case "suspended":
                    pipColor  = (Brush)new BrushConverter().ConvertFromString("#F97316")!;
                    chipLabel = status == "suspended" ? "suspended" : "degraded";
                    break;
                default:
                    pipColor  = (Brush)new BrushConverter().ConvertFromString("#505052")!;
                    chipLabel = status;
                    break;
            }

            // ── Row grid: [icon] | [name *] | [chip] ────────────────────
            var row = new Grid();
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(24) });                   // icon
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) }); // label
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });                      // chip

            var iconTb = new TextBlock
            {
                Text                = icon,
                FontSize            = 13,
                Foreground          = (Brush)new BrushConverter().ConvertFromString("#484850")!,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment   = VerticalAlignment.Center
            };
            Grid.SetColumn(iconTb, 0);

            var nameTb = new TextBlock
            {
                Text              = label,
                FontSize          = 12,
                Foreground        = (Brush)new BrushConverter().ConvertFromString("#A0A0A4")!,
                VerticalAlignment = VerticalAlignment.Center
            };
            Grid.SetColumn(nameTb, 1);

            // Status pill chip
            var chipBorder = new Border
            {
                CornerRadius        = new CornerRadius(10),
                Padding             = new Thickness(7, 2, 7, 2),
                Background          = (Brush)new BrushConverter().ConvertFromString("#1C1C1E")!,
                VerticalAlignment   = VerticalAlignment.Center
            };
            var chipInner = new StackPanel { Orientation = Orientation.Horizontal };
            chipInner.Children.Add(new System.Windows.Shapes.Ellipse
            {
                Width             = 5,
                Height            = 5,
                Fill              = pipColor,
                VerticalAlignment = VerticalAlignment.Center,
                Margin            = new Thickness(0, 0, 5, 0)
            });
            chipInner.Children.Add(new TextBlock
            {
                Text              = chipLabel,
                FontSize          = 9,
                FontWeight        = FontWeights.SemiBold,
                Foreground        = pipColor,
                VerticalAlignment = VerticalAlignment.Center
            });
            chipBorder.Child = chipInner;
            Grid.SetColumn(chipBorder, 2);

            row.Children.Add(iconTb);
            row.Children.Add(nameTb);
            row.Children.Add(chipBorder);

            // ── Hover wrapper ────────────────────────────────────────────
            var wrapper = new Border
            {
                CornerRadius = new CornerRadius(6),
                Padding      = new Thickness(6, 5, 8, 5),
                Margin       = new Thickness(0, 0, 0, 1),
                Background   = Brushes.Transparent,
                Child        = row
            };
            wrapper.MouseEnter += (s, _) =>
                ((Border)s).Background = (Brush)new BrushConverter().ConvertFromString("#212123")!;
            wrapper.MouseLeave += (s, _) =>
                ((Border)s).Background = Brushes.Transparent;

            target.Children.Add(wrapper);
        }

        private void UpdateStatus()
        {
            try
            {
                using (ServiceController sc = new ServiceController(ServiceName))
                {
                    switch (sc.Status)
                    {
                        case ServiceControllerStatus.Running:
                            StatusText.Text = "RUNNING";
                            StatusText.Foreground = _colorLimeCream;
                            StatusIndicator.Fill = _colorLimeCream;
                            BtnStart.IsEnabled = false;
                            BtnStop.IsEnabled = true;
                            break;
                        case ServiceControllerStatus.Stopped:
                            StatusText.Text = "STOPPED";
                            StatusText.Foreground = _colorPlatinum;
                            StatusIndicator.Fill = _colorPlatinum;
                            BtnStart.IsEnabled = true;
                            BtnStop.IsEnabled = false;
                            break;
                        default:
                            StatusText.Text = sc.Status.ToString().ToUpper();
                            StatusText.Foreground = _colorWhite;
                            StatusIndicator.Fill = _colorWhite;
                            BtnStart.IsEnabled = false;
                            BtnStop.IsEnabled = false;
                            break;
                    }
                }
            }
            catch (InvalidOperationException)
            {
                StatusText.Text = "NOT INSTALLED";
                StatusText.Foreground = _colorPlatinum;
                StatusIndicator.Fill = _colorPlatinum;
                BtnStart.IsEnabled = false;
                BtnStop.IsEnabled = false;
            }
            catch
            {
                StatusText.Text = "ERROR";
                StatusText.Foreground = _colorOrange;
            }
        }

        private void BtnOpenConfig_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                string[] searchPaths = new string[] {
                    baseDir,
                    Path.Combine(baseDir, "..", "..", "..", "..", ".."),
                    Path.Combine(baseDir, "..", "..", "..", ".."),
                    @"C:\Users\User\Desktop\Agent"
                };
                
                string configDir = baseDir;
                foreach (var p in searchPaths)
                {
                    if (File.Exists(Path.Combine(p, "config.json")))
                    {
                        configDir = Path.GetFullPath(p);
                        break;
                    }
                }

                System.Diagnostics.Process.Start("explorer.exe", configDir);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Could not open directory: {ex.Message}");
            }
        }

        // ── Tab navigation ────────────────────────────────────────────────

        private void TabOverview_Click(object sender, System.Windows.Input.MouseButtonEventArgs e)
            => SwitchTab(0);
        private void TabComponents_Click(object sender, System.Windows.Input.MouseButtonEventArgs e)
            => SwitchTab(1);
        private void TabLogs_Click(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            SwitchTab(2);
            RefreshLogs();
        }

        private void SwitchTab(int tab)
        {
            _activeTab = tab;

            TabOverviewPanel.Visibility    = tab == 0 ? Visibility.Visible : Visibility.Collapsed;
            TabComponentsPanel.Visibility  = tab == 1 ? Visibility.Visible : Visibility.Collapsed;
            TabLogsPanel.Visibility        = tab == 2 ? Visibility.Visible : Visibility.Collapsed;

            var active   = (Brush)new BrushConverter().ConvertFromString("#FFFFFF")!;
            var inactive = (Brush)new BrushConverter().ConvertFromString("#484850")!;

            TabLblOverview.Foreground   = tab == 0 ? active : inactive;
            TabLblComponents.Foreground = tab == 1 ? active : inactive;
            TabLblLogs.Foreground       = tab == 2 ? active : inactive;

            TabBarOverview.Visibility   = tab == 0 ? Visibility.Visible : Visibility.Collapsed;
            TabBarComponents.Visibility = tab == 1 ? Visibility.Visible : Visibility.Collapsed;
            TabBarLogs.Visibility       = tab == 2 ? Visibility.Visible : Visibility.Collapsed;
        }

        private void BtnOpenLogs_Click(object sender, RoutedEventArgs e)
            => SwitchTab(2);

        private void BtnCloseLogs_Click(object sender, RoutedEventArgs e)
            => SwitchTab(0);

        private void RefreshLogs()
        {
            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                string[] searchPaths = new string[] {
                    baseDir, Path.Combine(baseDir, "..", "..", "..", "..", ".."),
                    Path.Combine(baseDir, "..", "..", "..", ".."), @"C:\Users\User\Desktop\Agent"
                };
                
                string logFile = "";
                foreach (var p in searchPaths)
                {
                    string file = Path.Combine(p, "agent.log");
                    if (File.Exists(file)) { logFile = Path.GetFullPath(file); break; }
                }

                if (!string.IsNullOrEmpty(logFile))
                {
                    using (var fs = new FileStream(logFile, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                    using (var sr = new StreamReader(fs))
                    {
                        if (fs.Length > 5000) fs.Seek(-5000, SeekOrigin.End);
                        string text = sr.ReadToEnd();
                        
                        if (fs.Length > 5000 && text.Contains("\n"))
                            text = text.Substring(text.IndexOf('\n') + 1);

                        if (TxtLogViewer.Text != text)
                        {
                            bool scrollToEnd = TxtLogViewer.VerticalOffset >= TxtLogViewer.ExtentHeight - TxtLogViewer.ViewportHeight - 10;
                            TxtLogViewer.Text = text;
                            if (scrollToEnd) TxtLogViewer.ScrollToEnd();
                        }
                    }
                }
                else
                {
                    TxtLogViewer.Text = "[No logs found]";
                }
            }
            catch {}
        }

        private async void BtnStart_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                BtnStart.IsEnabled = false;
                using (ServiceController sc = new ServiceController(ServiceName))
                {
                    if (sc.Status == ServiceControllerStatus.Stopped)
                    {
                        sc.Start();
                        await Task.Run(() => sc.WaitForStatus(ServiceControllerStatus.Running, TimeSpan.FromSeconds(10)));
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Failed to start: {ex.Message}");
            }
            finally { UpdateStatus(); }
        }

        private async void BtnStop_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                BtnStop.IsEnabled = false;
                using (ServiceController sc = new ServiceController(ServiceName))
                {
                    if (sc.Status == ServiceControllerStatus.Running)
                    {
                        sc.Stop();
                        await Task.Run(() => sc.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(10)));
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Failed to stop: {ex.Message}");
            }
            finally { UpdateStatus(); }
        }
    }
}