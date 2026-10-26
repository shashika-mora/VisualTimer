# Visual Timer

A tiny, native **Windows-only** visual countdown timer (C++ / Win32 / GDI+) built to make time *visible*,
inspired by Time Timer-style analog visual timers. One portable `.exe` (~1 MB), no installer, no runtime.

## Features

- **Red-disc dial**: the remaining time is a shrinking wedge, so you *see* time disappear (no mental subtraction).
- **Overtime mode**: after zero, an amber wedge grows and the clock counts `+mm:ss`, which makes hyperfocus overruns visible.
- Presets (5 / 10 / 15 / 25 / 30 / 45 / 60 min); click and drag the dial, mouse wheel, or Up/Down keys for custom time.
- **Always on top** or normal (background) window mode.
- **Notifications** (Windows toast/balloon via tray icon) and taskbar flashing; time also shown in the title/taskbar tooltip.
- **Sound alarm** (simple built-in beeps, no audio files).
- **Step-down warnings** at 5 min and 1 min left, optional **check-ins** every 5/10/15/30 min ("12 min elapsed, 13 left"), and repeating overtime reminders.
- Dark / light theme, 6 accent colors, "fit" dial mode (dial scale = chosen duration instead of 60 min).
- Settings and window position saved in `VisualTimer.ini` next to the exe. Single-instance, tray icon (left-click show/hide, right-click menu).

Shortcuts: `Space` start/pause, `R` reset, `Up/Down` +-1 min (`Ctrl` = 5), `D` theme, `P` pin on top, `M` mute.

## Build

Requires MinGW-w64 `g++` (e.g. MSYS2 ucrt64):

```powershell
./build.ps1      # -> build\VisualTimer.exe
```

## Why this design? (ADHD & time research notes)

- Time perception differences in ADHD are well documented. A lifespan meta-analysis (824 effect sizes) found a moderate-to-large deficit
  (mean g ~ 0.69), moderated by age and working memory; youth meta-analyses report g ~ 0.4-0.66. Findings are consistent across visual and auditory timing.
- Barkley frames ADHD as impaired self-regulation across time ("time blindness"): distant deadlines feel non-existent; waiting feels aversive (delay aversion).
- Willpower to "watch the clock" fails because internal tracking depends on working memory, so the common recommendation is to **externalize time**.
- Design consequences used here:
  1. **Analog area = time left**: visual timers turn time into space; digits alone require abstract subtraction.
  2. **Always visible** (always-on-top, title bar and tray tooltip) so time stays in view during hyperfocus or drift.
  3. **Graduated warnings** (5 min, 1 min) instead of one abrupt alarm, so transitions are less jarring.
  4. **Periodic check-ins** to break time-blind stretches without needing to remember to look.
  5. **Overtime made visible** (not silent) and **elapsed vs planned** always shown, which supports estimate-then-measure calibration of your planning fallacy.
  6. Low friction: one click on a preset starts the timer; no accounts, no setup.

This app is a productivity aid, not medical advice.

## License

MIT
