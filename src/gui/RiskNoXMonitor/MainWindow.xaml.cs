using System;
using System.IO;
using System.Security.Cryptography.X509Certificates;
using System.ServiceProcess;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;

namespace RisknoxMonitor
{
    public partial class MainWindow : Window
    {
        private const string ServiceName = "ResolutePulse";
        private DispatcherTimer _timer;

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
            if (LogViewerGrid.Visibility == Visibility.Visible)
            {
                RefreshLogs();
            }
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

        private void BtnOpenLogs_Click(object sender, RoutedEventArgs e)
        {
            if (LogViewerGrid.Visibility == Visibility.Visible)
            {
                BtnCloseLogs_Click(sender, e);
            }
            else
            {
                LogViewerGrid.Visibility = Visibility.Visible;
                this.SizeToContent = SizeToContent.Height;
                RefreshLogs();
            }
        }

        private void BtnCloseLogs_Click(object sender, RoutedEventArgs e)
        {
            LogViewerGrid.Visibility = Visibility.Collapsed;
            this.SizeToContent = SizeToContent.Height;
        }

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