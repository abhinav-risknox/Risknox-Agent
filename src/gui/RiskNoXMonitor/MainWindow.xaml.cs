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
        private int _devClickCount = 0;

        private readonly Brush _colorLimeCream = (Brush)new BrushConverter().ConvertFromString("#22C55E")!; // Vibrant Green
        private readonly Brush _colorPlatinum = (Brush)new BrushConverter().ConvertFromString("#EAEAEB")!;
        private readonly Brush _colorWhite = (Brush)new BrushConverter().ConvertFromString("#FFFFFF")!;
        private readonly Brush _colorOrange = (Brush)new BrushConverter().ConvertFromString("#FF5B00")!;
        private readonly Brush _colorRed = (Brush)new BrushConverter().ConvertFromString("#EF4444")!; // Vibrant Red

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


        private void StartIndicatorPulse() { }

        private void StopIndicatorPulse() { }

        private void Timer_Tick(object? sender, EventArgs e)
        {
            UpdateStatus();
            UpdateLicenseStatus();
            UpdateComponentsStatus();
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
                    Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Risknox Pulse", "status.json"),
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
                    Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Risknox Pulse", "certs", "agent.crt"),
                    Path.Combine(baseDir, "certs", "agent.crt"),
                };

                string certPath = "";
                foreach (var p in possiblePaths)
                {
                    if (File.Exists(p)) { certPath = p; break; }
                }

                if (!string.IsNullOrEmpty(certPath))
                {
                    var cert = X509CertificateLoader.LoadCertificateFromFile(certPath);
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
                    Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Risknox Pulse", "status.json"),
                    Path.Combine(baseDir, "..", "..", "..", "..", "..", "build", "status.json"),
                    Path.Combine(baseDir, "..", "..", "..", "..", "build", "status.json"),
                    @"C:\Users\User\Desktop\Agent\build\status.json"
                };

                string statusPath = "";
                foreach (var p in possiblePaths)
                {
                    if (File.Exists(p)) { statusPath = p; break; }
                }

                if (string.IsNullOrEmpty(statusPath)) return;

                string json;
                using (var fs = new FileStream(statusPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                using (var sr = new StreamReader(fs))
                {
                    json = sr.ReadToEnd();
                }

                using var doc = System.Text.Json.JsonDocument.Parse(json);
                var root = doc.RootElement;

                int totalShields = 0;
                int activeShields = 0;

                // ── CONNECTIONS ────────────────────────────────────────
                if (root.TryGetProperty("connection", out var connEl))
                {
                    var connTiles = new System.Collections.Generic.List<(string label, string icon, string status)>();
                    if (connEl.TryGetProperty("mTLS", out var mtlsProp) && mtlsProp.GetString() != "not_configured")
                    {
                        string status = mtlsProp.GetString()!;
                        connTiles.Add(("mTLS Registration", "🔐", status));
                        totalShields++;
                        if (status == "registered" || status == "active") activeShields++;
                    }
                    if (connEl.TryGetProperty("server", out var srvProp) && srvProp.GetString() != "not_configured")
                    {
                        string status = srvProp.GetString()!;
                        connTiles.Add(("Management Server", "🌐", status));
                        totalShields++;
                        if (status == "connected") activeShields++;
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
                        totalShields++;
                        string wStatus = worker.Value.TryGetProperty("status", out var wsProp) ? wsProp.GetString() ?? "idle" : "idle";
                        if (wStatus == "running") activeShields++;

                        string displayName = worker.Name switch {
                            "rp-webblock"  => "Web Block",
                            "rp-softblock" => "App Block",
                            "rp-patch"     => "Patching",
                            "rp-antivirus" => "Antivirus",
                            _              => worker.Name
                        };
                        string icon = worker.Name switch {
                            "rp-webblock"  => "⊕",
                            "rp-softblock" => "⊘",
                            "rp-patch"     => "⬡",
                            "rp-antivirus" => "◈",
                            _              => "◉"
                        };
                        workerTiles.Add((displayName, icon, wStatus));
                    }
                }


                UpdateShieldStrength(activeShields, totalShields);
            }
            catch
            {
                // Silently ignore
            }
        }

        private void UpdateShieldStrength(int active, int total)
        {
            if (total == 0) return;
            double strength = (double)active / total;
            
            if (strength >= 1.0) {
                TxtShieldDetail.Text = "All systems operational";
                AnimateShieldColor(_colorLimeCream);
            } else if (strength > 0.5) {
                TxtShieldDetail.Text = "Partially protected";
                AnimateShieldColor(_colorOrange);
            } else {
                TxtShieldDetail.Text = "System at risk";
                AnimateShieldColor(_colorRed);
            }
        }

        private void AnimateShieldColor(Brush color)
        {
            Color targetColor = ((SolidColorBrush)color).Color;
            
            var anim = new ColorAnimation { To = targetColor, Duration = TimeSpan.FromSeconds(0.5) };
            
            // Animate Glows
            ShieldGlow.BeginAnimation(DropShadowEffect.ColorProperty, anim);
            GlowColorStop.BeginAnimation(GradientStop.ColorProperty, anim);
            
            // Animate Shield Stroke (must target the color of the brush)
            if (ShieldIcon.Stroke is SolidColorBrush scb)
            {
                if (scb.IsFrozen) ShieldIcon.Stroke = scb.Clone();
                ShieldIcon.Stroke.BeginAnimation(SolidColorBrush.ColorProperty, anim);
            }
        }

        // ── Status table helpers ─────────────────────────────────────────


        private void UpdateStatus()
        {
            try
            {
                using (ServiceController sc = new ServiceController(ServiceName))
                {
                    ServiceControllerStatus status = sc.Status;
                    
                    // Immediately show intermediate states
                    if (status == ServiceControllerStatus.StartPending)
                    {
                        SetStatusUI("STARTING...", _colorOrange, false, "Starting service...", "#FF5B00");
                        return;
                    }
                    if (status == ServiceControllerStatus.StopPending)
                    {
                        SetStatusUI("STOPPING...", _colorOrange, false, "Stopping service...", "#FF5B00");
                        return;
                    }

                    switch (status)
                    {
                        case ServiceControllerStatus.Running:
                            SetStatusUI("PROTECTED", _colorLimeCream, true, "Stop Agent", "#252527");
                            break;
                        case ServiceControllerStatus.Stopped:
                            SetStatusUI("STOPPED", _colorPlatinum, true, "Start Agent", "#FF5B00");
                            break;
                        default:
                            SetStatusUI(status.ToString().ToUpper(), _colorWhite, false, "Please wait...", "#FF5B00");
                            break;
                    }
                }
            }
            catch (InvalidOperationException)
            {
                SetStatusUI("NOT INSTALLED", _colorPlatinum, false, "Install Service First", "#FF5B00");
            }
            catch
            {
                SetStatusUI("ERROR", _colorRed, false, "Check Logs", "#FF5B00");
            }
        }

        private void ShieldIcon_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            _devClickCount++;
            
            // Visual feedback for the tap
            var anim = new DoubleAnimation(0.9, 1.0, TimeSpan.FromMilliseconds(100));
            var transform = new ScaleTransform(1, 1, 22.5, 22.5); // Center of 45x45
            ShieldIcon.RenderTransform = transform;
            transform.BeginAnimation(ScaleTransform.ScaleXProperty, anim);
            transform.BeginAnimation(ScaleTransform.ScaleYProperty, anim);

            if (_devClickCount >= 5)
            {
                BtnOpenLogs.Visibility = BtnOpenLogs.Visibility == Visibility.Visible 
                    ? Visibility.Collapsed 
                    : Visibility.Visible;
                _devClickCount = 0;
            }
        }

        private void SetStatusUI(string text, Brush color, bool canToggle, string buttonText, string buttonColor = "#FF5B00")
        {
            StatusText.Text = text;
            
            // Animate shield icon and glow
            AnimateShieldColor(color);

            // Button update
            BtnAction.Content = buttonText;
            BtnAction.IsEnabled = canToggle;
            BtnAction.Background = (Brush)new BrushConverter().ConvertFromString(buttonColor)!;
        }

        private void BtnOpenConfig_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                string[] searchPaths = new string[] {
                    Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Risknox Pulse"),
                    baseDir
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

        private void BtnOpenLogs_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                string logFile = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Risknox Pulse", "agent.log");
                if (File.Exists(logFile))
                {
                    System.Diagnostics.Process.Start("powershell.exe", $"-NoExit -Command \"Get-Content '{logFile}' -Wait -Tail 100\"");
                }
                else
                {
                    MessageBox.Show("Agent log file not found at:\n" + logFile, "Log Not Found", MessageBoxButton.OK, MessageBoxImage.Information);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show("Could not open log file: " + ex.Message, "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void BtnCloseLogs_Click(object sender, RoutedEventArgs e) { }

        private void RefreshLogs() { }

        private async void BtnAction_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                BtnAction.IsEnabled = false;
                using (ServiceController sc = new ServiceController(ServiceName))
                {
                    if (sc.Status == ServiceControllerStatus.Stopped)
                    {
                        BtnAction.Content = "Starting...";
                        sc.Start();
                        await Task.Run(() => sc.WaitForStatus(ServiceControllerStatus.Running, TimeSpan.FromSeconds(15)));
                    }
                    else if (sc.Status == ServiceControllerStatus.Running)
                    {
                        BtnAction.Content = "Stopping...";
                        sc.Stop();
                        await Task.Run(() => sc.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(15)));
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Operation failed: {ex.Message}", "Service Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally 
            { 
                UpdateStatus(); 
            }
        }
    }
}