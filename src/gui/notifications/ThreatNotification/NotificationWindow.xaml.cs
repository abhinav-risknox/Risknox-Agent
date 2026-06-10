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

    // ── State ───────────────────────────────────────────────────
    private int  _exitCode       = 0;
    private bool _closing        = false;
    private bool _confirmed      = false;

    // ── DPI ─────────────────────────────────────────────────────
    private double _dpiScale = 1.0;
    private double _scale    = 1.0;

    // ── Timers ──────────────────────────────────────────────────
    private readonly DispatcherTimer _countdownTimer  = new();
    private readonly DispatcherTimer _confirmTimer    = new();
    private int _tickCount = 0;
    private int _totalTicks;

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
                CountdownFill.Fill = BrushFromHex("#EF4444"); // Red
            else if (secsLeft <= 5)
                CountdownFill.Fill = BrushFromHex("#F97316"); // Orange
            // else stays default gray

            if (_tickCount >= _totalTicks)
            {
                _countdownTimer.Stop();
                CloseWithResult(4, "AUTO_QUARANTINE");
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
        RunThreatName.Text = "";
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
            SystemSounds.Hand.Play();
            _countdownTimer.Start();
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
    {
        var identity  = WindowsIdentity.GetCurrent();
        var principal = new WindowsPrincipal(identity);
        if (principal.IsInRole(WindowsBuiltInRole.Administrator))
        {
            CloseWithResult(1, "IGNORE");
        }
        else
        {
            RunThreatName.Text       = "Run as Administrator to ignore";
            RunThreatName.Foreground = BrushFromHex("#EF4444");
            SeverityBadge.Visibility = Visibility.Collapsed;
        }
    }

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

    // ═════════════════════════════════════════════════════════════
    // HELPERS
    // ═════════════════════════════════════════════════════════════
    private static SolidColorBrush BrushFromHex(string hex)
        => new((Color)ColorConverter.ConvertFromString(hex));
}
