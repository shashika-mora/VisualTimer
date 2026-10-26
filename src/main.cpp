// Visual Timer - a tiny native Win32/GDI+ visual countdown timer designed to
// make elapsed and remaining time *visible* (helpful for ADHD "time blindness").
// Single static executable, no installer, no runtime dependencies.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using std::max;
using std::min;
#include <gdiplus.h>
using namespace Gdiplus;

// ---------------------------------------------------------------- constants
static const wchar_t* kClass = L"VisualTimerWnd";
static const wchar_t* kTitle = L"Visual Timer";
static const UINT_PTR kTimerId = 1;   // 10 Hz UI/clock tick
static const UINT_PTR kRepeatId = 2;  // auto-repeat for the custom -/+ buttons
static const UINT WM_TRAY = WM_APP + 1;
// Normal window: caption + close + resizable border, no minimize/maximize buttons.
static const DWORD kNormalStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
static const int kFocusSide = 320;  // clock-only window size at 96 DPI
static const double kPi = 3.14159265358979323846;

enum Id {
    ID_START = 1, ID_RESET, ID_TOP, ID_THEME, ID_SOUND, ID_NOTIFY, ID_NUDGE, ID_SCALE, ID_COLOR,
    ID_CUST_DEC, ID_CUST, ID_CUST_INC,
    ID_PRESET0 = 100,
    // tray menu only
    CMD_SHOW = 200, CMD_QUIT
};

static const int kPresets[] = {5, 10, 15, 25, 30, 45, 60};
static const int kPresetCount = 7;
static const int kNudges[] = {0, 5, 10, 15, 30};
static const int kNudgeCount = 5;
static const Color kAccents[] = {Color(255, 239, 68, 68),  Color(255, 249, 115, 22),
                                 Color(255, 59, 130, 246), Color(255, 34, 197, 94),
                                 Color(255, 168, 85, 247), Color(255, 236, 72, 153)};
static const int kAccentCount = 6;

// -------------------------------------------------------------------- state
struct State {
    // timer
    int totalSec = 25 * 60;
    bool running = false, started = false;
    ULONGLONG startTick = 0;
    double accumMs = 0;
    bool w5 = false, w1 = false, done = false;
    double nextOver = 0;
    long nudgeCount = 0;
    // settings
    bool topmost = true, dark = true, sound = true, notify = true, prop = false;
    int nudgeIdx = 0, colorIdx = 0;
    int custom = 50;  // custom preset, 1..60 minutes
    // ui
    bool dragging = false;  // dragging the dial to set the time
    bool focus = false;     // borderless "clock only" mode while the timer runs
    bool mouseIn = false;   // cursor currently over the window
    int hover = 0;          // id of hovered button
    int repeatId = 0;       // custom -/+ button being held
    long lastShownSec = -999999;
} S;

static HWND g_hwnd;
static HICON g_icon, g_iconSmall;
static NOTIFYICONDATAW g_nid;
static ULONG_PTR g_gdipToken;
static std::wstring g_ini;
static RECT g_savedRect;     // normal window rect saved while in focus mode
static double g_dpi = 1.0;   // system DPI scale (1.0 = 96 DPI)
static UINT g_msgTaskbarCreated;

// ------------------------------------------------------------------ helpers
static int dpx(int v) { return (int)std::lround(v * g_dpi); }

static double elapsedSec() {
    double ms = S.accumMs + (S.running ? (double)(GetTickCount64() - S.startTick) : 0.0);
    return ms / 1000.0;
}
static double remainingSec() { return S.totalSec - elapsedSec(); }

static std::wstring fmt(long sec, bool plus = false) {
    if (sec < 0) sec = 0;
    wchar_t b[32];
    long h = sec / 3600, m = (sec / 60) % 60, s = sec % 60;
    if (h > 0)
        swprintf(b, 32, L"%s%ld:%02ld:%02ld", plus ? L"+" : L"", h, m, s);
    else
        swprintf(b, 32, L"%s%02ld:%02ld", plus ? L"+" : L"", m, s);
    return b;
}
static std::wstring minutesText(long sec) {
    wchar_t b[32];
    swprintf(b, 32, L"%ld min", sec / 60);
    return b;
}

// ------------------------------------------------------------------- sounds
// Simple synthesized beeps on a worker thread so the UI never blocks.
static volatile LONG g_busy = 0;
static DWORD WINAPI soundThread(LPVOID p) {
    switch ((int)(INT_PTR)p) {
        case 0:  // gentle check-in
            Beep(784, 110); Beep(988, 160);
            break;
        case 1:  // heads-up (5 / 1 minute left)
            Beep(880, 140); Sleep(60); Beep(880, 140);
            break;
        case 2:  // time is up
            for (int i = 0; i < 3; i++) {
                Beep(880, 150); Beep(1109, 150); Beep(1319, 260);
                Sleep(250);
            }
            break;
        case 3:  // overtime reminder
            Beep(660, 200); Beep(523, 320);
            break;
    }
    InterlockedExchange(&g_busy, 0);
    return 0;
}
static void playSound(int kind) {
    if (!S.sound) return;
    if (InterlockedExchange(&g_busy, 1)) return;
    HANDLE h = CreateThread(nullptr, 0, soundThread, (LPVOID)(INT_PTR)kind, 0, nullptr);
    if (h) CloseHandle(h); else InterlockedExchange(&g_busy, 0);
}

// ------------------------------------------------------------ notifications
static void addTrayIcon() { Shell_NotifyIconW(NIM_ADD, &g_nid); }
static void balloon(const std::wstring& title, const std::wstring& text) {
    if (!S.notify) return;
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;  // we play our own sound
    wcsncpy(n.szInfoTitle, title.c_str(), 63);
    n.szInfoTitle[63] = 0;
    wcsncpy(n.szInfo, text.c_str(), 255);
    n.szInfo[255] = 0;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}
// Make the window visible again (without stealing keyboard focus) and flash it.
static void bringToFrontNoFocus() {
    if (!IsWindowVisible(g_hwnd) || IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (!S.topmost)
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    FLASHWINFO f = {sizeof(f), g_hwnd, FLASHW_ALL | FLASHW_TIMERNOFG, 6, 0};
    FlashWindowEx(&f);
}

// ------------------------------------------------------------- timer logic
// Re-derive which alerts have already fired (after start/adjust) so changing
// the time never triggers a burst of stale notifications.
static void resync() {
    double el = elapsedSec(), rem = S.totalSec - el;
    S.w5 = rem <= 300;
    S.w1 = rem <= 60;
    S.done = rem <= 0;
    S.nextOver = S.done ? el + 120 : 0;
    int n = kNudges[S.nudgeIdx];
    S.nudgeCount = n ? (long)(el / (n * 60.0)) : 0;
}
static void startPause() {
    if (S.running) {
        S.accumMs += (double)(GetTickCount64() - S.startTick);
        S.running = false;
    } else {
        S.startTick = GetTickCount64();
        S.running = true;
        S.started = true;
        resync();
    }
}
static void resetTimer() {
    S.running = false;
    S.started = false;
    S.accumMs = 0;
    resync();
}
static void adjust(int deltaSec) {
    S.totalSec = std::clamp(S.totalSec + deltaSec, 60, 180 * 60);
    resync();
}
static void setTotalAndStart(int minutes) {
    resetTimer();
    S.totalSec = minutes * 60;
    startPause();
}
static void adjustCustom(int delta) {
    S.custom = std::clamp(S.custom + delta, 1, 60);
    if (!S.started) {  // preview the custom value on the dial while idle
        S.totalSec = S.custom * 60;
        resync();
    }
}

static void tick() {
    if (!S.running) return;
    double el = elapsedSec(), rem = S.totalSec - el;

    if (!S.done) {
        if (rem <= 0) {
            S.done = true;
            S.w1 = S.w5 = true;
            S.nextOver = el + 120;
            playSound(2);
            bringToFrontNoFocus();
            balloon(L"Time's up!", L"You planned " + minutesText(S.totalSec) +
                                       L". Wrap up, or choose to keep going on purpose.");
        } else if (!S.w5 && rem <= 300 && S.totalSec > 300) {
            S.w5 = true;
            playSound(1);
            balloon(L"5 minutes left", L"Start winding down - finish the current step.");
        } else if (!S.w1 && rem <= 60 && S.totalSec > 120) {
            S.w1 = true;
            playSound(1);
            balloon(L"1 minute left", L"Save your work and prepare to switch.");
        }
        int n = kNudges[S.nudgeIdx];
        if (!S.done && n) {
            long c = (long)(el / (n * 60.0));
            if (c > S.nudgeCount) {
                S.nudgeCount = c;
                playSound(0);
                balloon(L"Time check-in",
                        minutesText((long)(c * n * 60)) + L" elapsed, " +
                            minutesText((long)std::ceil(rem / 60.0) * 60) + L" left.");
            }
        }
    } else if (el >= S.nextOver) {
        S.nextOver += 120;
        playSound(3);
        balloon(L"Overtime", L"You're " + fmt((long)(-rem)) + L" past your planned time.");
    }
}

// ----------------------------------------------------------------- UI model
struct Btn {
    RectF r;
    int id;
    std::wstring label;
    bool on;      // highlighted (toggle on / selected preset)
    bool swatch;  // draws the accent colour dot
};
struct Ui {
    float u;  // layout scale factor
    RectF digits, status, hint;
    float cx, cy, R;  // dial centre + radius
    std::vector<Btn> b;
} U;

static void buildUi(int W, int H) {
    U.b.clear();
    if (S.focus) {
        float fu = W / 300.f;
        U.u = fu;
        U.cx = W / 2.f;
        U.cy = H / 2.f;
        U.R = W / 2.f - 3 * fu;
        U.digits = RectF(0, U.cy - 52 * fu, (float)W, 62 * fu);
        U.status = RectF(0, U.cy + 8 * fu, (float)W, 20 * fu);
        U.hint = RectF(0, 0, 0, 0);
        if (S.mouseIn)
            U.b.push_back({RectF(U.cx - 60 * fu, U.cy + 36 * fu, 120 * fu, 38 * fu), ID_START, L"PAUSE", false, false});
        return;
    }
    float u = min(W / 400.f, H / 660.f);
    U.u = u;

    // presets + custom (custom block = 2 preset widths)
    float m = 16 * u, gap = 6 * u, rowY = 12 * u, rowH = 30 * u;
    float bw = (W - 2 * m - 8 * gap) / 9.f;
    for (int i = 0; i < kPresetCount; i++) {
        wchar_t t[8];
        swprintf(t, 8, L"%d", kPresets[i]);
        U.b.push_back({RectF(m + i * (bw + gap), rowY, bw, rowH), ID_PRESET0 + i, t,
                       S.totalSec == kPresets[i] * 60, false});
    }
    float c0 = m + kPresetCount * (bw + gap), cw = W - m - c0, sb = cw * 0.27f, ig = 2 * u;
    wchar_t ct[8];
    swprintf(ct, 8, L"%d", S.custom);
    U.b.push_back({RectF(c0, rowY, sb, rowH), ID_CUST_DEC, L"\u2212", false, false});
    U.b.push_back({RectF(c0 + sb + ig, rowY, cw - 2 * (sb + ig), rowH), ID_CUST, ct,
                   S.totalSec == S.custom * 60, false});
    U.b.push_back({RectF(c0 + cw - sb, rowY, sb, rowH), ID_CUST_INC, L"+", false, false});

    U.digits = RectF(0, 46 * u, (float)W, 84 * u);
    U.status = RectF(0, 130 * u, (float)W, 22 * u);

    // bottom block
    float hintY = H - 22 * u;
    U.hint = RectF(0, hintY, (float)W, 18 * u);
    float row2 = hintY - 6 * u - 28 * u;
    float row1 = row2 - 6 * u - 28 * u;
    float ctl = row1 - 12 * u - 48 * u;
    float bwc = 140 * u, cg = 12 * u;
    float x0 = (W - (2 * bwc + cg)) / 2;
    U.b.push_back({RectF(x0, ctl, bwc, 48 * u), ID_START,
                   S.running ? L"PAUSE" : (S.started ? L"RESUME" : L"START"), false, false});
    U.b.push_back({RectF(x0 + bwc + cg, ctl, bwc, 48 * u), ID_RESET, L"RESET", false, false});

    float tw = (W - 2 * m - 3 * gap) / 4.f;
    U.b.push_back({RectF(m, row1, tw, 28 * u), ID_TOP, L"On top", S.topmost, false});
    U.b.push_back({RectF(m + (tw + gap), row1, tw, 28 * u), ID_THEME,
                   S.dark ? L"Theme: Dark" : L"Theme: Light", false, false});
    U.b.push_back({RectF(m + 2 * (tw + gap), row1, tw, 28 * u), ID_SOUND, L"Sound", S.sound, false});
    U.b.push_back({RectF(m + 3 * (tw + gap), row1, tw, 28 * u), ID_NOTIFY, L"Notify", S.notify, false});
    float t3 = (W - 2 * m - 2 * gap) / 3.f;
    wchar_t nl[32];
    if (kNudges[S.nudgeIdx]) swprintf(nl, 32, L"Check-in: %dm", kNudges[S.nudgeIdx]);
    else swprintf(nl, 32, L"Check-in: off");
    U.b.push_back({RectF(m, row2, t3, 28 * u), ID_NUDGE, nl, kNudges[S.nudgeIdx] != 0, false});
    U.b.push_back({RectF(m + (t3 + gap), row2, t3, 28 * u), ID_SCALE,
                   S.prop ? L"Dial: fit" : L"Dial: 60 min", S.prop, false});
    U.b.push_back({RectF(m + 2 * (t3 + gap), row2, t3, 28 * u), ID_COLOR, L"Color", false, true});

    // dial fills the space between the status line and the controls
    float top = 156 * u, bottom = ctl - 8 * u;
    float D = min(W - 40 * u, bottom - top);
    U.cx = W / 2.f;
    U.cy = top + (bottom - top) / 2;
    U.R = max(10.f, D / 2 - 22 * u);
}

static int hitTest(float x, float y) {
    for (auto& b : U.b)
        if (b.r.Contains(x, y)) return b.id;
    return 0;
}

// ------------------------------------------------------------------ palette
struct Pal {
    Color bg, panel, hover, text, sub, face, tick, major;
};
static Pal palette() {
    if (S.dark)
        return {Color(255, 24, 24, 27),    Color(255, 42, 42, 48),    Color(255, 60, 60, 68),
                Color(255, 245, 245, 247), Color(255, 150, 150, 160), Color(255, 38, 38, 43),
                Color(255, 85, 85, 92),    Color(255, 150, 150, 158)};
    return {Color(255, 244, 244, 246), Color(255, 226, 226, 232), Color(255, 210, 210, 220),
            Color(255, 28, 28, 32),    Color(255, 105, 105, 115), Color(255, 255, 255, 255),
            Color(255, 170, 170, 178), Color(255, 80, 80, 88)};
}
static Color darker(Color c, int d) {
    return Color(255, max(0, c.GetR() - d), max(0, c.GetG() - d), max(0, c.GetB() - d));
}

static void roundRect(GraphicsPath& p, RectF r, float rad) {
    float d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

static void text(Graphics& g, const std::wstring& s, const wchar_t* fam, float px, bool bold,
                 Color c, RectF r) {
    FontFamily ff(fam);
    Font f(&ff, px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush br(c);
    g.DrawString(s.c_str(), -1, &f, r, &sf, &br);
}

static void drawButton(Graphics& g, const Btn& b, const Pal& p, Color accent) {
    float u = U.u;
    GraphicsPath path;
    roundRect(path, b.r, 8 * u);
    bool big = b.id == ID_START;
    bool toggle = b.id >= ID_TOP && b.id <= ID_COLOR && b.id != ID_THEME;
    Color fill = p.panel, fg = p.text;
    if (b.id == ID_START) {
        fill = S.running ? Color(255, 245, 158, 11) : Color(255, 34, 197, 94);
        fg = Color(255, 255, 255, 255);
        if (S.hover == b.id) fill = darker(fill, 18);
    } else if (b.on) {
        fill = accent;
        fg = Color(255, 255, 255, 255);
        if (S.hover == b.id) fill = darker(accent, 20);
    } else {
        fg = toggle ? p.sub : p.text;
        if (S.hover == b.id) fill = p.hover;
    }
    SolidBrush fb(fill);
    g.FillPath(&fb, &path);
    if (b.id == ID_CUST && !b.on) {  // outline marks the editable custom value
        Pen op(accent, max(1.f, 1.5f * u));
        g.DrawPath(&op, &path);
    }
    RectF tr = b.r;
    if (b.swatch) {
        SolidBrush sw(accent);
        float d = 14 * u;
        g.FillEllipse(&sw, b.r.X + 14 * u, b.r.Y + (b.r.Height - d) / 2, d, d);
        tr.X += 14 * u;
        tr.Width -= 14 * u;
    }
    float px = big ? 17.f : (b.id == ID_CUST_DEC || b.id == ID_CUST_INC) ? 16.f : 12.5f;
    bool bold = big || b.id == ID_RESET || b.id == ID_CUST;
    text(g, b.label, L"Segoe UI", px * u, bold, fg, tr);
}

static void paintAll(HDC hdc, int W, int H) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    Pal p = palette();
    Color accent = kAccents[S.colorIdx];
    Color amber(255, 245, 158, 11);
    g.Clear(p.bg);
    if (!S.focus) {
        Pen borderPen(p.panel, 1.f);
        g.DrawLine(&borderPen, 0, 0, W, 0);
    }
    buildUi(W, H);
    float u = U.u;

    double el = elapsedSec(), rem = S.totalSec - el;
    bool over = S.started && rem <= 0;

    // --- dial: remaining time is a coloured wedge that shrinks clockwise to 0
    float cx = U.cx, cy = U.cy, R = U.R;
    SolidBrush face(p.face);
    g.FillEllipse(&face, cx - R, cy - R, 2 * R, 2 * R);
    float scale = S.prop ? (float)S.totalSec : 3600.f;
    float wr = R - 16 * u;
    if (!over) {
        double frac = std::clamp(rem / scale, 0.0, 1.0);
        if (frac > 0) {
            SolidBrush wb(accent);
            if (frac >= 0.9999) g.FillEllipse(&wb, cx - wr, cy - wr, 2 * wr, 2 * wr);
            else g.FillPie(&wb, cx - wr, cy - wr, 2 * wr, 2 * wr, -90.f, (REAL)(360.0 * frac));
        }
    } else {  // overtime: amber wedge grows clockwise
        double ot = -rem;
        double frac = std::fmod(ot, (double)scale) / scale;
        SolidBrush wb(amber);
        if (ot > 0.5) g.FillPie(&wb, cx - wr, cy - wr, 2 * wr, 2 * wr, -90.f, (REAL)max(2.0, 360.0 * frac));
    }
    // ticks + minute labels
    Pen minor(p.tick, max(1.f, 1.2f * u)), major(p.major, max(2.f, 2.4f * u));
    for (int i = 0; i < 60; i++) {
        bool mj = i % 5 == 0;
        double a = (i * 6 - 90) * kPi / 180.0;
        float r1 = R - 2 * u, r2 = R - (mj ? 14 : 8) * u;
        g.DrawLine(mj ? &major : &minor, cx + (float)cos(a) * r1, cy + (float)sin(a) * r1,
                   cx + (float)cos(a) * r2, cy + (float)sin(a) * r2);
        if (mj && !S.prop && !S.focus) {
            float lr = R + 13 * u;
            wchar_t t[8];
            swprintf(t, 8, L"%d", i);
            text(g, t, L"Segoe UI", 13 * u, true, p.sub,
                 RectF(cx + (float)cos(a) * lr - 16 * u, cy + (float)sin(a) * lr - 10 * u, 32 * u, 20 * u));
        }
    }
        SolidBrush knob(p.bg);
    g.FillEllipse(&knob, cx - 7 * u, cy - 7 * u, 14 * u, 14 * u);
    if (S.focus) {
        Pen rim(p.panel, max(2.f, 2.5f * u));
        g.DrawEllipse(&rim, cx - R, cy - R, 2 * R, 2 * R);
    }

    // --- digits + status. In focus mode only on hover, or always once in overtime.
    if (!S.focus || S.mouseIn || over) {
        if (S.focus) {
            SolidBrush veil(Color(190, p.bg.GetR(), p.bg.GetG(), p.bg.GetB()));
            g.FillEllipse(&veil, cx - R, cy - R, 2 * R, 2 * R);
        }
        std::wstring big = over ? fmt((long)(-rem), true) : fmt((long)std::ceil(rem));
        text(g, big, L"Consolas", (S.focus ? 50 : 76) * u, true, over ? amber : p.text, U.digits);
        std::wstring st;
        Color stc = p.sub;
        if (!S.started) st = L"Ready: pick a time or drag the dial";
        else if (over) { st = L"OVERTIME - you planned " + fmt(S.totalSec); stc = amber; }
        else st = std::wstring(S.running ? L"Elapsed " : L"Paused - elapsed ") + fmt((long)el) + L" of " + fmt(S.totalSec);
        text(g, st, L"Segoe UI", (S.focus ? 12.f : 13.5f) * u, false, stc, U.status);
    }

    // --- buttons
    for (auto& b : U.b) drawButton(g, b, p, accent);
    if (!S.focus)
        text(g, L"Space start/pause \u00B7 R reset \u00B7 \u2191\u2193 \u00B11 min \u00B7 wheel/drag dial",
             L"Segoe UI", 11 * u, false, p.tick, U.hint);
}

// ------------------------------------------------------------ window state
static COLORREF toRef(Color c) { return RGB(c.GetR(), c.GetG(), c.GetB()); }

// Dark title bar + caption/border colours that match the app theme (instead of
// the system accent colour). Attributes that an older Windows build doesn't
// support are simply ignored.
static void applyTheme() {
    Pal p = palette();
    BOOL dark = S.dark;
    DwmSetWindowAttribute(g_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF cap = toRef(p.bg), txt = toRef(p.text), brd = toRef(p.panel);
    DwmSetWindowAttribute(g_hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
    DwmSetWindowAttribute(g_hwnd, 36 /*DWMWA_TEXT_COLOR*/, &txt, sizeof(txt));
    DwmSetWindowAttribute(g_hwnd, 34 /*DWMWA_BORDER_COLOR*/, &brd, sizeof(brd));
    int corner = S.focus ? 1 /*DWMWCP_DONOTROUND*/ : 2 /*DWMWCP_ROUND*/;
    DwmSetWindowAttribute(g_hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
    InvalidateRect(g_hwnd, nullptr, FALSE);
}
static void applyTopmost() {
    SetWindowPos(g_hwnd, S.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
static void updateTitle() {
    double rem = remainingSec();
    long sec = S.started ? (rem > 0 ? (long)std::ceil(rem) : -(long)(-rem)) : S.totalSec;
    if (sec == S.lastShownSec) return;
    S.lastShownSec = sec;
    std::wstring t = (S.started && rem <= 0) ? fmt(-sec, true) : fmt(sec);
    t += L" - ";
    t += kTitle;
    SetWindowTextW(g_hwnd, t.c_str());
    wcsncpy(g_nid.szTip, t.c_str(), 127);
    g_nid.szTip[127] = 0;
    NOTIFYICONDATAW n = g_nid;
    n.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}
static void showWindowNormal() {
    ShowWindow(g_hwnd, SW_SHOW);
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

// Running => borderless round clock-only window. Paused/idle => normal app window.
static void setFocus(bool on) {
    if (on == S.focus) return;
    DWORD vis = (DWORD)GetWindowLongPtrW(g_hwnd, GWL_STYLE) & WS_VISIBLE;  // keep hidden-to-tray state
    if (on) {
        if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
        GetWindowRect(g_hwnd, &g_savedRect);
        POINT c = {(LONG)U.cx, (LONG)U.cy};
        ClientToScreen(g_hwnd, &c);
        int side = dpx(kFocusSide);
        // keep the clock fully on the monitor it is on
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST), &mi);
        RECT wa = mi.rcWork;
        int x = std::clamp((int)c.x - side / 2, (int)wa.left, max((int)wa.left, (int)wa.right - side));
        int y = std::clamp((int)c.y - side / 2, (int)wa.top, max((int)wa.top, (int)wa.bottom - side));
        S.focus = true;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_POPUP | vis);
        SetWindowPos(g_hwnd, nullptr, x, y, side, side, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        SetWindowRgn(g_hwnd, CreateEllipticRgn(0, 0, side + 1, side + 1), TRUE);
        applyTheme();
    } else {
        S.focus = false;
        SetWindowRgn(g_hwnd, nullptr, TRUE);
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, kNormalStyle | vis);
        RECT r = g_savedRect;
        SetWindowPos(g_hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        applyTheme();
    }
    S.mouseIn = false;
    S.hover = 0;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void refresh() {
    updateTitle();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void doAction(int id) {
    if (id >= ID_PRESET0 && id < ID_PRESET0 + kPresetCount) {
        setTotalAndStart(kPresets[id - ID_PRESET0]);
    } else switch (id) {
        case ID_START: startPause(); break;
        case ID_RESET: resetTimer(); break;
        case ID_TOP: S.topmost = !S.topmost; applyTopmost(); break;
        case ID_THEME: S.dark = !S.dark; applyTheme(); break;
        case ID_SOUND: S.sound = !S.sound; if (S.sound) playSound(0); break;
        case ID_NOTIFY: S.notify = !S.notify; break;
        case ID_NUDGE: S.nudgeIdx = (S.nudgeIdx + 1) % kNudgeCount; resync(); break;
        case ID_SCALE: S.prop = !S.prop; break;
        case ID_COLOR: S.colorIdx = (S.colorIdx + 1) % kAccentCount; break;
        case ID_CUST: setTotalAndStart(S.custom); break;
        case ID_CUST_DEC: adjustCustom(GetKeyState(VK_SHIFT) < 0 ? -5 : -1); break;
        case ID_CUST_INC: adjustCustom(GetKeyState(VK_SHIFT) < 0 ? 5 : 1); break;
    }
    setFocus(S.running);
    refresh();
}

static void dragDial(int mx, int my) {
    double ang = std::atan2((double)my - U.cy, (double)mx - U.cx) * 180.0 / kPi + 90.0;
    if (ang < 0) ang += 360.0;
    int m = (int)std::lround(ang / 6.0);
    if (m < 1) m = 1;
    S.totalSec = m * 60;
    resync();
    refresh();
}
static bool inDial(int x, int y) {
    float dx = x - U.cx, dy = y - U.cy, r = U.R + 28 * U.u;
    return dx * dx + dy * dy <= r * r;
}

// ------------------------------------------------------------- persistence
// Settings live in VisualTimer.ini next to the exe (portable, no registry).
static void iniPath() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    size_t k = s.find_last_of(L"\\/");
    g_ini = s.substr(0, k + 1) + L"VisualTimer.ini";
}
static int geti(const wchar_t* k, int d) { return (int)GetPrivateProfileIntW(L"VisualTimer", k, d, g_ini.c_str()); }
static void puti(const wchar_t* k, int v) {
    wchar_t b[16];
    swprintf(b, 16, L"%d", v);
    WritePrivateProfileStringW(L"VisualTimer", k, b, g_ini.c_str());
}
static void loadSettings() {
    S.totalSec = std::clamp(geti(L"Minutes", 25), 1, 180) * 60;
    S.custom = std::clamp(geti(L"Custom", 50), 1, 60);
    S.topmost = geti(L"OnTop", 1) != 0;
    S.dark = geti(L"Dark", 1) != 0;
    S.sound = geti(L"Sound", 1) != 0;
    S.notify = geti(L"Notify", 1) != 0;
    S.prop = geti(L"Fit", 0) != 0;
    S.nudgeIdx = std::clamp(geti(L"CheckIn", 0), 0, kNudgeCount - 1);
    S.colorIdx = std::clamp(geti(L"Color", 0), 0, kAccentCount - 1);
}
static void saveSettings() {
    puti(L"Minutes", S.totalSec / 60);
    puti(L"Custom", S.custom);
    puti(L"OnTop", S.topmost);
    puti(L"Dark", S.dark);
    puti(L"Sound", S.sound);
    puti(L"Notify", S.notify);
    puti(L"Fit", S.prop);
    puti(L"CheckIn", S.nudgeIdx);
    puti(L"Color", S.colorIdx);
    WINDOWPLACEMENT wp = {sizeof(wp)};
    if (GetWindowPlacement(g_hwnd, &wp)) {
        RECT r = S.focus ? g_savedRect : wp.rcNormalPosition;
        puti(L"X", r.left);
        puti(L"Y", r.top);
        puti(L"W", r.right - r.left);
        puti(L"H", r.bottom - r.top);
    }
}

// --------------------------------------------------------------- icon + tray
// Fallback icon drawn at runtime if the embedded resource is missing.
static HICON makeIcon(int sz) {
    Bitmap bmp(sz, sz, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0, 0, 0, 0));
    SolidBrush face(Color(255, 245, 245, 247)), rim(Color(255, 70, 70, 80)), red(Color(255, 239, 68, 68));
    g.FillEllipse(&rim, 0.f, 0.f, (REAL)sz, (REAL)sz);
    float m = sz * 0.07f;
    g.FillEllipse(&face, m, m, sz - 2 * m, sz - 2 * m);
    float w = sz * 0.34f * 2;
    g.FillPie(&red, sz / 2 - w / 2, sz / 2 - w / 2, w, w, -90.f, -250.f);
    HICON h = nullptr;
    bmp.GetHICON(&h);
    return h;
}
static HICON loadAppIcon(HINSTANCE hi, int sz) {
    HICON h = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, sz, sz, LR_DEFAULTCOLOR);
    return h ? h : makeIcon(sz);
}

static void showMenu() {
    bool shown = IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, CMD_SHOW, shown ? L"Hide" : L"Show");
    AppendMenuW(m, MF_STRING, ID_START, S.running ? L"Pause" : (S.started ? L"Resume" : L"Start"));
    AppendMenuW(m, MF_STRING | (S.started ? 0 : MF_GRAYED), ID_RESET, L"Reset");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (S.topmost ? MF_CHECKED : 0), ID_TOP, L"Always on top");
    AppendMenuW(m, MF_STRING | (S.sound ? MF_CHECKED : 0), ID_SOUND, L"Sound");
    AppendMenuW(m, MF_STRING | (S.notify ? MF_CHECKED : 0), ID_NOTIFY, L"Notifications");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, CMD_QUIT, L"Quit");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    int c = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (c == CMD_SHOW) {
        if (shown) ShowWindow(g_hwnd, SW_HIDE);
        else showWindowNormal();
    } else if (c == CMD_QUIT) DestroyWindow(g_hwnd);
    else if (c) doAction(c);
}

// ---------------------------------------------------------------- wndproc
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = h;
            memset(&g_nid, 0, sizeof(g_nid));
            g_nid.cbSize = sizeof(g_nid);
            g_nid.hWnd = h;
            g_nid.uID = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_TRAY;
            g_nid.hIcon = g_iconSmall;
            wcscpy(g_nid.szTip, kTitle);
            addTrayIcon();
            applyTheme();
            applyTopmost();
            SetTimer(h, kTimerId, 100, nullptr);
            resync();
            updateTitle();
            return 0;
        }
        case WM_TIMER:
            if (wp == kRepeatId) {  // auto-repeat while holding custom -/+
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(h, &pt);
                if (S.repeatId && hitTest((float)pt.x, (float)pt.y) == S.repeatId) doAction(S.repeatId);
                SetTimer(h, kRepeatId, 70, nullptr);
                return 0;
            }
            tick();
            updateTitle();
            if (S.focus) {  // poll hover: client mouse messages stop while dragging the window
                POINT pt;
                RECT wr;
                GetCursorPos(&pt);
                GetWindowRect(h, &wr);
                bool in = PtInRect(&wr, pt) != 0;
                if (in != S.mouseIn) {
                    S.mouseIn = in;
                    InvalidateRect(h, nullptr, FALSE);
                }
            }
            if (S.running) InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {  // double-buffered GDI+ painting
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            int W = rc.right, H = rc.bottom;
            if (W > 0 && H > 0) {
                HDC m = CreateCompatibleDC(hdc);
                HBITMAP bm = CreateCompatibleBitmap(hdc, W, H);
                HGDIOBJ old = SelectObject(m, bm);
                paintAll(m, W, H);
                BitBlt(hdc, 0, 0, W, H, m, 0, 0, SRCCOPY);
                SelectObject(m, old);
                DeleteObject(bm);
                DeleteDC(m);
            }
            EndPaint(h, &ps);
            return 0;
        }
        case WM_SIZE: InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_GETMINMAXINFO: {
            if (S.focus) break;  // the round clock has its own fixed size
            RECT r = {0, 0, dpx(340), dpx(560)};
            AdjustWindowRect(&r, kNormalStyle, FALSE);
            ((MINMAXINFO*)lp)->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(h, &pt);
                bool hand = hitTest((float)pt.x, (float)pt.y) || (!S.started && !S.focus && inDial(pt.x, pt.y));
                SetCursor(LoadCursor(nullptr, hand ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_MOUSEMOVE: {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            if (S.dragging) { dragDial(x, y); return 0; }
            bool wasIn = S.mouseIn;
            S.mouseIn = true;
            int hv = hitTest((float)x, (float)y);
            if (hv != S.hover || !wasIn) {
                S.hover = hv;
                InvalidateRect(h, nullptr, FALSE);
            }
            TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            return 0;
        }
        case WM_MOUSELEAVE:
            S.hover = 0;
            S.mouseIn = false;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            int id = hitTest((float)x, (float)y);
            if (id == ID_CUST_DEC || id == ID_CUST_INC) {
                doAction(id);
                S.repeatId = id;
                SetCapture(h);
                SetTimer(h, kRepeatId, 400, nullptr);
            } else if (id) {
                doAction(id);
            } else if (S.focus) {  // borderless: drag the clock anywhere
                ReleaseCapture();
                SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            } else if (!S.started && inDial(x, y)) {
                S.dragging = true;
                SetCapture(h);
                dragDial(x, y);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (S.repeatId) {
                S.repeatId = 0;
                KillTimer(h, kRepeatId);
                ReleaseCapture();
            }
            if (S.dragging) { S.dragging = false; ReleaseCapture(); }
            return 0;
        case WM_CAPTURECHANGED:
            if (S.repeatId) { S.repeatId = 0; KillTimer(h, kRepeatId); }
            S.dragging = false;
            return 0;
        case WM_MOUSEWHEEL: {
            if (S.focus) return 0;  // avoid accidental changes in clock-only mode
            int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            ScreenToClient(h, &pt);
            int id = hitTest((float)pt.x, (float)pt.y);
            bool big = GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_SHIFT) < 0;
            if (id == ID_CUST || id == ID_CUST_DEC || id == ID_CUST_INC) adjustCustom(d * (big ? 5 : 1));
            else adjust(d * (big ? 300 : 60));
            refresh();
            return 0;
        }
        case WM_KEYDOWN: {
            bool repeat = (lp & (1 << 30)) != 0;
            bool ctrl = GetKeyState(VK_CONTROL) < 0;
            switch (wp) {
                case VK_SPACE: if (!repeat) doAction(ID_START); break;
                case 'R': if (!repeat) doAction(ID_RESET); break;
                case 'D': if (!repeat) doAction(ID_THEME); break;
                case 'P': if (!repeat) doAction(ID_TOP); break;
                case 'M': if (!repeat) doAction(ID_SOUND); break;
                case 'C': if (!repeat) doAction(ID_CUST); break;
                case VK_UP: adjust(ctrl ? 300 : 60); refresh(); break;
                case VK_DOWN: adjust(ctrl ? -300 : -60); refresh(); break;
            }
            return 0;
        }
        case WM_TRAY:
            if (lp == WM_LBUTTONUP) {
                if (IsWindowVisible(h) && !IsIconic(h)) ShowWindow(h, SW_HIDE);
                else showWindowNormal();
            } else if (lp == WM_RBUTTONUP) showMenu();
            return 0;
        case WM_DESTROY:
            saveSettings();
            KillTimer(h, kTimerId);
            KillTimer(h, kRepeatId);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
        default:
            if (msg == g_msgTaskbarCreated && g_msgTaskbarCreated) {  // Explorer restarted
                addTrayIcon();
                return 0;
            }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    // single instance: focus the running one
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"VisualTimer.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(kClass, nullptr);
        if (w) { ShowWindow(w, SW_SHOW); ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
        if (mtx) CloseHandle(mtx);
        return 0;
    }
    SetProcessDPIAware();
    {
        HDC sdc = GetDC(nullptr);
        g_dpi = GetDeviceCaps(sdc, LOGPIXELSX) / 96.0;
        ReleaseDC(nullptr, sdc);
    }
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    GdiplusStartupInput gi;
    GdiplusStartup(&g_gdipToken, &gi, nullptr);
    iniPath();
    loadSettings();
    g_icon = loadAppIcon(hi, GetSystemMetrics(SM_CXICON));
    g_iconSmall = loadAppIcon(hi, GetSystemMetrics(SM_CXSMICON));

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = g_icon;
    wc.hIconSm = g_iconSmall;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    RECT r = {0, 0, dpx(420), dpx(700)};
    AdjustWindowRect(&r, kNormalStyle, FALSE);
    int w = r.right - r.left, hgt = r.bottom - r.top, x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int sw = geti(L"W", 0), sh = geti(L"H", 0);
    if (sw > 200 && sh > 300) {  // restore last position if it is still on a screen
        w = sw; hgt = sh;
        x = geti(L"X", 0); y = geti(L"Y", 0);
        RECT v = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
        v.right = v.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        v.bottom = v.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (x < v.left - 50 || x > v.right - 100 || y < v.top - 50 || y > v.bottom - 100) x = y = CW_USEDEFAULT;
    }
    HWND hw = CreateWindowExW(0, kClass, kTitle, kNormalStyle, x, y, w, hgt, nullptr, nullptr, hi, nullptr);
    if (show == SW_MAXIMIZE || show == SW_SHOWMAXIMIZED) show = SW_SHOWNORMAL;
    ShowWindow(hw, show);
    UpdateWindow(hw);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    GdiplusShutdown(g_gdipToken);
    if (mtx) CloseHandle(mtx);
    return (int)m.wParam;
}
