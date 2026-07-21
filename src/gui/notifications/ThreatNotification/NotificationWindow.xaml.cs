using System.IO;
using System.Media;
using System.Security.Principal;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;

namespace ThreatNotification;

/// <summary>
/// Toast notification for blocked threats.
/// CLI: ThreatNotification.exe --file "x" --threat "y" --path "z" --source "u"
///      --severity critical --hash "abc" --slot 0 --timeout 10
/// Exit codes: 0=QUARANTINE, 1=IGNORE, 2=DETAILS, 3=DISMISSED, 4=AUTO_QUARANTINE, 5=UNDO
/// </summary>
public partial class NotificationWindow : Window
{
    // ── CLI Parameters ──────────────────────────────────────────
    private string _fileName   = "invoice_setup.exe";
    private string _threatName = "Win.Test.MockThreat-1";
    private string _filePath   = @"C:\Users\X\Downloads\invoice_setup.exe";
    private string _sourceUrl  = "";
    private string _hash       = "";
    private string _severity   = "high";
    private int    _slotIndex  = 0;
    private int    _timeout    = 10;
    private string _mode       = "threat";

    // ── State ───────────────────────────────────────────────────
    private int  _exitCode       = 0;
    private bool _closing        = false;
    private bool _confirmed      = false;
    private bool _isScanningMode = false;

    // ── DPI ─────────────────────────────────────────────────────
    private double _dpiScale = 1.0;
    private double _scale    = 1.0;

    // ── Timers ──────────────────────────────────────────────────
    private readonly DispatcherTimer _countdownTimer  = new();
    private readonly DispatcherTimer _confirmTimer    = new();
    private readonly DispatcherTimer _scanDotsTimer   = new();
    private readonly DispatcherTimer _scanProgressTimer = new();

    private long _lastLogOffset = -1;
    private readonly string _logFilePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Risknox Pulse", "antivirus", "clamscan.log");

    private int _tickCount = 0;
    private int _totalTicks;
    private int _scanDotsTick = 0;

    // ── Cached Brushes ──────────────────────────────────────────
    private static readonly SolidColorBrush BrushCountdownOrange = new((Color)ColorConverter.ConvertFromString("#F97316"));
    private static readonly SolidColorBrush BrushCountdownRed    = new((Color)ColorConverter.ConvertFromString("#EF4444"));
    private static readonly SolidColorBrush BrushCountdownGray   = new((Color)ColorConverter.ConvertFromString("#404044"));

    // ── Consts ──────────────────────────────────────────────────

    // ── Logging ─────────────────────────────────────────────────
    private static readonly string LogDir  = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
        "Risknox Pulse", "logs");
    private static readonly string LogFile = Path.Combine(LogDir, "threat_actions.log");

    // ═════════════════════════════════════════════════════════════
    // CONSTRUCTOR
    // ═════════════════════════════════════════════════════════════
    public NotificationWindow()
    {
        InitializeComponent();
        ParseArgs();
        BindData();
        ApplySeverity();
        ApplyFileTypeIcon();
        SetupCountdown();
        SetupConfirmTimer();

        if (_mode.Equals("safe", StringComparison.OrdinalIgnoreCase))
        {
            ApplySafeMode();
        }
        else if (_mode.Equals("scanning", StringComparison.OrdinalIgnoreCase))
        {
            ApplyScanningMode();
        }

        if (!_mode.Equals("scanning", StringComparison.OrdinalIgnoreCase))
        {
            CloseExistingScanningPopups();
        }

        // In scanning mode the countdown bar is replaced by an infinite pulse;
        // never start the countdown timer for scanning popups.
        _isScanningMode = _mode.Equals("scanning", StringComparison.OrdinalIgnoreCase);

        Loaded       += OnLoaded;
        Closing      += (_, _) => Environment.ExitCode = _exitCode;
    }

    // ═════════════════════════════════════════════════════════════
    // CLI PARSING
    // ═════════════════════════════════════════════════════════════
    private void ParseArgs()
    {
        var args = Environment.GetCommandLineArgs();
        for (int i = 1; i < args.Length - 1; i++)
        {
            switch (args[i].ToLowerInvariant())
            {
                case "--file":     _fileName   = args[++i]; break;
                case "--threat":   _threatName = args[++i]; break;
                case "--path":     _filePath   = args[++i]; break;
                case "--source":   _sourceUrl  = args[++i]; break;
                case "--hash":     _hash       = args[++i]; break;
                case "--severity": _severity   = args[++i]; break;
                case "--slot":     int.TryParse(args[++i], out _slotIndex); break;
                case "--timeout":  int.TryParse(args[++i], out _timeout); break;
                case "--mode":     _mode       = args[++i]; break;
            }
        }
    }

    // ═════════════════════════════════════════════════════════════
    // BIND DATA TO UI
    // ═════════════════════════════════════════════════════════════
    private void BindData()
    {
        RunFileName.Text    = _fileName;
        RunThreatName.Text  = _threatName;
        TxtPath.Text        = _filePath;
    }

    // ═════════════════════════════════════════════════════════════
    // FEATURE 1: SEVERITY BADGE + ACCENT COLOR
    // ═════════════════════════════════════════════════════════════
    private void ApplySeverity()
    {
        var (accentHex, badgeHex, label, tileBg, tileBorder) = _severity.ToLowerInvariant() switch
        {
            "critical" => ("#EF4444", "#DC2626", "CRITICAL", "#2D1410", "#3D1A14"),
            "high"     => ("#F97316", "#EA580C", "HIGH",     "#2D1F10", "#3D2A14"),
            "medium"   => ("#EAB308", "#CA8A04", "MEDIUM",   "#2D2A10", "#3D3514"),
            "low"      => ("#3B82F6", "#2563EB", "LOW",      "#101A2D", "#14253D"),
            _          => ("#EF4444", "#DC2626", "HIGH",     "#2D1410", "#3D1A14"),
        };

        AccentBar.Fill          = BrushFromHex(accentHex);
        SeverityBadge.Background = BrushFromHex(badgeHex);
        TxtSeverity.Text        = label;
        IconTile.Background     = BrushFromHex(tileBg);
        IconTile.BorderBrush    = BrushFromHex(tileBorder);
    }

    // ═════════════════════════════════════════════════════════════
    // FEATURE: SCANNING MODE
    // ═════════════════════════════════════════════════════════════
    private void ApplyScanningMode()
    {
        // ── Window Title (used to identify and kill this later) ──
        this.Title = "Risknox Scanning";

        // ── Accent → blue ─────────────────────────────────
        AccentBar.Fill = BrushFromHex("#3B82F6");

        // ── Icon tile → USB/download icon ─────────────────
        IconTile.Background  = BrushFromHex("#0D1A2D");
        IconTile.BorderBrush = BrushFromHex("#14253D");
        ShieldGroup.Visibility = Visibility.Collapsed;
        ScanGroup.Visibility   = Visibility.Visible;

        // ── Text ─────────────────────────────────────────
        bool isUsbDrive = _fileName.Length == 2 && _fileName.EndsWith(":");
        RunMainPrefix.Text  = isUsbDrive ? "Scanning USB Drive \u2014 " : "Scanning Download \u2014 ";
        RunFileName.Text    = _fileName;
        RunThreatName.Text  = "●○○";
        RunThreatName.Foreground = BrushFromHex("#3B82F6");
        TxtPath.Text        = "Running virus scan, please wait...";
        TxtPath.Foreground  = BrushFromHex("#A0A0A4");

        // ── Hide action buttons ──────────────────────────
        BtnQuarantine.Visibility = Visibility.Collapsed;
        LnkIgnore.Visibility     = Visibility.Collapsed;
        DotSep.Visibility        = Visibility.Collapsed;
        LnkDetails.Visibility    = Visibility.Collapsed;
        FileTypeBadge.Visibility = Visibility.Collapsed;
        SeverityBadge.Visibility = Visibility.Collapsed;

        // ── Replace countdown bar with an infinite looping pulse ─
        // Prevent the countdown from ever firing in scanning mode.
        _totalTicks = int.MaxValue;

        CountdownFill.Fill  = BrushFromHex("#3B82F6");
        CountdownFill.Width = 512;

        // ── Animated pulsing dots ────────────────────────
        _scanDotsTimer.Interval = TimeSpan.FromMilliseconds(500);
        _scanDotsTimer.Tick += (_, _) =>
        {
            _scanDotsTick++;
            RunThreatName.Text = (_scanDotsTick % 3) switch
            {
                0 => "●○○",
                1 => "●●○",
                _ => "●●●"
            };
        };
        _scanDotsTimer.Start();

        // ── Real-time file scanning progress ─────────────
        _scanProgressTimer.Interval = TimeSpan.FromMilliseconds(200);
        _scanProgressTimer.Tick += ScanProgressTimer_Tick;
        _scanProgressTimer.Start();
    }

    private void ScanProgressTimer_Tick(object sender, EventArgs e)
    {
        try
        {
            if (!File.Exists(_logFilePath)) return;

            using var fs = new FileStream(_logFilePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            if (_lastLogOffset == -1 || fs.Length < _lastLogOffset)
            {
                // First tick, or file was truncated/restarted -> jump to end
                _lastLogOffset = fs.Length;
                return;
            }

            if (fs.Length == _lastLogOffset) return;

            fs.Seek(_lastLogOffset, SeekOrigin.Begin);
            using var reader = new StreamReader(fs);
            string newContent = reader.ReadToEnd();
            _lastLogOffset = fs.Length;

            // Parse lines to find the last file scanned
            string[] lines = newContent.Split(new[] { '\n', '\r' }, StringSplitOptions.RemoveEmptyEntries);
            string lastFile = null;
            foreach (var line in lines)
            {
                if (line.Contains(": OK") || line.Contains(": Empty file") || line.Contains("FOUND"))
                {
                    int colonIdx = line.LastIndexOf(':');
                    if (colonIdx > 0)
                    {
                        lastFile = line.Substring(0, colonIdx).Trim();
                    }
                }
                else if (line.StartsWith("Scanning ")) 
                {
                    lastFile = line.Substring(9).Trim();
                }
            }

            if (!string.IsNullOrEmpty(lastFile))
            {
                string displayFile = Path.GetFileName(lastFile);
                if (string.IsNullOrEmpty(displayFile)) displayFile = lastFile;
                TxtPath.Text = "Scanning: " + displayFile;
            }
        }
        catch { /* Ignore sharing violations etc */ }
    }

    // ═════════════════════════════════════════════════════════════
    // FEATURE: SAFE MODE OVERRIDE
    // ═════════════════════════════════════════════════════════════
    private void ApplySafeMode()
    {
        // ── Morph accent → green ────────────────────────────
        AccentBar.Fill = BrushFromHex("#22C55E");

        // ── Morph icon → checkmark ──────────────────────────
        IconTile.Background  = BrushFromHex("#0D2818");
        IconTile.BorderBrush = BrushFromHex("#14401D");
        ShieldGroup.Visibility = Visibility.Collapsed;
        CheckGroup.Visibility  = Visibility.Visible;

        // ── Morph text ──────────────────────────────────────
        bool isUsbDrive = _fileName.Length == 2 && _fileName.EndsWith(":");
        RunMainPrefix.Text = isUsbDrive ? "USB Drive Verified Safe \u2014 " : "Download Verified Safe \u2014 ";
        RunThreatName.Text = _fileName;
        TxtThreat.Visibility = Visibility.Collapsed;
        
        string summaryText = "No threats detected";
        try
        {
            if (File.Exists(_logFilePath))
            {
                // Read the tail of the log file using a FileShare read stream
                using var fs = new FileStream(_logFilePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
                long startPos = Math.Max(0, fs.Length - 4096); // read last 4KB
                fs.Seek(startPos, SeekOrigin.Begin);
                using var reader = new StreamReader(fs);
                string tail = reader.ReadToEnd();

                // Look for the last SCAN SUMMARY block
                int summaryIdx = tail.LastIndexOf("----------- SCAN SUMMARY -----------");
                if (summaryIdx >= 0)
                {
                    string summaryBlock = tail.Substring(summaryIdx);
                    
                    string files = "", data = "", time = "";
                    foreach (var line in summaryBlock.Split(new[] { '\n', '\r' }, StringSplitOptions.RemoveEmptyEntries))
                    {
                        if (line.StartsWith("Scanned files:")) files = line.Substring(14).Trim();
                        else if (line.StartsWith("Data scanned:")) data = line.Substring(13).Trim();
                        else if (line.StartsWith("Time:")) time = line.Substring(5).Trim();
                    }

                    if (!string.IsNullOrEmpty(files) && !string.IsNullOrEmpty(time))
                    {
                        if (string.IsNullOrEmpty(data)) data = "0 MiB";
                        summaryText = $"No threats detected\nScanned {files} files ({data}) in {time}";
                    }
                }
            }
        }
        catch { /* ignore */ }

        TxtPath.Text         = summaryText;
        TxtPath.Foreground   = BrushFromHex("#A0A0A4");

        // ── Hide buttons ────────────────────────────────────
        BtnQuarantine.Visibility = Visibility.Collapsed;
        LnkIgnore.Visibility     = Visibility.Collapsed;
        DotSep.Visibility        = Visibility.Collapsed;
        LnkDetails.Visibility    = Visibility.Collapsed;
        FileTypeBadge.Visibility = Visibility.Collapsed;
        SeverityBadge.Visibility = Visibility.Collapsed;

        // ── Countdown bar → green ───────────────────────────
        CountdownFill.Fill  = BrushFromHex("#22C55E");
    }

    // ═════════════════════════════════════════════════════════════
    // FEATURE 4: FILE TYPE ICON
    // ═════════════════════════════════════════════════════════════
    private void ApplyFileTypeIcon()
    {
        var ext = Path.GetExtension(_fileName).ToLowerInvariant();
        TxtFileType.Text = ext switch
        {
            ".exe" or ".msi" or ".bat" or ".cmd" or ".ps1" or ".com" => "⚙",
            ".doc" or ".docx" or ".pdf" or ".xls" or ".xlsx" or ".ppt" or ".pptx" => "📄",
            ".zip" or ".rar" or ".7z" or ".tar" or ".gz" => "📦",
            ".html" or ".htm" or ".js" or ".vbs" or ".wsf" => "🌐",
            ".dll" or ".sys" or ".drv" => "🔧",
            ".iso" or ".img" => "💿",
            _ => "🛡"
        };
    }

    // ═════════════════════════════════════════════════════════════
    // FEATURE 3: COUNTDOWN BAR
    // ═════════════════════════════════════════════════════════════
    private void SetupCountdown()
    {
        _totalTicks = _timeout * 10;
        // Guard: _timeout 0 means "indefinite" for scanning mode; set a safe non-zero
        // value so the timer body never divides by zero if somehow invoked.
        if (_totalTicks <= 0) _totalTicks = int.MaxValue;
        _countdownTimer.Interval = TimeSpan.FromMilliseconds(100);
        _countdownTimer.Tick += (_, _) =>
        {
            if (_closing || _confirmed) return;

            _tickCount++;

            // Update countdown bar width + color
            double progress = (double)_tickCount / _totalTicks;
            CountdownFill.Width = 512.0 * (1.0 - progress);

            double secsLeft = (_totalTicks - _tickCount) / 10.0;
            if (secsLeft <= 2)
                CountdownFill.Fill = BrushCountdownRed;
            else if (secsLeft <= 5)
                CountdownFill.Fill = BrushCountdownOrange;
            // else stays default gray

            // In scanning mode _totalTicks == int.MaxValue — never auto-close.
            if (_tickCount >= _totalTicks && !_isScanningMode)
            {
                _countdownTimer.Stop();
                CloseWithResult(_mode.Equals("safe", StringComparison.OrdinalIgnoreCase) ? 3 : 4, "TIMEOUT");
            }
        };
    }

    // ═════════════════════════════════════════════════════════════
    // FEATURE 2: CONFIRMATION TIMER
    // ═════════════════════════════════════════════════════════════
    private void SetupConfirmTimer()
    {
        _confirmTimer.Interval = TimeSpan.FromSeconds(2);
        _confirmTimer.Tick += (_, _) =>
        {
            _confirmTimer.Stop();
            SlideOut();
        };
    }

    private void ShowConfirmation()
    {
        _confirmed = true;
        _countdownTimer.Stop();

        // Collapse hover if expanded

        // ── Morph accent → green ────────────────────────────
        AccentBar.Fill = BrushFromHex("#22C55E");

        // ── Morph icon → checkmark ──────────────────────────
        IconTile.Background  = BrushFromHex("#0D2818");
        IconTile.BorderBrush = BrushFromHex("#14401D");
        ShieldGroup.Visibility = Visibility.Collapsed;
        CheckGroup.Visibility  = Visibility.Visible;

        // ── Morph text ──────────────────────────────────────
        RunMainPrefix.Text = "Quarantined \u2014 ";
        RunThreatName.Text = _fileName;
        TxtThreat.Visibility = Visibility.Collapsed;
        TxtPath.Text         = "Threat moved to quarantine vault";
        TxtPath.Foreground   = BrushFromHex("#A0A0A4");

        // ── Swap button → Undo ──────────────────────────────
        BtnQuarantine.Visibility = Visibility.Collapsed;
        LnkUndo.Visibility       = Visibility.Visible;
        LnkIgnore.Visibility     = Visibility.Collapsed;
        DotSep.Visibility        = Visibility.Collapsed;
        LnkDetails.Visibility    = Visibility.Collapsed;
        FileTypeBadge.Visibility = Visibility.Collapsed;

        // ── Countdown bar → green full ──────────────────────
        CountdownFill.Width = 512;
        CountdownFill.Fill  = BrushFromHex("#22C55E");

        // ── Auto-close after 2s ─────────────────────────────
        _confirmTimer.Start();
    }

    private void RevertConfirmation()
    {
        _confirmed = false;
        _confirmTimer.Stop();

        // Restore visuals
        ApplySeverity();
        ShieldGroup.Visibility   = Visibility.Visible;
        CheckGroup.Visibility    = Visibility.Collapsed;
        RunMainPrefix.Text       = "Threat Blocked \u2014 ";
        RunFileName.Text         = _fileName;
        TxtThreat.Visibility     = Visibility.Visible;
        TxtPath.Text             = _filePath;
        TxtPath.Foreground       = BrushFromHex("#606066");
        RunThreatName.Foreground = BrushFromHex("#A0A0A4");
        SeverityBadge.Visibility = Visibility.Visible;
        BtnQuarantine.Visibility = Visibility.Visible;
        LnkUndo.Visibility       = Visibility.Collapsed;
        LnkIgnore.Visibility     = Visibility.Visible;
        DotSep.Visibility        = Visibility.Visible;
        LnkDetails.Visibility    = Visibility.Visible;
        FileTypeBadge.Visibility = Visibility.Visible;

        // Resume countdown bar
        double progress = (double)_tickCount / _totalTicks;
        CountdownFill.Width = 512.0 * (1.0 - progress);
        CountdownFill.Fill  = BrushFromHex("#404044");
        _countdownTimer.Start();
    }


    // ═════════════════════════════════════════════════════════════
    // LOADED — DPI fix + slide-in + sound
    // ═════════════════════════════════════════════════════════════
    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        // DPI compensation
        var source = PresentationSource.FromVisual(this);
        if (source?.CompositionTarget != null)
            _dpiScale = source.CompositionTarget.TransformToDevice.M11;

        if (_dpiScale > 1.01)
        {
            _scale = 1.0 / _dpiScale;
            Width  = 520 * _scale;
            Height = 120.0 * _scale;
            MainBorder.LayoutTransform = new ScaleTransform(_scale, _scale);
        }
        else
        {
            _scale = 1.0;
        }

        // Position bottom-right
        var screen = SystemParameters.WorkArea;
        int slotOffset = _slotIndex * ((int)(120.0 * _scale) + 12);
        Left = screen.Right  - Width  - 16;
        Top  = screen.Bottom - Height - 16 - slotOffset;

        // Start off-screen
        var targetTop = Top;
        Top = screen.Bottom;
        Opacity = 0;

        var slideIn = new DoubleAnimation
        {
            From = screen.Bottom, To = targetTop,
            Duration = TimeSpan.FromMilliseconds(250),
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
        };
        var fadeIn = new DoubleAnimation
        {
            From = 0, To = 1,
            Duration = TimeSpan.FromMilliseconds(200)
        };

        slideIn.Completed += (_, _) =>
        {
            try { SystemSounds.Hand.Play(); } catch { /* audio may not be available */ }

            // Don't start the countdown timer for scanning popups — they stay
            // open indefinitely and are killed by the arriving result popup.
            if (!_isScanningMode)
            {
                _countdownTimer.Start();
            }
            else
            {
                // Start the pulsing bar animation AFTER the visual tree is fully
                // attached. Use the Rectangle's own WidthProperty explicitly to
                // avoid resolving it as Window.WidthProperty via inheritance.
                var pulseAnim = new DoubleAnimation
                {
                    From           = 80,
                    To             = 512,
                    Duration       = TimeSpan.FromSeconds(1.4),
                    AutoReverse    = true,
                    RepeatBehavior = RepeatBehavior.Forever,
                    EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut }
                };
                // WidthProperty must be taken from the Rectangle type directly —
                // Window also inherits WidthProperty, so the unqualified name
                // resolves to the same DependencyProperty, but being explicit
                // prevents future ambiguity and potential runtime dispatch issues.
                var widthDp = System.Windows.Shapes.Rectangle.WidthProperty;
                CountdownFill.BeginAnimation(widthDp, pulseAnim);
            }
        };

        BeginAnimation(TopProperty, slideIn);
        BeginAnimation(OpacityProperty, fadeIn);
    }

    // ═════════════════════════════════════════════════════════════
    // SLIDE-OUT
    // ═════════════════════════════════════════════════════════════
    private void SlideOut()
    {
        if (_closing) return;
        _closing = true;
        _countdownTimer.Stop();
        _confirmTimer.Stop();
        _scanProgressTimer.Stop();
        _scanDotsTimer.Stop();

        var screen = SystemParameters.WorkArea;

        var slideOut = new DoubleAnimation
        {
            To = screen.Bottom,
            Duration = TimeSpan.FromMilliseconds(150),
            EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseIn }
        };
        var fadeOut = new DoubleAnimation { To = 0, Duration = TimeSpan.FromMilliseconds(150) };

        slideOut.Completed += (_, _) => Close();

        BeginAnimation(TopProperty, slideOut);
        BeginAnimation(OpacityProperty, fadeOut);
    }

    // ═════════════════════════════════════════════════════════════
    // ACTION HANDLERS
    // ═════════════════════════════════════════════════════════════
    private void BtnQuarantine_Click(object sender, RoutedEventArgs e)
    {
        WriteThreatLog("QUARANTINE");
        _exitCode = 0;
        ShowConfirmation();
    }

    private void BtnClose_Click(object sender, RoutedEventArgs e)
        => CloseWithResult(3, "DISMISSED");

    private void LnkDetails_Click(object sender, MouseButtonEventArgs e)
        => CloseWithResult(2, "DETAILS");

    private void LnkIgnore_Click(object sender, MouseButtonEventArgs e)
        => CloseWithResult(1, "IGNORE");

    private void LnkUndo_Click(object sender, MouseButtonEventArgs e)
    {
        WriteThreatLog("UNDO");
        RevertConfirmation();
    }

    // ═════════════════════════════════════════════════════════════
    // CLOSE + LOG
    // ═════════════════════════════════════════════════════════════
    private void CloseWithResult(int exitCode, string action)
    {
        _exitCode = exitCode;
        WriteThreatLog(action);
        SlideOut();
    }

    private void WriteThreatLog(string action)
    {
        try
        {
            Directory.CreateDirectory(LogDir);
            var entry = JsonSerializer.Serialize(new
            {
                timestamp  = DateTime.Now.ToString("o"),
                action,
                severity   = _severity,
                fileName   = _fileName,
                threatName = _threatName,
                filePath   = _filePath,
                sourceUrl  = _sourceUrl,
                hash       = _hash
            });
            File.AppendAllText(LogFile, entry + Environment.NewLine);
        }
        catch { /* Silent */ }
    }

    private void CloseExistingScanningPopups()
    {
        try
        {
            int currentId = System.Diagnostics.Process.GetCurrentProcess().Id;
            foreach (var proc in System.Diagnostics.Process.GetProcessesByName("ThreatNotification"))
            {
                if (proc.Id != currentId)
                {
                    proc.Kill();
                }
            }
        }
        catch { /* ignore */ }
    }

    // ═════════════════════════════════════════════════════════════
    // HELPERS
    // ═════════════════════════════════════════════════════════════
    private static SolidColorBrush BrushFromHex(string hex)
        => new((Color)ColorConverter.ConvertFromString(hex));
}
