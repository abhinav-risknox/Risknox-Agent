// ThreatNotification.exe — Professional GDI+ desktop notification popup
//
// Design: CrowdStrike/SentinelOne inspired enterprise security aesthetic
//   - Dark #16161A background, severity-colored accent bar
//   - Clean text hierarchy: heading / threat · badge / path
//   - Flat solid buttons (no gradients), text links for secondary actions
//   - Smooth slide-in/out animation, countdown bar
//
// CLI:
//   ThreatNotification.exe --file <name> --threat <name> --path <path>
//                          [--severity critical|high|medium|low]
//                          [--slot <int>] [--timeout <sec>] [--mode threat|safe|scanning]
//
// Exit codes: 0=QUARANTINE  1=IGNORE  2=DETAILS  3=DISMISSED  4=AUTO_QUARANTINE

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <objbase.h>
#include <gdiplus.h>
using namespace Gdiplus;

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <cstdlib>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

// ─────────────────────────────────────────────────────────────────────────────
// Exit codes
// ─────────────────────────────────────────────────────────────────────────────
enum ExitCode { EC_QUARANTINE=0, EC_IGNORE=1, EC_DETAILS=2, EC_DISMISSED=3, EC_AUTO_QUARANTINE=4 };

// ─────────────────────────────────────────────────────────────────────────────
// Timer IDs
// ─────────────────────────────────────────────────────────────────────────────
constexpr UINT_PTR TID_ANIM=1, TID_COUNTDOWN=2, TID_SCAN=3, TID_CONFIRM=4;

// ─────────────────────────────────────────────────────────────────────────────
// Base geometry (logical px @ 96 DPI)
// ─────────────────────────────────────────────────────────────────────────────
constexpr int BASE_W    = 360;
constexpr int BASE_H    = 86;
constexpr int CORNER_R  = 8;
constexpr int EDGE_MARGIN = 12;
constexpr int SLOT_GAP  = 8;

// ─────────────────────────────────────────────────────────────────────────────
// Design tokens — professional enterprise palette
// ─────────────────────────────────────────────────────────────────────────────
namespace DS {
    // Backgrounds
    inline Color BG()          { return Color(255, 0x16,0x16,0x1A); } // near-black
    inline Color BORDER()      { return Color(255, 0x2A,0x2A,0x30); } // subtle border
    inline Color TILE_NEUTRAL(){ return Color(255, 0x22,0x22,0x28); }

    // Text
    inline Color TEXT_PRIMARY()   { return Color(255, 0xFA,0xFA,0xFA); } // almost white
    inline Color TEXT_SECONDARY() { return Color(255, 0xA1,0xA1,0xAA); } // zinc-400
    inline Color TEXT_MUTED()     { return Color(255, 0x52,0x52,0x5B); } // zinc-600
    inline Color TEXT_DIM()       { return Color(255, 0x3F,0x3F,0x46); } // zinc-700

    // Severity colours
    inline Color CRITICAL() { return Color(255, 0xDC,0x26,0x26); } // red-600
    inline Color HIGH()     { return Color(255, 0xEA,0x58,0x0C); } // orange-600
    inline Color MEDIUM()   { return Color(255, 0xD9,0x77,0x06); } // amber-600
    inline Color LOW()      { return Color(255, 0x25,0x63,0xEB); } // blue-600
    inline Color SAFE()     { return Color(255, 0x16,0xA3,0x4A); } // green-600
    inline Color SCAN()     { return Color(255, 0x25,0x63,0xEB); } // blue-600

    // Tile backgrounds (very dark tint of severity color)
    inline Color TILE_CRITICAL() { return Color(255, 0x1F,0x09,0x09); }
    inline Color TILE_HIGH()     { return Color(255, 0x1C,0x0D,0x04); }
    inline Color TILE_MEDIUM()   { return Color(255, 0x1C,0x14,0x03); }
    inline Color TILE_LOW()      { return Color(255, 0x08,0x10,0x20); }
    inline Color TILE_SAFE()     { return Color(255, 0x06,0x17,0x0B); }
    inline Color TILE_SCAN()     { return Color(255, 0x08,0x10,0x20); }

    // Button hover
    inline Color BTN_HOVER_CLOSE() { return Color(255, 0x7F,0x1D,0x1D); } // dark red
    inline Color BTN_HOVER_QUAR()  { return Color(255, 0xB9,0x1C,0x1C); } // red-700
    inline Color LINK_HOVER()      { return Color(255, 0xE4,0xE4,0xE7); } // zinc-200
}

// ─────────────────────────────────────────────────────────────────────────────
// App state
// ─────────────────────────────────────────────────────────────────────────────
struct State {
    // CLI params
    std::wstring fileName   = L"unknown.exe";
    std::wstring threatName = L"Unknown Threat";
    std::wstring filePath;
    std::wstring severity   = L"high";
    std::wstring mode       = L"threat";
    int slotIndex           = 0;
    int timeoutSecs         = 10;

    // Window
    HWND  hwnd      = nullptr;
    int   W = BASE_W, H = BASE_H;
    float sc = 1.0f;
    int   posX = 0, finalY = 0, offScreenY = 0;

    // Animation
    enum Phase { SLIDE_IN, IDLE, SLIDE_OUT } phase = SLIDE_IN;
    int animTick = 0, animTotal = 18;   // 18 × 16ms ≈ 290ms

    // Countdown (100ms ticks)
    int cntTick = 0, cntTotal = 0;

    // Scanning
    int  scanTick = 0;
    long long logOff = -1;
    std::wstring logPath, scanStatus;

    // Interaction state
    bool confirmed = false;
    bool closing   = false;
    int  exitCode  = EC_DISMISSED;

    // Hover flags
    bool hvClose = false;
    bool hvQuar  = false;
    bool hvIgn   = false;
    bool hvDet   = false;
    bool hvUndo  = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Derived helpers
// ─────────────────────────────────────────────────────────────────────────────

static Color AccentColor(const State& s) {
    if (s.confirmed)              return DS::SAFE();
    if (s.mode == L"safe")        return DS::SAFE();
    if (s.mode == L"scanning")    return DS::SCAN();
    if (s.severity == L"critical")return DS::CRITICAL();
    if (s.severity == L"medium")  return DS::MEDIUM();
    if (s.severity == L"low")     return DS::LOW();
    return DS::HIGH();
}

static Color TileColor(const State& s) {
    if (s.confirmed || s.mode == L"safe")  return DS::TILE_SAFE();
    if (s.mode == L"scanning")             return DS::TILE_SCAN();
    if (s.severity == L"critical")         return DS::TILE_CRITICAL();
    if (s.severity == L"medium")           return DS::TILE_MEDIUM();
    if (s.severity == L"low")              return DS::TILE_LOW();
    return DS::TILE_HIGH();
}

static std::wstring SevLabel(const State& s) {
    if (s.severity == L"critical") return L"CRITICAL";
    if (s.severity == L"medium")   return L"MEDIUM";
    if (s.severity == L"low")      return L"LOW";
    return L"HIGH";
}

static std::wstring ScanDots(int tick) {
    switch ((tick / 3) % 3) {
        case 0:  return L"scanning.";
        case 1:  return L"scanning..";
        default: return L"scanning...";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GDI+ drawing helpers
// ─────────────────────────────────────────────────────────────────────────────

static float S(float v, float sc) { return v * sc; }

static void RoundedRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d,   0.0f, 90.0f);
    path.AddArc(x,         y + h - d, d, d,  90.0f, 90.0f);
    path.CloseFigure();
}

// Draw text in a rect with given alignment
static void DrawText(
    Graphics& g, const std::wstring& text, Font& font, Color col,
    float x, float y, float w, float h,
    StringAlignment hAlign = StringAlignmentNear,
    StringTrimming trim    = StringTrimmingEllipsisCharacter)
{
    SolidBrush br(col);
    RectF rc(x, y, w, h);
    StringFormat sf;
    sf.SetAlignment(hAlign);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetTrimming(trim);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(text.c_str(), -1, &font, rc, &sf, &br);
}

// Draw path text (with ellipsis in middle of path)
static void DrawPathText(
    Graphics& g, const std::wstring& text, Font& font, Color col,
    float x, float y, float w, float h)
{
    SolidBrush br(col);
    RectF rc(x, y, w, h);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetTrimming(StringTrimmingEllipsisPath);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(text.c_str(), -1, &font, rc, &sf, &br);
}

// Easing functions
static double EaseOutCubic(double t) { double u = 1.0 - t; return 1.0 - u*u*u; }
static double EaseInQuad(double t)   { return t * t; }

// ─────────────────────────────────────────────────────────────────────────────
// Vector icon drawing
// ─────────────────────────────────────────────────────────────────────────────

static void DrawShieldIcon(Graphics& g, float cx, float cy, float sc, Color col) {
    // Clean minimal shield — two arcs + straight sides
    Pen p(col, 1.5f * sc);
    p.SetLineJoin(LineJoinRound);
    p.SetLineCap(LineCapRound, LineCapRound, DashCapRound);

    // Shield outline path
    float hw = 7.5f * sc;  // half-width
    float ht = 10.0f * sc; // half-height
    GraphicsPath shield;
    // Top arc + sides tapering to bottom point
    PointF pts[] = {
        {cx,       cy - ht},       // top-center
        {cx + hw,  cy - ht + 2*sc},// top-right
        {cx + hw,  cy - 2*sc},    // right
        {cx,       cy + ht},       // bottom point
        {cx - hw,  cy - 2*sc},    // left
        {cx - hw,  cy - ht + 2*sc},// top-left
        {cx,       cy - ht},       // back to top
    };
    shield.AddLines(pts, 7);
    g.DrawPath(&p, &shield);

    // Exclamation mark inside (threat mode visual)
    SolidBrush fb(col);
    float barW = 1.8f * sc, barH = 5.5f * sc;
    g.FillRectangle(&fb,
        cx - barW*0.5f, cy - 6.0f*sc,
        barW, barH);
    g.FillEllipse(&fb,
        cx - barW*0.5f, cy + 0.5f*sc,
        barW, barW);
}

static void DrawCheckIcon(Graphics& g, float cx, float cy, float sc, Color col) {
    Pen p(col, 2.2f * sc);
    p.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    p.SetLineJoin(LineJoinRound);
    PointF pts[] = {
        {cx - 6.5f*sc, cy - 0.5f*sc},
        {cx - 2.0f*sc, cy + 4.5f*sc},
        {cx + 7.0f*sc, cy - 5.5f*sc}
    };
    g.DrawLines(&p, pts, 3);
}

static void DrawScanIcon(Graphics& g, float cx, float cy, float sc, Color col) {
    // Magnifying glass
    Pen p(col, 1.8f * sc);
    p.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    float cr = 5.5f * sc;
    g.DrawEllipse(&p, cx - cr - 2*sc, cy - cr - 1.5f*sc, cr*2.0f, cr*2.0f);
    g.DrawLine(&p,
        PointF(cx + cr*0.7f - 2*sc, cy + cr*0.7f - 1.5f*sc),
        PointF(cx + 7.5f*sc,        cy + 7.0f*sc));
}

// ─────────────────────────────────────────────────────────────────────────────
// Log polling (scanning mode)
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring ReadSafeSummary(const std::wstring& logPath) {
    std::ifstream f(logPath.c_str(), std::ios::binary | std::ios::ate);
    if (!f) return L"No threats detected";
    auto sz = (long long)f.tellg();
    f.seekg(std::max(0LL, sz - 4096));
    std::string tail((std::istreambuf_iterator<char>(f)), {});
    auto idx = tail.rfind("----------- SCAN SUMMARY -----------");
    if (idx == std::string::npos) return L"No threats detected";
    auto trim = [](std::string s) {
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        if (!s.empty()) s.erase(s.find_last_not_of(" \t\r\n") + 1);
        return s;
    };
    std::string block = tail.substr(idx), files, dur, line;
    std::istringstream ss(block);
    while (std::getline(ss, line)) {
        if (line.rfind("Scanned files:", 0) == 0) files = trim(line.substr(14));
        else if (line.rfind("Time:", 0) == 0)     dur   = trim(line.substr(5));
    }
    if (!files.empty() && !dur.empty()) {
        return L"Scanned " + std::wstring(files.begin(), files.end())
             + L" files in " + std::wstring(dur.begin(), dur.end());
    }
    return L"No threats detected";
}

static void PollScanLog(State* st) {
    if (st->logPath.empty()) return;
    try {
        std::ifstream f(st->logPath.c_str(), std::ios::binary | std::ios::ate);
        if (!f) return;
        long long sz = (long long)f.tellg();
        if (st->logOff < 0) { st->logOff = sz; return; }
        if (sz <= st->logOff) return;
        f.seekg(st->logOff);
        std::string chunk((std::istreambuf_iterator<char>(f)), {});
        st->logOff = sz;
        std::istringstream ss(chunk);
        std::string line, lastFile;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.find(": OK") != std::string::npos ||
                line.find("FOUND") != std::string::npos) {
                auto ci = line.rfind(':');
                if (ci != std::string::npos) lastFile = line.substr(0, ci);
            } else if (line.rfind("Scanning ", 0) == 0) {
                lastFile = line.substr(9);
            }
        }
        if (!lastFile.empty()) {
            auto fn = std::filesystem::path(lastFile).filename().string();
            if (fn.empty()) fn = lastFile;
            if (fn.size() > 52) fn = "..." + fn.substr(fn.size() - 49);
            st->scanStatus = std::wstring(fn.begin(), fn.end());
        }
    } catch (...) {}
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit testing (in logical px, scaled)
// ─────────────────────────────────────────────────────────────────────────────

static bool Hit(const State* st, int mx, int my, float l, float t, float r, float b) {
    float sc = st->sc;
    return mx >= (int)(l*sc) && mx < (int)(r*sc)
        && my >= (int)(t*sc) && my < (int)(b*sc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Render — called from WM_PAINT into a back-buffer DC
// ─────────────────────────────────────────────────────────────────────────────

static void RenderFrame(Graphics& g, const State* st) {
    const float sc  = st->sc;
    const float W   = (float)st->W;
    const float H   = (float)st->H;

    const bool isThreat   = (st->mode == L"threat");
    const bool isSafe     = (st->mode == L"safe");
    const bool isScanning = (st->mode == L"scanning");
    const bool confirmed  = st->confirmed;

    const Color accent = AccentColor(*st);

    // ── 1. Background ────────────────────────────────────────────────
    {
        GraphicsPath bg;
        RoundedRect(bg, 0.0f, 0.0f, W, H, S(CORNER_R, sc));
        SolidBrush bgBr(DS::BG());
        g.FillPath(&bgBr, &bg);
        Pen borderPen(DS::BORDER(), 1.0f);
        g.DrawPath(&borderPen, &bg);
    }

    // ── 2. Left accent bar (4px) ─────────────────────────────────────
    {
        // Clip to rounded-rect shape before painting bar
        GraphicsPath barClip;
        RoundedRect(barClip, 0.0f, 0.0f, W, H, S(CORNER_R, sc));
        g.SetClip(&barClip);
        SolidBrush accentBr(accent);
        g.FillRectangle(&accentBr, 0.0f, 0.0f, S(4.0f, sc), H);
        g.ResetClip();
    }

    // ── 3. Brand label "RISKNOX" (top left, uppercase, muted) ────────
    {
        FontFamily ff(L"Segoe UI");
        Font f(&ff, S(7.0f, sc), FontStyleBold, UnitPixel);
        DrawText(g, L"RISKNOX", f, DS::TEXT_MUTED(),
                 S(13.0f, sc), S(4.0f, sc), S(80.0f, sc), S(10.0f, sc));
    }

    // ── 4. Close button (top right, 20×14) ───────────────────────────
    {
        if (st->hvClose) {
            SolidBrush hov(DS::BTN_HOVER_CLOSE());
            g.FillRectangle(&hov, S(338.0f,sc), S(3.0f,sc), S(20.0f,sc), S(14.0f,sc));
        }
        FontFamily ff(L"Segoe UI");
        Font f(&ff, S(8.5f, sc), FontStyleRegular, UnitPixel);
        DrawText(g, L"\u00D7", f,
                 st->hvClose ? DS::TEXT_PRIMARY() : DS::TEXT_MUTED(),
                 S(338.0f,sc), S(3.0f,sc), S(20.0f,sc), S(14.0f,sc),
                 StringAlignmentCenter);
    }

    // ── 5. Icon tile (32×32, centred in content zone) ─────────────────
    //  Content zone: y=17..83, centre=50. Tile 32px → y=34
    {
        float tx = S(10.0f, sc), ty = S(26.0f, sc), ts = S(32.0f, sc);
        GraphicsPath tile;
        RoundedRect(tile, tx, ty, ts, ts, S(5.0f, sc));
        SolidBrush tileBr(TileColor(*st));
        g.FillPath(&tileBr, &tile);
        Pen tilePen(DS::BORDER(), 1.0f);
        g.DrawPath(&tilePen, &tile);

        float cx = tx + ts * 0.5f, cy = ty + ts * 0.5f;
        if (confirmed || isSafe)  DrawCheckIcon(g, cx, cy, sc, DS::TEXT_PRIMARY());
        else if (isScanning)      DrawScanIcon (g, cx, cy, sc, DS::TEXT_PRIMARY());
        else                      DrawShieldIcon(g, cx, cy, sc, DS::TEXT_PRIMARY());
    }

    // ── 6. Text content ──────────────────────────────────────────────
    {
        FontFamily ff(L"Segoe UI");
        // Text column: x=50, width shrinks to leave room for buttons (threat) or full width (other)
        const float TX = S(50.0f, sc);
        const float TW = isThreat ? S(196.0f, sc) : S(296.0f, sc);

        // Row 1: Heading (bold white, 10px)
        {
            Font boldF(&ff, S(10.0f, sc), FontStyleBold, UnitPixel);
            std::wstring heading;
            if (confirmed)       heading = L"Quarantined \u2014 " + st->fileName;
            else if (isSafe)     heading = L"Scan Complete \u2014 " + st->fileName;
            else if (isScanning) heading = L"Scanning \u2014 " + st->fileName;
            else                 heading = L"Threat Detected \u2014 " + st->fileName;
            DrawText(g, heading, boldF, DS::TEXT_PRIMARY(),
                     TX, S(18.0f, sc), TW, S(14.0f, sc));
        }

        // Row 2: Threat name / status (9px) + severity badge
        {
            Font regF(&ff, S(9.0f, sc), FontStyleRegular, UnitPixel);
            if (isScanning) {
                DrawText(g, ScanDots(st->scanTick), regF, DS::SCAN(),
                         TX, S(35.0f, sc), TW, S(12.0f, sc));
            } else if (confirmed) {
                DrawText(g, L"Threat removed from your system",
                         regF, DS::TEXT_SECONDARY(),
                         TX, S(35.0f, sc), TW, S(12.0f, sc));
            } else if (isSafe) {
                std::wstring summary = st->scanStatus.empty()
                                       ? L"No threats detected" : st->scanStatus;
                DrawText(g, summary, regF, DS::TEXT_SECONDARY(),
                         TX, S(35.0f, sc), TW, S(12.0f, sc));
            } else {
                // Threat name (truncated to make room for badge)
                DrawText(g, st->threatName, regF, DS::TEXT_SECONDARY(),
                         TX, S(35.0f, sc), S(148.0f, sc), S(12.0f, sc));

                // Severity badge — small inline pill
                {
                    std::wstring sevText = SevLabel(*st);
                    Font badgeF(&ff, S(6.5f, sc), FontStyleBold, UnitPixel);
                    RectF layout(0,0, S(50.0f,sc), S(10.0f,sc));
                    RectF measured;
                    StringFormat sf;
                    sf.SetTrimming(StringTrimmingNone);
                    sf.SetFormatFlags(StringFormatFlagsNoWrap);
                    g.MeasureString(sevText.c_str(), -1, &badgeF, layout, &sf, &measured);
                    float bw = measured.Width + S(6.0f, sc);
                    float bh = S(10.0f, sc);
                    float bx = TX + S(152.0f, sc);
                    float by = S(35.5f, sc);

                    Color badgeBg(70, accent.GetR(), accent.GetG(), accent.GetB());
                    GraphicsPath badgePath;
                    RoundedRect(badgePath, bx, by, bw, bh, S(2.0f, sc));
                    SolidBrush badgeBgBr(badgeBg);
                    g.FillPath(&badgeBgBr, &badgePath);
                    Pen badgePen(accent, 1.0f);
                    g.DrawPath(&badgePen, &badgePath);
                    DrawText(g, sevText, badgeF, accent,
                             bx, by, bw, bh, StringAlignmentCenter);
                }
            }
        }

        // Row 3: File path / status line (8px, dim)
        {
            Font pathF(&ff, S(8.0f, sc), FontStyleRegular, UnitPixel);
            std::wstring pathText;
            if (confirmed || isSafe)  pathText = L"";
            else if (isScanning) {
                pathText = st->scanStatus.empty()
                           ? L"Running virus scan, please wait..."
                           : st->scanStatus;
            } else {
                pathText = st->filePath;
            }
            if (!pathText.empty()) {
                DrawPathText(g, pathText, pathF, DS::TEXT_MUTED(),
                             TX, S(50.0f, sc), TW, S(11.0f, sc));
            }
        }
    }

    // ── 7. Action buttons (threat mode, not confirmed) ───────────────
    if (isThreat && !confirmed) {
        FontFamily ff(L"Segoe UI");
        const float BX = S(254.0f, sc);   // button zone start

        // Quarantine button — solid fill pill, 98×24
        {
            float bx = BX, by = S(20.0f, sc), bw = S(98.0f, sc), bh = S(24.0f, sc);
            Color btnCol = st->hvQuar ? DS::BTN_HOVER_QUAR() : accent;
            GraphicsPath pill;
            RoundedRect(pill, bx, by, bw, bh, S(4.0f, sc));
            SolidBrush btnBr(btnCol);
            g.FillPath(&btnBr, &pill);
            Font btnF(&ff, S(9.0f, sc), FontStyleBold, UnitPixel);
            DrawText(g, L"Quarantine", btnF, DS::TEXT_PRIMARY(),
                     bx, by, bw, bh, StringAlignmentCenter);
        }

        // Ignore · Details text links
        {
            Font lnkF(&ff, S(8.5f, sc), FontStyleRegular, UnitPixel);
            float ly = S(52.0f, sc), lh = S(12.0f, sc);

            DrawText(g, L"Ignore", lnkF,
                     st->hvIgn ? DS::LINK_HOVER() : DS::TEXT_MUTED(),
                     BX, ly, S(34.0f,sc), lh, StringAlignmentNear);

            DrawText(g, L"\u00B7", lnkF, DS::TEXT_DIM(),
                     BX + S(34.0f,sc), ly, S(9.0f,sc), lh, StringAlignmentCenter);

            DrawText(g, L"Details", lnkF,
                     st->hvDet ? DS::LINK_HOVER() : DS::TEXT_MUTED(),
                     BX + S(43.0f,sc), ly, S(40.0f,sc), lh, StringAlignmentNear);
        }
    }

    // ── 8. Undo link (confirmed state) ───────────────────────────────
    if (confirmed) {
        FontFamily ff(L"Segoe UI");
        Font f(&ff, S(9.0f, sc), FontStyleRegular, UnitPixel);
        DrawText(g, L"Undo", f,
                 st->hvUndo ? DS::LINK_HOVER() : DS::TEXT_MUTED(),
                 S(267.0f,sc), S(28.0f,sc), S(60.0f,sc), S(18.0f,sc),
                 StringAlignmentCenter);
    }

    // ── 9. Bottom bar (countdown / scanning pulse) ───────────────────
    {
        float bx = S(4.0f,sc), by = H - S(2.5f,sc), bw = W - S(8.0f,sc), bh = S(2.0f,sc);

        // Track
        SolidBrush trackBr(DS::BORDER());
        g.FillRectangle(&trackBr, bx, by, bw, bh);

        if (isScanning) {
            // Smooth animated pulse using wall-clock time
            ULONGLONG ms = GetTickCount64();
            double t  = (double)(ms % 3200) / 3200.0;
            // Ease in-out sine wave
            float pulse = (float)(0.5 - 0.5 * cos(t * 2.0 * 3.14159265));
            float fw  = S(80.0f,sc) + (bw - S(80.0f,sc)) * pulse;
            SolidBrush fillBr(DS::SCAN());
            g.FillRectangle(&fillBr, bx, by, fw, bh);
        } else {
            float fw; Color fc;
            if (confirmed || st->cntTotal <= 0) {
                fw = bw;
                fc = (confirmed || isSafe) ? DS::SAFE() : DS::TEXT_DIM();
            } else {
                double prog = (double)st->cntTick / st->cntTotal;
                fw = bw * (float)(1.0 - prog);
                double secsLeft = (double)(st->cntTotal - st->cntTick) / 10.0;
                if      (secsLeft <= 2.0) fc = DS::CRITICAL();
                else if (secsLeft <= 4.0) fc = DS::MEDIUM();
                else                      fc = accent;
            }
            if (fw > 0.5f) {
                SolidBrush fillBr(fc);
                g.FillRectangle(&fillBr, bx, by, fw, bh);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Window helpers
// ─────────────────────────────────────────────────────────────────────────────

static void Repaint(State* st) {
    InvalidateRect(st->hwnd, nullptr, FALSE);
    UpdateWindow(st->hwnd);
}

static void SetAlpha(State* st, BYTE a) {
    SetLayeredWindowAttributes(st->hwnd, 0, a, LWA_ALPHA);
}

static void BeginSlideOut(State* st) {
    if (st->closing) return;
    st->closing   = true;
    KillTimer(st->hwnd, TID_COUNTDOWN);
    KillTimer(st->hwnd, TID_SCAN);
    KillTimer(st->hwnd, TID_CONFIRM);
    st->phase     = State::SLIDE_OUT;
    st->animTick  = 0;
    st->animTotal = 10;  // 10 × 16ms ≈ 160ms
    SetTimer(st->hwnd, TID_ANIM, 16, nullptr);
}

static void ShowConfirmation(State* st) {
    st->confirmed = true;
    st->exitCode  = EC_QUARANTINE;
    KillTimer(st->hwnd, TID_COUNTDOWN);
    SetTimer(st->hwnd, TID_CONFIRM, 2000, nullptr);
    Repaint(st);
}

// ─────────────────────────────────────────────────────────────────────────────
// Window procedure
// ─────────────────────────────────────────────────────────────────────────────

static State* g_st = nullptr;

// Inline helpers used inside WndProc
static bool isScanning(const State* st) { return st->mode == L"scanning"; }
static bool isSafe(const State* st)     { return st->mode == L"safe"; }

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* st = g_st;

    switch (msg) {

    // Double-buffered paint ────────────────────────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HDC mdc = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, st->W, st->H);
        auto old = (HBITMAP)SelectObject(mdc, bmp);
        {
            Graphics g(mdc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            g.SetCompositingQuality(CompositingQualityHighQuality);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            RenderFrame(g, st);
        }
        BitBlt(hdc, 0, 0, st->W, st->H, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, old);
        DeleteObject(bmp);
        DeleteDC(mdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    // Timers ──────────────────────────────────────────────────────────
    case WM_TIMER:
        switch (wp) {

        case TID_ANIM: {
            st->animTick++;
            double t = std::min(1.0, (double)st->animTick / st->animTotal);
            if (st->phase == State::SLIDE_IN) {
                double ease = EaseOutCubic(t);
                int y = (int)(st->offScreenY + (st->finalY - st->offScreenY) * ease);
                SetWindowPos(hwnd, nullptr, st->posX, y, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                SetAlpha(st, (BYTE)(ease * 255.0));
                if (t >= 1.0) {
                    KillTimer(hwnd, TID_ANIM);
                    st->phase = State::IDLE;
                    SetWindowPos(hwnd, nullptr, st->posX, st->finalY, 0, 0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    SetAlpha(st, 255);
                    PlaySoundW(L"SystemHand", nullptr, SND_ALIAS|SND_ASYNC|SND_NODEFAULT);
                    if (isScanning(st))                   SetTimer(hwnd, TID_SCAN,      100, nullptr);
                    else if (st->cntTotal > 0) SetTimer(hwnd, TID_COUNTDOWN, 100, nullptr);
                }
            } else if (st->phase == State::SLIDE_OUT) {
                double ease = EaseInQuad(t);
                int y = (int)(st->finalY + (st->offScreenY - st->finalY) * ease);
                SetWindowPos(hwnd, nullptr, st->posX, y, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                SetAlpha(st, (BYTE)((1.0 - ease) * 255.0));
                if (t >= 1.0) {
                    KillTimer(hwnd, TID_ANIM);
                    DestroyWindow(hwnd);
                }
            }
            break;
        }

        case TID_COUNTDOWN:
            st->cntTick++;
            if (st->cntTick >= st->cntTotal) {
                KillTimer(hwnd, TID_COUNTDOWN);
                st->exitCode = isSafe(st) ? EC_DISMISSED : EC_AUTO_QUARANTINE;
                BeginSlideOut(st);
            } else {
                Repaint(st);
            }
            break;

        case TID_SCAN:
            st->scanTick++;
            if (st->scanTick % 5 == 0) PollScanLog(st);
            Repaint(st);
            break;

        case TID_CONFIRM:
            KillTimer(hwnd, TID_CONFIRM);
            BeginSlideOut(st);
            break;
        }
        return 0;

    // Mouse ───────────────────────────────────────────────────────────
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);

        // Close button — always active (x=338..358, y=3..17)
        if (Hit(st, x, y, 338, 3, 358, 17)) {
            st->exitCode = EC_DISMISSED;
            BeginSlideOut(st);
            return 0;
        }
        if (st->closing) return 0;

        if (st->mode == L"threat" && !st->confirmed) {
            if      (Hit(st, x, y, 254, 20, 352, 44)) ShowConfirmation(st);           // Quarantine btn
            else if (Hit(st, x, y, 254, 50, 292, 64)) { st->exitCode = EC_IGNORE;  BeginSlideOut(st); }  // Ignore
            else if (Hit(st, x, y, 300, 50, 354, 64)) { st->exitCode = EC_DETAILS; BeginSlideOut(st); }  // Details
        } else if (st->confirmed) {
            if (Hit(st, x, y, 267, 28, 327, 46)) {   // Undo
                st->confirmed = false;
                KillTimer(hwnd, TID_CONFIRM);
                st->exitCode = EC_DISMISSED;
                if (st->cntTotal > 0) SetTimer(hwnd, TID_COUNTDOWN, 100, nullptr);
                Repaint(st);
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        bool hc = Hit(st,x,y,338,3,358,17);
        bool hq = Hit(st,x,y,254,20,352,44);
        bool hi = Hit(st,x,y,254,50,292,64);
        bool hd = Hit(st,x,y,300,50,354,64);
        bool hu = Hit(st,x,y,267,28,327,46);
        if (hc!=st->hvClose||hq!=st->hvQuar||hi!=st->hvIgn||hd!=st->hvDet||hu!=st->hvUndo) {
            st->hvClose=hc; st->hvQuar=hq; st->hvIgn=hi; st->hvDet=hd; st->hvUndo=hu;
            bool anyCursor = hc||hq||hi||hd||hu;
            SetCursor(LoadCursor(nullptr, anyCursor ? IDC_HAND : IDC_ARROW));
            Repaint(st);
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        st->hvClose=st->hvQuar=st->hvIgn=st->hvDet=st->hvUndo=false;
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        Repaint(st);
        return 0;

    // HTCLIENT everywhere — window is pinned in corner, no dragging
    case WM_NCHITTEST:
        return HTCLIENT;

    case WM_DESTROY:
        PostQuitMessage(st->exitCode);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CLI parsing
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring NextArg(const std::vector<std::wstring>& a, int& i) {
    return (++i < (int)a.size()) ? a[i] : L"";
}

static void ParseArgs(State* st, int argc, wchar_t* argv[]) {
    std::vector<std::wstring> args(argv, argv + argc);
    for (int i = 1; i < (int)args.size(); ++i) {
        const auto& a = args[i];
        if      (a == L"--file")     st->fileName   = NextArg(args, i);
        else if (a == L"--threat")   st->threatName  = NextArg(args, i);
        else if (a == L"--path")     st->filePath    = NextArg(args, i);
        else if (a == L"--severity") st->severity    = NextArg(args, i);
        else if (a == L"--mode")     st->mode        = NextArg(args, i);
        else if (a == L"--slot")     st->slotIndex   = _wtoi(NextArg(args, i).c_str());
        else if (a == L"--timeout")  st->timeoutSecs = _wtoi(NextArg(args, i).c_str());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // Init GDI+
    ULONG_PTR gdipToken;
    GdiplusStartupInput gdipIn;
    if (GdiplusStartup(&gdipToken, &gdipIn, nullptr) != Ok) return EC_DISMISSED;

    // Parse CLI
    int argc;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    State st;
    ParseArgs(&st, argc, argv);
    LocalFree(argv);

    // Log path for safe/scanning mode
    wchar_t appData[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, appData);
    st.logPath = std::wstring(appData) + L"\\Risknox Pulse\\antivirus\\clamscan.log";
    if (st.mode == L"safe") st.scanStatus = ReadSafeSummary(st.logPath);

    // DPI awareness (dynamic load — works with older MinGW headers)
    {
        auto fn = (BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT))
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
        if (fn) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    // Measure DPI
    {
        HDC dc = GetDC(nullptr);
        int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
        st.sc = (float)dpi / 96.0f;
    }
    st.W = (int)(BASE_W * st.sc);
    st.H = (int)(BASE_H * st.sc);

    // Position (bottom-right corner, stacked by slot)
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    st.posX      = wa.right  - st.W - EDGE_MARGIN;
    st.finalY    = wa.bottom - st.H - EDGE_MARGIN - st.slotIndex * (st.H + SLOT_GAP);
    st.offScreenY= wa.bottom + st.H + 4;

    // Countdown
    st.cntTotal = (st.timeoutSecs > 0 && st.mode != L"scanning")
                      ? st.timeoutSecs * 10 : 0;

    // Register window class
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"RisknoxNotifyV2";
    RegisterClassExW(&wc);

    // Set window title based on mode so parent process can target scanning popups selectively
    std::wstring winTitle = L"Risknox";
    if (st.mode == L"scanning")     winTitle = L"Risknox Scanning";
    else if (st.mode == L"threat")   winTitle = L"Risknox Threat";
    else if (st.mode == L"safe")     winTitle = L"Risknox Safe";

    // Create window
    g_st = &st;
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"RisknoxNotifyV2",
        winTitle.c_str(),
        WS_POPUP,
        st.posX, st.offScreenY, st.W, st.H,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) { GdiplusShutdown(gdipToken); return EC_DISMISSED; }
    st.hwnd = hwnd;

    // Rounded window region
    {
        int r = (int)(CORNER_R * st.sc);
        HRGN rgn = CreateRoundRectRgn(0, 0, st.W + 1, st.H + 1, r * 2, r * 2);
        SetWindowRgn(hwnd, rgn, FALSE);
    }

    // Windows 11: ask DWM for rounded corners
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
    { int pref = 2; DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref); }

    // Start fully transparent; slide-in animation will fade it in
    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    SetTimer(hwnd, TID_ANIM, 16, nullptr);

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(gdipToken);
    return st.exitCode;
}
