# Visual Timer

A lightweight, native **Windows-only** visual countdown timer (C++ / Win32 / GDI+) built to make elapsed and remaining time *physically visible*. Inspired by analog Time Timer clocks and designed specifically for ADHD time awareness ("time blindness").

Single portable `.exe` (~316 KB) — no installer, no external runtime dependencies, no registry clutter.

---

## Features

- **Shrinking Color Wedge Dial**: Remaining time is shown as a colored sector that shrinks clockwise to `0`, turning abstract time into immediate spatial area without mental subtraction.
- **Borderless Focus Mode (Clock-Only)**:
  - As soon as the timer starts, all outer UI elements and window borders disappear, leaving **only the round clock dial** floating on screen.
  - **Hover to Reveal**: Move your mouse over the clock to reveal the digital countdown (`mm:ss`), elapsed status, and the **PAUSE** button. Move your mouse away and they hide again.
  - **Drag Anywhere**: Click and drag anywhere on the round clock to reposition it on your screen.
  - **Pause to Restore**: Click **PAUSE** (or press `Space`) to immediately restore the full application window and controls.
- **Presets & Adjustable Custom Time (1–60 min)**:
  - Quick presets: `5`, `10`, `15`, `25`, `30`, `45`, `60` minutes.
  - **Custom Slot** at the end of the preset row (defaults to `50` min, adjustable from `1` to `60` min):
    - Click `−` or `+` (hold to auto-repeat, or hold `Shift` for `±5` min steps).
    - Scroll the mouse wheel over the custom slot to adjust `1–60` min (`Ctrl`/`Shift` + wheel = `±5` min).
    - Click the custom number button (or press `C`) to start the custom countdown immediately.
  - You can also click & drag directly on the dial face, scroll the mouse wheel on the dial, or use `Up`/`Down` arrow keys.
- **Overtime Tracking**: When time runs out, the timer doesn't just stop silently—an amber wedge grows clockwise and the counter tracks `+mm:ss` overtime so hyperfocus overruns are immediately visible.
- **Always on Top / Background**: Toggle **On top** to keep the clock floating above all windows, or run it as a normal background window / minimized to the system tray.
- **Step-Down Warnings & Periodic Check-Ins**:
  - Gentle heads-up notifications at **5 minutes** and **1 minute** remaining to ease task transitions.
  - Optional **Check-in** nudges every `5m`, `10m`, `15m`, or `30m` ("10 min elapsed, 15 min left").
  - Repeating **Overtime** reminders every 2 minutes past zero.
- **Sound Alarm & System Tray Notifications**: Built-in synthesized audio cues (no external `.wav` files required) and Windows tray balloon/toast notifications + non-intrusive taskbar flashing.
- **Themes & Customization**:
  - **Dark** and **Light** themes with matching native Windows DWM caption & border styling (clean title bar without minimize/maximize clutter).
  - **6 Accent Colors** (Red, Orange, Blue, Green, Purple, Pink).
  - **Dial Scale**: Switch between standard `60 min` clock scale and `fit` (proportional) scale where the full circle equals your chosen duration.
- **Portable Persistence**: Saves your custom duration, theme, color, toggles, and window position in `VisualTimer.ini` right next to `VisualTimer.exe`.

---

## Keyboard & Mouse Shortcuts

| Input | Action |
| :--- | :--- |
| `Space` | Start / Pause / Resume timer (toggles Focus Mode) |
| `C` | Start timer with the Custom (`1–60` min) slot value |
| `R` | Reset timer and return to normal window |
| `Up` / `Down` | Adjust duration by `±1` min (`Ctrl` + `Up`/`Down` = `±5` min) |
| `Mouse Wheel` | Adjust duration on dial, or adjust `1–60` range when hovering Custom slot |
| `D` | Toggle Dark / Light theme |
| `P` | Toggle Always on Top (`On top`) |
| `M` | Toggle Sound (`Mute`) |
| `Tray Icon` | Left-click to Show/Hide; Right-click for quick menu |

---

## Building from Source

Requires MinGW-w64 `g++` and `windres` (e.g., via MSYS2 `ucrt64` or `mingw64`):

```powershell
./build.ps1
```

This generates the multi-size application icon (`res/app.ico`), compiles the Windows resource file (`res/app.rc`), and links a single standalone static executable at `build\VisualTimer.exe`.

---

## GitHub Release Packaging

To build the release binary and create a GitHub release with `VisualTimer.exe` attached:

```powershell
./release.ps1
```

*(If GitHub CLI `gh` is authenticated, `release.ps1` will push and publish the release automatically; otherwise it packages `dist\VisualTimer.exe` and `dist\VisualTimer-v1.0.0-win64.zip` ready to upload on your GitHub Releases page.)*

---

## Why This Design? (ADHD & Time Blindness Research)

1. **Time Perception Deficits in ADHD**: Meta-analyses across the lifespan (pooling 800+ effect sizes) demonstrate consistent moderate-to-large impairments in temporal perception and duration discrimination ($g \approx 0.40\text{–}0.69$), heavily moderated by working memory load.
2. **Delay Aversion & Myopia to the Future**: Rooted in Russell Barkley's model of ADHD as a disorder of self-regulation across time, deadlines that are not immediate ("now") carry weak motivational salience, while hyperfocus can erase awareness of passing hours.
3. **Externalizing Time**: Because internal time tracking relies on taxed working memory, "trying harder to watch the clock" reliably fails. Clinical strategies emphasize **externalizing time** into physical space:
   - **Spatial Representation**: A shrinking colored wedge bypasses digit-subtraction and lets the visual cortex perceive remaining time at a glance.
   - **Minimal Visual Clutter (Focus Mode)**: Hiding buttons and borders while running prevents UI distraction while keeping the analog time wedge anchored in peripheral vision.
   - **Transition Buffering**: Step-down warnings (5 min, 1 min) prevent the jarring "alarm shock" of sudden task switching.
   - **Calibration (Elapsed vs. Planned & Overtime)**: Displaying elapsed time alongside overtime helps calibrate the *planning fallacy* (chronic underestimation of how long tasks take).

*Note: This tool is a productivity and time-awareness aid, not a medical device.*

---

## License

[MIT License](LICENSE)
