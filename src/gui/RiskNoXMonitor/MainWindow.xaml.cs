using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Security.Cryptography.X509Certificates;
using System.ServiceProcess;
using System.Text.Json;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;
using System.Windows.Shapes;
using System.Windows.Threading;

namespace RisknoxMonitor
{
    public partial class MainWindow : Window
    {
        private const string ServiceName = "ResolutePulse";
        private DispatcherTimer _timer;
        private int _devClickCount = 0;

        // ── Color palette ─────────────────────────────────────────────
        private readonly Brush _colorLimeCream = (Brush)new BrushConverter().ConvertFromString("#22C55E")!;
        private readonly Brush _colorPlatinum  = (Brush)new BrushConverter().ConvertFromString("#EAEAEB")!;
        private readonly Brush _colorWhite     = (Brush)new BrushConverter().ConvertFromString("#FFFFFF")!;
        private readonly Brush _colorOrange    = (Brush)new BrushConverter().ConvertFromString("#FF5B00")!;
        private readonly Brush _colorRed       = (Brush)new BrushConverter().ConvertFromString("#EF4444")!;
        private readonly Brush _colorDimGray   = (Brush)new BrushConverter().ConvertFromString("#505056")!;
        private readonly Brush _colorCardBg    = (Brush)new BrushConverter().ConvertFromString("#252527")!;

        private readonly Brush _orangeGradient = new LinearGradientBrush(
            Color.FromRgb(0xFF, 0x5B, 0x00),
            Color.FromRgb(0xFF, 0x3B, 0x00),
            new Point(0, 0), new Point(1, 1));

        private readonly Brush _darkButtonBg = (Brush)new BrushConverter().ConvertFromString("#252527")!;

        // ── Breathing animation ───────────────────────────────────────
        private Storyboard? _breathingStoryboard;
        private bool _isBreathing = false;

        // ── Error banner state ────────────────────────────────────────
        private string _lastErrorText = "";

        // ── Module tile tracking ──────────────────────────────────────
        private class TileRefs
        {
            public Border Card = null!;
            public Ellipse StatusDot = null!;
            public TextBlock StatusLabel = null!;
            public TextBlock DetailText = null!;
            public Border DetailPanel = null!;
            public bool IsExpanded = false;
        }
        private readonly Dictionary<string, TileRefs> _moduleTiles = new();
        private bool _tilesBuilt = false;

        // ── Fixed module definitions (always shown) ──────────────────
        private static readonly (string key, string displayName, string icon)[] AllModules = new[]
        {
            ("rp-webblock",  "Web Block",    "⊕"),
            ("rp-softblock", "App Block",    "⊘"),
            ("rp-patch",     "Patching",     "⬡"),
            ("rp-antivirus", "Antivirus",    "◈"),
            ("fim",          "File Monitor", "◎"),
            ("batchSender",  "Telemetry",    "⇅"),
        };

        // ── Status file paths ─────────────────────────────────────────
        private string? _cachedStatusPath;

        // ════════════════════════════════════════════════════════════════
        //  CONSTRUCTOR
        // ════════════════════════════════════════════════════════════════

        public MainWindow()
        {
            InitializeComponent();

            // Show version
            try
            {
                var ver = Assembly.GetExecutingAssembly().GetName().Version;
                if (ver != null)
                    TxtVersion.Text = $"v{ver.Major}.{ver.Minor}.{ver.Build}";
            }
            catch { }

            _timer = new DispatcherTimer();
            _timer.Interval = TimeSpan.FromSeconds(2);
            _timer.Tick += Timer_Tick;
            _timer.Start();

            // Initial update
            UpdateServiceStatus();
            var statusRoot = ReadStatusJson();
            if (statusRoot.HasValue)
            {
                UpdateConnectionStatus(statusRoot.Value);
                UpdateModuleTiles(statusRoot.Value);
                UpdateLicenseStatus(statusRoot.Value);
                UpdateCertStatus(statusRoot.Value);
                UpdatePhaseAndErrors(statusRoot.Value);
            }
            else
            {
                UpdateLicenseFallback();
                // Still build the module tiles even without status.json
                BuildInitialTiles();
            }
        }

        // ════════════════════════════════════════════════════════════════
        //  TIMER TICK — single parse, multiple consumers
        // ════════════════════════════════════════════════════════════════

        private void Timer_Tick(object? sender, EventArgs e)
        {
            UpdateServiceStatus();

            var statusRoot = ReadStatusJson();
            if (statusRoot.HasValue)
            {
                UpdateConnectionStatus(statusRoot.Value);
                UpdateModuleTiles(statusRoot.Value);
                UpdateLicenseStatus(statusRoot.Value);
                UpdateCertStatus(statusRoot.Value);
                UpdatePhaseAndErrors(statusRoot.Value);
                UpdateShieldFromComponents(statusRoot.Value);
            }
            else
            {
                UpdateLicenseFallback();
            }
        }

        // ════════════════════════════════════════════════════════════════
        //  STATUS.JSON READER (single read per tick)
        // ════════════════════════════════════════════════════════════════

        private string? FindStatusFile()
        {
            // Return cached path if it still exists
            if (_cachedStatusPath != null && File.Exists(_cachedStatusPath))
                return _cachedStatusPath;

            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            string[] searchPaths = new[]
            {
                System.IO.Path.Combine(baseDir, "status.json"),
                System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                    "Risknox Pulse", "status.json"),
                System.IO.Path.Combine(baseDir, "..", "..", "..", "..", "..", "build", "status.json"),
                System.IO.Path.Combine(baseDir, "..", "..", "..", "..", "build", "status.json"),
            };

            foreach (var p in searchPaths)
            {
                if (File.Exists(p))
                {
                    _cachedStatusPath = System.IO.Path.GetFullPath(p);
                    return _cachedStatusPath;
                }
            }
            return null;
        }

        private JsonElement? ReadStatusJson()
        {
            try
            {
                string? path = FindStatusFile();
                if (path == null) return null;

                string json;
                using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                using (var sr = new StreamReader(fs))
                {
                    json = sr.ReadToEnd();
                }

                var doc = JsonDocument.Parse(json);
                return doc.RootElement.Clone();
            }
            catch
            {
                return null;
            }
        }

        // ════════════════════════════════════════════════════════════════
        //  SERVICE STATUS (Windows Service Controller)
        // ════════════════════════════════════════════════════════════════

        private void UpdateServiceStatus()
        {
            try
            {
                using (ServiceController sc = new ServiceController(ServiceName))
                {
                    ServiceControllerStatus status = sc.Status;

                    if (status == ServiceControllerStatus.StartPending)
                    {
                        SetStatusUI("STARTING…", _colorOrange, false, "Starting service…", false);
                        return;
                    }
                    if (status == ServiceControllerStatus.StopPending)
                    {
                        SetStatusUI("STOPPING…", _colorOrange, false, "Stopping service…", false);
                        return;
                    }

                    switch (status)
                    {
                        case ServiceControllerStatus.Running:
                            SetStatusUI("PROTECTED", _colorLimeCream, true, "Stop Agent", true);
                            break;
                        case ServiceControllerStatus.Stopped:
                            SetStatusUI("STOPPED", _colorPlatinum, true, "Start Agent", false);
                            break;
                        default:
                            SetStatusUI(status.ToString().ToUpper(), _colorWhite, false, "Please wait…", false);
                            break;
                    }
                }
            }
            catch (InvalidOperationException)
            {
                SetStatusUI("NOT INSTALLED", _colorPlatinum, false, "Install Service First", false);
            }
            catch
            {
                SetStatusUI("ERROR", _colorRed, false, "Check Logs", false);
            }
        }

        private void SetStatusUI(string text, Brush color, bool canToggle, string buttonText, bool isProtected)
        {
            StatusText.Text = text;
            AnimateShieldColor(color);

            BtnAction.Content = buttonText;
            BtnAction.IsEnabled = canToggle;

            if (isProtected)
            {
                BtnAction.Background = _darkButtonBg;
                StartIndicatorPulse();
            }
            else
            {
                BtnAction.Background = _orangeGradient;
                StopIndicatorPulse();
            }
        }

        // ════════════════════════════════════════════════════════════════
        //  CONNECTION STATUS (from status.json)
        // ════════════════════════════════════════════════════════════════

        private void UpdateConnectionStatus(JsonElement root)
        {
            try
            {
                if (root.TryGetProperty("connection", out var connEl))
                {
                    // mTLS status
                    if (connEl.TryGetProperty("mTLS", out var mtlsProp))
                    {
                        string mtls = mtlsProp.GetString() ?? "unknown";
                        bool connected = mtls == "connected";
                        TxtConnStatus.Text = connected ? "Connected" : CapitalizeFirst(mtls);
                        ConnDot.Fill = connected ? _colorLimeCream : _colorDimGray;
                    }

                    // Manager host
                    if (connEl.TryGetProperty("managerHost", out var hostProp))
                    {
                        string host = hostProp.GetString() ?? "";
                        TxtManagerHost.Text = string.IsNullOrEmpty(host) ? "—" : host;
                    }
                }

                // Events counters (legacy fields for easy access)
                long collected = 0, sent = 0, buffered = 0;

                if (root.TryGetProperty("eventsCollected", out var ecProp))
                    collected = ecProp.GetInt64();
                if (root.TryGetProperty("eventsSent", out var esProp))
                    sent = esProp.GetInt64();

                if (root.TryGetProperty("components", out var compEl) &&
                    compEl.TryGetProperty("batchSender", out var bsEl) &&
                    bsEl.TryGetProperty("eventsBuffered", out var bufProp))
                    buffered = bufProp.GetInt64();

                TxtEventCounters.Text = $"{collected:N0} in · {sent:N0} out";
                TxtBuffered.Text = buffered > 0 ? $"{buffered:N0} pending" : "0";
            }
            catch { }
        }

        // ════════════════════════════════════════════════════════════════
        //  MODULE TILES (workers + components)
        // ════════════════════════════════════════════════════════════════

        private void UpdateModuleTiles(JsonElement root)
        {
            try
            {
                // Build tiles once from the fixed list
                if (!_tilesBuilt)
                {
                    var initialModules = new List<(string key, string displayName, string icon, string status, string detail)>();
                    foreach (var (key, displayName, icon) in AllModules)
                        initialModules.Add((key, displayName, icon, "—", ""));
                    RebuildModuleTilesGrid(initialModules);
                    _tilesBuilt = true;
                }

                // Collect live statuses from status.json
                var liveStatus = new Dictionary<string, (string status, string detail)>();

                // Workers
                if (root.TryGetProperty("workers", out var workersEl) &&
                    workersEl.ValueKind == JsonValueKind.Object)
                {
                    foreach (var worker in workersEl.EnumerateObject())
                    {
                        if (worker.Value.ValueKind != JsonValueKind.Object) continue;
                        string wStatus = worker.Value.TryGetProperty("status", out var wsProp)
                            ? wsProp.GetString() ?? "idle" : "idle";
                        liveStatus[worker.Name] = (wStatus, "");
                    }
                }

                // Components
                if (root.TryGetProperty("components", out var compEl) &&
                    compEl.ValueKind == JsonValueKind.Object)
                {
                    if (compEl.TryGetProperty("fim", out var fimEl))
                    {
                        string fStatus = fimEl.TryGetProperty("status", out var fsProp)
                            ? fsProp.GetString() ?? "idle" : "idle";
                        liveStatus["fim"] = (fStatus, "");
                    }

                    if (compEl.TryGetProperty("batchSender", out var bsEl))
                    {
                        string bStatus = bsEl.TryGetProperty("status", out var bsProp)
                            ? bsProp.GetString() ?? "stopped" : "stopped";
                        long eSent = bsEl.TryGetProperty("eventsSent", out var esProp) ? esProp.GetInt64() : 0;
                        long bSent = bsEl.TryGetProperty("batchesSent", out var bbProp) ? bbProp.GetInt64() : 0;
                        string detail = $"{eSent:N0} events · {bSent:N0} batches";
                        liveStatus["batchSender"] = (bStatus, detail);
                    }
                }

                // Update all tiles with live data (or "—" if no data)
                foreach (var (key, _, _) in AllModules)
                {
                    if (_moduleTiles.TryGetValue(key, out var refs))
                    {
                        if (liveStatus.TryGetValue(key, out var live))
                            UpdateTileStatus(refs, live.status, live.detail);
                        else
                            UpdateTileStatus(refs, "—", "");
                    }
                }
            }
            catch { }
        }

        private void RebuildModuleTilesGrid(List<(string key, string displayName, string icon, string status, string detail)> modules)
        {
            _moduleTiles.Clear();
            ModuleTilesGrid.RowDefinitions.Clear();
            ModuleTilesGrid.Children.Clear();

            int row = 0;
            for (int i = 0; i < modules.Count; i += 2)
            {
                ModuleTilesGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

                // Left tile
                var leftTile = CreateModuleTile(modules[i].key, modules[i].displayName,
                    modules[i].icon, modules[i].status, modules[i].detail);
                Grid.SetRow(leftTile, row);
                Grid.SetColumn(leftTile, 0);
                leftTile.Margin = new Thickness(0, 0, 0, 6);
                ModuleTilesGrid.Children.Add(leftTile);

                // Right tile (if exists)
                if (i + 1 < modules.Count)
                {
                    var rightTile = CreateModuleTile(modules[i + 1].key, modules[i + 1].displayName,
                        modules[i + 1].icon, modules[i + 1].status, modules[i + 1].detail);
                    Grid.SetRow(rightTile, row);
                    Grid.SetColumn(rightTile, 2);
                    rightTile.Margin = new Thickness(0, 0, 0, 6);
                    ModuleTilesGrid.Children.Add(rightTile);
                }

                row++;
            }
        }

        private Border CreateModuleTile(string key, string displayName, string icon, string status, string detail)
        {
            Brush accentColor = StatusToBrush(status);

            // ── Card ──
            var card = new Border
            {
                Background = _colorCardBg,
                CornerRadius = new CornerRadius(8),
                Padding = new Thickness(12, 10, 12, 10),
                BorderThickness = new Thickness(2, 0, 0, 0),
                BorderBrush = accentColor,
                Cursor = Cursors.Hand,
                ClipToBounds = true,
            };

            var mainStack = new StackPanel();

            // ── Header row: icon + name + dot ──
            var headerGrid = new Grid();
            headerGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            headerGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            headerGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            var iconBlock = new TextBlock
            {
                Text = icon,
                FontSize = 14,
                Foreground = (Brush)new BrushConverter().ConvertFromString("#808086")!,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(0, 0, 8, 0)
            };
            Grid.SetColumn(iconBlock, 0);

            var nameBlock = new TextBlock
            {
                Text = displayName,
                FontSize = 12,
                FontWeight = FontWeights.SemiBold,
                Foreground = Brushes.White,
                VerticalAlignment = VerticalAlignment.Center,
            };
            Grid.SetColumn(nameBlock, 1);

            var statusDot = new Ellipse
            {
                Width = 8,
                Height = 8,
                Fill = accentColor,
                VerticalAlignment = VerticalAlignment.Center,
            };
            Grid.SetColumn(statusDot, 2);

            headerGrid.Children.Add(iconBlock);
            headerGrid.Children.Add(nameBlock);
            headerGrid.Children.Add(statusDot);

            // ── Status label ──
            var statusLabel = new TextBlock
            {
                Text = CapitalizeFirst(status),
                FontSize = 10,
                Foreground = (Brush)new BrushConverter().ConvertFromString("#707076")!,
                Margin = new Thickness(22, 2, 0, 0),
            };

            // ── Expandable detail panel ──
            var detailStack = new StackPanel { Margin = new Thickness(0, 6, 0, 0) };

            var detailSeparator = new Rectangle
            {
                Height = 1,
                Fill = (Brush)new BrushConverter().ConvertFromString("#2E2E30")!,
                Margin = new Thickness(0, 0, 0, 6)
            };
            detailStack.Children.Add(detailSeparator);

            var detailText = new TextBlock
            {
                Text = string.IsNullOrEmpty(detail) ? $"Status: {status}" : detail,
                FontSize = 11,
                Foreground = (Brush)new BrushConverter().ConvertFromString("#808086")!,
                TextWrapping = TextWrapping.Wrap,
            };
            detailStack.Children.Add(detailText);

            var detailPanel = new Border
            {
                Child = detailStack,
                ClipToBounds = true,
            };
            detailPanel.LayoutTransform = new ScaleTransform(1, 0);

            mainStack.Children.Add(headerGrid);
            mainStack.Children.Add(statusLabel);
            mainStack.Children.Add(detailPanel);
            card.Child = mainStack;

            // ── Store refs ──
            var refs = new TileRefs
            {
                Card = card,
                StatusDot = statusDot,
                StatusLabel = statusLabel,
                DetailText = detailText,
                DetailPanel = detailPanel,
                IsExpanded = false,
            };
            _moduleTiles[key] = refs;

            // ── Click handler for expand / collapse ──
            card.MouseLeftButtonDown += (s, e) => ToggleTileExpand(refs);

            return card;
        }

        private void ToggleTileExpand(TileRefs refs)
        {
            double targetScaleY = refs.IsExpanded ? 0 : 1;
            var anim = new DoubleAnimation(targetScaleY, TimeSpan.FromMilliseconds(200))
            {
                EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseInOut }
            };

            var transform = refs.DetailPanel.LayoutTransform as ScaleTransform;
            transform?.BeginAnimation(ScaleTransform.ScaleYProperty, anim);

            refs.IsExpanded = !refs.IsExpanded;
        }

        private void UpdateTileStatus(TileRefs refs, string status, string detail)
        {
            Brush accentColor = StatusToBrush(status);

            // Animate dot color
            if (refs.StatusDot.Fill is SolidColorBrush oldBrush)
            {
                Color targetColor = ((SolidColorBrush)accentColor).Color;
                if (oldBrush.Color != targetColor)
                {
                    var scb = oldBrush.IsFrozen ? oldBrush.Clone() : oldBrush;
                    refs.StatusDot.Fill = scb;
                    var anim = new ColorAnimation { To = targetColor, Duration = TimeSpan.FromSeconds(0.4) };
                    scb.BeginAnimation(SolidColorBrush.ColorProperty, anim);
                }
            }

            // Update left accent border
            refs.Card.BorderBrush = accentColor;

            // Update text
            refs.StatusLabel.Text = CapitalizeFirst(status);
            if (!string.IsNullOrEmpty(detail))
                refs.DetailText.Text = detail;
            else
                refs.DetailText.Text = $"Status: {status}";
        }

        private Brush StatusToBrush(string status)
        {
            return status switch
            {
                "running" => _colorLimeCream,
                "active"  => _colorLimeCream,
                "idle"    => _colorDimGray,
                "stopped" => _colorDimGray,
                "—"       => _colorDimGray,
                "error"   => _colorRed,
                _         => _colorOrange,
            };
        }

        private void BuildInitialTiles()
        {
            if (_tilesBuilt) return;
            var initialModules = new List<(string key, string displayName, string icon, string status, string detail)>();
            foreach (var (key, displayName, icon) in AllModules)
                initialModules.Add((key, displayName, icon, "—", ""));
            RebuildModuleTilesGrid(initialModules);
            _tilesBuilt = true;
        }

        // ════════════════════════════════════════════════════════════════
        //  LICENSE / SUBSCRIPTION STATUS
        // ════════════════════════════════════════════════════════════════

        private void UpdateLicenseStatus(JsonElement root)
        {
            try
            {
                bool suspended = root.TryGetProperty("licenseSuspended", out var suspProp) && suspProp.GetBoolean();
                string message = root.TryGetProperty("licenseMessage", out var msgProp) ? msgProp.GetString() ?? "" : "";
                string licType = root.TryGetProperty("licenseType", out var typeProp) ? typeProp.GetString() ?? "" : "";
                string licExpiry = root.TryGetProperty("licenseExpiry", out var expProp) ? expProp.GetString() ?? "" : "";

                string typeDisplay = licType switch
                {
                    "ENTERPRISE" => "Enterprise License",
                    "STANDARD"   => "Standard License",
                    "TRIAL"      => "Trial License",
                    "NONE"       => "No License",
                    _            => "Agent Subscription"
                };

                string expiryDetail = "";
                if (!string.IsNullOrEmpty(licExpiry) && DateTime.TryParse(licExpiry, out var expDate))
                {
                    int daysLeft = (expDate - DateTime.UtcNow).Days;
                    expiryDetail = daysLeft > 0 ? $"Expires in {daysLeft}d" : "Expired";
                }

                TxtLicenseType.Text = typeDisplay;
                TxtLicenseDetail.Text = expiryDetail;

                if (suspended)
                {
                    TxtExpiry.Text = "⚠ Suspended";
                    TxtExpiry.Foreground = _colorOrange;
                }
                else if (message == "License active")
                {
                    TxtExpiry.Text = "✓ Active";
                    TxtExpiry.Foreground = (Brush)new BrushConverter().ConvertFromString("#4CAF50")!;
                }
                else
                {
                    TxtExpiry.Text = message;
                    TxtExpiry.Foreground = _colorWhite;
                }
            }
            catch
            {
                TxtLicenseType.Text = "Agent Subscription";
                TxtExpiry.Text = "Unknown";
                TxtExpiry.Foreground = _colorPlatinum;
            }
        }

        private void UpdateLicenseFallback()
        {
            // Fallback: read cert expiry if no status.json
            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                string[] possiblePaths = new[]
                {
                    System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                        "Risknox Pulse", "certs", "agent.crt"),
                    System.IO.Path.Combine(baseDir, "certs", "agent.crt"),
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

        // ════════════════════════════════════════════════════════════════
        //  CERTIFICATE HEALTH
        // ════════════════════════════════════════════════════════════════

        private void UpdateCertStatus(JsonElement root)
        {
            try
            {
                if (root.TryGetProperty("cert", out var certEl))
                {
                    int daysLeft = certEl.TryGetProperty("daysLeft", out var dlProp) ? dlProp.GetInt32() : -1;
                    bool renewable = certEl.TryGetProperty("renewable", out var rnProp) && rnProp.GetBoolean();

                    if (daysLeft < 0)
                    {
                        TxtCertStatus.Text = "Certificate: unknown";
                        CertDot.Fill = _colorDimGray;
                    }
                    else if (daysLeft == 0)
                    {
                        TxtCertStatus.Text = "Certificate: expired";
                        CertDot.Fill = _colorRed;
                    }
                    else if (renewable)
                    {
                        TxtCertStatus.Text = $"Certificate: {daysLeft}d (renewal pending)";
                        CertDot.Fill = _colorOrange;
                    }
                    else
                    {
                        TxtCertStatus.Text = $"Certificate: {daysLeft}d remaining";
                        CertDot.Fill = _colorLimeCream;
                    }
                }
                else if (root.TryGetProperty("certDaysLeft", out var legacyProp))
                {
                    int daysLeft = legacyProp.GetInt32();
                    TxtCertStatus.Text = daysLeft > 0 ? $"Certificate: {daysLeft}d remaining" : "Certificate: expired";
                    CertDot.Fill = daysLeft > 30 ? _colorLimeCream : (daysLeft > 0 ? _colorOrange : _colorRed);
                }
            }
            catch { }
        }

        // ════════════════════════════════════════════════════════════════
        //  PHASE & ERRORS
        // ════════════════════════════════════════════════════════════════

        private void UpdatePhaseAndErrors(JsonElement root)
        {
            try
            {
                // Agent phase → shield detail text
                if (root.TryGetProperty("phase", out var phaseProp))
                {
                    string phase = phaseProp.GetString() ?? "";
                    if (!string.IsNullOrEmpty(phase) && phase != "running")
                    {
                        TxtShieldDetail.Text = CapitalizeFirst(phase) + "…";
                    }
                    // "running" phase → let shield strength logic set the detail text
                }

                // Error banner
                string lastError = root.TryGetProperty("lastError", out var errProp) ? errProp.GetString() ?? "" : "";
                if (!string.IsNullOrEmpty(lastError))
                {
                    ShowErrorBanner(lastError);
                }
                else
                {
                    HideErrorBanner();
                }
            }
            catch { }
        }

        // ════════════════════════════════════════════════════════════════
        //  SHIELD STRENGTH (from workers + components)
        // ════════════════════════════════════════════════════════════════

        private void UpdateShieldFromComponents(JsonElement root)
        {
            try
            {
                int totalShields = 0;
                int activeShields = 0;

                if (root.TryGetProperty("connection", out var connEl))
                {
                    if (connEl.TryGetProperty("mTLS", out var mtlsProp) && mtlsProp.GetString() != "not_configured")
                    {
                        totalShields++;
                        string status = mtlsProp.GetString()!;
                        if (status == "connected" || status == "registered" || status == "active") activeShields++;
                    }
                }

                if (root.TryGetProperty("workers", out var workersEl) &&
                    workersEl.ValueKind == JsonValueKind.Object)
                {
                    foreach (var worker in workersEl.EnumerateObject())
                    {
                        if (worker.Value.ValueKind != JsonValueKind.Object) continue;
                        totalShields++;
                        string wStatus = worker.Value.TryGetProperty("status", out var wsProp)
                            ? wsProp.GetString() ?? "idle" : "idle";
                        if (wStatus == "running") activeShields++;
                    }
                }

                if (totalShields > 0)
                {
                    double strength = (double)activeShields / totalShields;
                    if (strength >= 1.0)
                    {
                        TxtShieldDetail.Text = "All systems operational";
                    }
                    else if (strength > 0.5)
                    {
                        TxtShieldDetail.Text = "Partially protected";
                    }
                    else
                    {
                        TxtShieldDetail.Text = "System at risk";
                    }
                }
            }
            catch { }
        }

        // ════════════════════════════════════════════════════════════════
        //  ANIMATIONS
        // ════════════════════════════════════════════════════════════════

        private void AnimateShieldColor(Brush color)
        {
            Color targetColor = ((SolidColorBrush)color).Color;
            var anim = new ColorAnimation { To = targetColor, Duration = TimeSpan.FromSeconds(0.5) };

            ShieldGlow.BeginAnimation(DropShadowEffect.ColorProperty, anim);
            GlowColorStop.BeginAnimation(GradientStop.ColorProperty, anim);

            if (ShieldIcon.Stroke is SolidColorBrush scb)
            {
                if (scb.IsFrozen) ShieldIcon.Stroke = scb.Clone();
                ShieldIcon.Stroke.BeginAnimation(SolidColorBrush.ColorProperty, anim);
            }
        }

        private void StartIndicatorPulse()
        {
            if (_isBreathing) return;
            _isBreathing = true;

            var opacityAnim = new DoubleAnimation
            {
                From = 0.10,
                To = 0.30,
                Duration = TimeSpan.FromSeconds(2),
                AutoReverse = true,
                RepeatBehavior = RepeatBehavior.Forever,
                EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut }
            };

            _breathingStoryboard = new Storyboard();
            Storyboard.SetTarget(opacityAnim, ShieldOuterGlow);
            Storyboard.SetTargetProperty(opacityAnim, new PropertyPath(UIElement.OpacityProperty));
            _breathingStoryboard.Children.Add(opacityAnim);
            _breathingStoryboard.Begin();
        }

        private void StopIndicatorPulse()
        {
            if (!_isBreathing) return;
            _isBreathing = false;

            _breathingStoryboard?.Stop();
            _breathingStoryboard = null;
            ShieldOuterGlow.Opacity = 0.15;
        }

        // ── Error banner slide animation ──────────────────────────────
        private void ShowErrorBanner(string error)
        {
            if (error == _lastErrorText && ErrorBanner.Visibility == Visibility.Visible)
                return;

            _lastErrorText = error;
            TxtError.Text = error;
            ErrorBanner.Visibility = Visibility.Visible;

            var fadeIn = new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(300))
            {
                EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut }
            };
            var slideIn = new DoubleAnimation(-12, 0, TimeSpan.FromMilliseconds(300))
            {
                EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut }
            };

            ErrorBanner.BeginAnimation(UIElement.OpacityProperty, fadeIn);
            ErrorBannerTranslate.BeginAnimation(TranslateTransform.YProperty, slideIn);
        }

        private void HideErrorBanner()
        {
            if (ErrorBanner.Visibility == Visibility.Collapsed) return;

            _lastErrorText = "";
            var fadeOut = new DoubleAnimation(1, 0, TimeSpan.FromMilliseconds(200));
            fadeOut.Completed += (s, e) =>
            {
                ErrorBanner.Visibility = Visibility.Collapsed;
            };
            var slideOut = new DoubleAnimation(0, -12, TimeSpan.FromMilliseconds(200));

            ErrorBanner.BeginAnimation(UIElement.OpacityProperty, fadeOut);
            ErrorBannerTranslate.BeginAnimation(TranslateTransform.YProperty, slideOut);
        }

        // ════════════════════════════════════════════════════════════════
        //  WINDOW CHROME HANDLERS
        // ════════════════════════════════════════════════════════════════

        private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (e.LeftButton == MouseButtonState.Pressed)
            {
                this.DragMove();
            }
        }

        private void BtnMinimize_Click(object sender, RoutedEventArgs e)
        {
            this.WindowState = WindowState.Minimized;
        }

        private void BtnClose_Click(object sender, RoutedEventArgs e)
        {
            this.Close();
        }

        // ════════════════════════════════════════════════════════════════
        //  SHIELD ICON (dev mode toggle)
        // ════════════════════════════════════════════════════════════════

        private void ShieldIcon_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            _devClickCount++;

            // Visual feedback tap
            var anim = new DoubleAnimation(0.9, 1.0, TimeSpan.FromMilliseconds(100));
            var transform = new ScaleTransform(1, 1, 22.5, 22.5);
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

        // ════════════════════════════════════════════════════════════════
        //  MAINTENANCE BUTTONS
        // ════════════════════════════════════════════════════════════════

        private void BtnOpenConfig_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                string[] searchPaths = new[]
                {
                    System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                        "Risknox Pulse"),
                    baseDir
                };

                string configDir = baseDir;
                foreach (var p in searchPaths)
                {
                    if (File.Exists(System.IO.Path.Combine(p, "config.json")))
                    {
                        configDir = System.IO.Path.GetFullPath(p);
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
                string logFile = System.IO.Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                    "Risknox Pulse", "agent.log");
                if (File.Exists(logFile))
                {
                    System.Diagnostics.Process.Start("powershell.exe",
                        $"-NoExit -Command \"Get-Content '{logFile}' -Wait -Tail 100\"");
                }
                else
                {
                    MessageBox.Show("Agent log file not found at:\n" + logFile,
                        "Log Not Found", MessageBoxButton.OK, MessageBoxImage.Information);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show("Could not open log file: " + ex.Message,
                    "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void BtnCloseLogs_Click(object sender, RoutedEventArgs e) { }
        private void RefreshLogs() { }

        // ════════════════════════════════════════════════════════════════
        //  SERVICE START / STOP
        // ════════════════════════════════════════════════════════════════

        private async void BtnAction_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                BtnAction.IsEnabled = false;
                using (ServiceController sc = new ServiceController(ServiceName))
                {
                    if (sc.Status == ServiceControllerStatus.Stopped)
                    {
                        BtnAction.Content = "Starting…";
                        sc.Start();
                        await Task.Run(() => sc.WaitForStatus(ServiceControllerStatus.Running, TimeSpan.FromSeconds(15)));
                    }
                    else if (sc.Status == ServiceControllerStatus.Running)
                    {
                        BtnAction.Content = "Stopping…";
                        sc.Stop();
                        await Task.Run(() => sc.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(15)));
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Operation failed: {ex.Message}", "Service Error",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
            finally
            {
                UpdateServiceStatus();
            }
        }

        // ════════════════════════════════════════════════════════════════
        //  HELPERS
        // ════════════════════════════════════════════════════════════════

        private static string CapitalizeFirst(string s)
        {
            if (string.IsNullOrEmpty(s)) return s;
            return char.ToUpper(s[0]) + s[1..];
        }
    }
}