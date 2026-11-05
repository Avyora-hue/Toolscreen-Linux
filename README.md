# Toolscreen for Linux

An **unofficial Linux port** of [Toolscreen](https://github.com/jojoe77777/Toolscreen), the Minecraft speedrunning overlay
tool by jojoe77777. It adds instant resizing (Thin, Wide, EyeZoom), mirrors, overlays and a live Ninjabrain Bot panel
to Minecraft Java Edition, without OBS projectors or leaving fullscreen.

The Windows version injects a DLL into the game. Linux has no equivalent of that, so this port loads a small native
library into the game when it starts (through `LD_PRELOAD`). A one-click installer sets that up in your
**Prism Launcher** or **MultiMC** instance.

> This project is not affiliated with the original Toolscreen. Report problems with the Linux version here, not to
> the Toolscreen author.

**Contents:** [Features](#features) · [Requirements](#requirements) · [Installing](#installing) ·
[Using it](#using-it) · [Recommended settings (Defaults tab)](#recommended-settings-the-defaults-tab) ·
[Measuring eyes](#measuring-eyes-fov-sensitivity-and-ninjabrain-bot) · [OneShot](#oneshot-bow-one-shot-on-the-ender-dragon) ·
[Troubleshooting](#troubleshooting) · [Changes](#changes) · [How it works](#how-it-works) · [Building](#building-from-source)

---

## Features

| Feature | Status |
|---|---|
| Modes with instant resizing (Fullscreen, Thin, Wide, EyeZoom, Preemptive, custom) | ✅ |
| EyeZoom magnifier with numbered boxes and centre line | ✅ |
| Mirrors and mirror groups (E counter, pie chart, colour filtering, borders) | ✅ |
| Image overlays | ✅ |
| Live Ninjabrain Bot panel (via Ninjabrain Bot's API) | ✅ |
| In-game settings editor (Ctrl+I) | ✅ |
| Edit overlays on screen: drag to move, corner to resize; draw new mirrors | ✅ |
| Game state from the state-output or Hermes mod | ✅ |
| Recommended settings built for your computer (Defaults tab) | ✅ Linux-port addition |
| Low-sensitivity hotkey for measuring (boat-eye safe) | ✅ |
| OneShot mode for the bow one-shot on the Ender Dragon | ✅ Linux-port addition |
| Hotkeys, hold/release triggers, game-state conditions | ✅ |
| Per-mode mouse sensitivity, sensitivity hotkeys, key rebinding | ✅ |
| Same `config.toml` format as Windows Toolscreen, reloaded live | ✅ |
| Window capture overlays, browser overlays, virtual camera, mode animations | ❌ not yet |

Tested with Minecraft 1.16.1 (LWJGL 3.2.2, Fabric, Sodium, MCSR Ranked) launched from Prism Launcher and MultiMC,
on KDE Plasma (X11) with an NVIDIA GPU.

## Requirements

- Linux on x86_64, with glibc 2.34 or newer (any distribution from 2022 onwards)
- **Prism Launcher** (native or Flatpak) or **MultiMC**
- Java, to run the installer (Java 8 or newer; the Java your launcher already uses is fine)
- An X11 session. Wayland sessions are untested; the game runs through XWayland there, so it may work.

---

## Installing

1. **Download** `Toolscreen-1.4.4-linux-double-click-me.jar` from the [Releases](../../releases) page.
2. **Open your instance folder.** In Prism Launcher: right-click the instance → **Folder**.
   In MultiMC: right-click → **Instance Folder**.
3. **Copy the jar into that folder.**
4. **Close Prism Launcher / MultiMC completely** (and Minecraft). The launcher rewrites the instance settings when it
   exits, which would undo the installation. The installer checks for this and offers to close it for you.
5. **Double-click the jar.** If your file manager opens it as an archive instead, right-click → *Open with* →
   *Java*, or run this in a terminal from that folder:
   ```sh
   java -jar Toolscreen-1.4.4-linux-double-click-me.jar
   ```
6. Click **Install**, then reopen your launcher and start the instance as usual.

When the game starts you will see *"Toolscreen … loaded"* in the top-left corner for a few seconds.

You can also run the jar from anywhere: it then lists every MultiMC / Prism instance it finds and lets you pick one.
From a terminal or script:

```sh
java -jar Toolscreen-1.4.4-linux-double-click-me.jar --list
java -jar Toolscreen-1.4.4-linux-double-click-me.jar --install   "/path/to/instance"
java -jar Toolscreen-1.4.4-linux-double-click-me.jar --uninstall "/path/to/instance"
java -jar Toolscreen-1.4.4-linux-double-click-me.jar --build-recommended "/path/to/instance"   # only rebuild the recommended settings
```

### What the installer does

- Creates `<instance>/toolscreen-linux/` with `libtoolscreen.so` and a small launch script.
- Sets the instance's **Wrapper command** (Edit instance → Settings → Custom commands) to that script. If you
  already had a wrapper (e.g. `gamemoderun` or `prime-run`), it is kept and still runs.
- Saves a backup of your instance settings as `instance.cfg.toolscreen-backup`.
- **Builds the recommended settings for your computer** (see [below](#recommended-settings-the-defaults-tab)) from your
  Minecraft mouse sensitivity, GUI scale and screen resolution, and saves them in
  `~/.config/toolscreen/recommended.toml`. If you have no Toolscreen config yet, they also become your starting config.

Nothing is sent anywhere, and nothing outside the instance folder and `~/.config/toolscreen/` is changed.

### Updating and uninstalling

- **Update:** put the new jar in the instance folder, close the launcher, double-click it and choose
  **Reinstall / Update**. Your config is kept.
- **Uninstall:** same, but choose **Uninstall**. Your instance settings go back to exactly how they were; your
  Toolscreen config is kept.
- To launch once **without** Toolscreen, set the environment variable `TOOLSCREEN_DISABLE=1` for the instance
  (Edit instance → Settings → Environment variables).

---

## Using it

### Hotkeys

| Key | Action |
|---|---|
| **Z** | Thin mode (press again to go back) |
| **J** | EyeZoom (press again to go back) |
| **Left Alt** | Wide mode |
| **O** | OneShot mode (dragon fight, see below) |
| **B** | Low mouse sensitivity on/off, for measuring eyes |
| **P** | Show / hide the Ninjabrain Bot panel |
| **Ctrl + I** | Settings editor |

Mode hotkeys don't fire while F3 is held, so F3 shortcuts (like F3+B) keep working. When you leave a world (title
screen, wall), Toolscreen goes back to Fullscreen and turns the low sensitivity off by itself. That needs the
[state-output](https://github.com/tildejustin/state-output) mod, which MCSR Ranked and most speedrun packs include, or
the Hermes mod (used instead whenever it is running).

O, B and P come with the recommended settings; with Toolscreen's plain defaults only Z, J, Left Alt and Ctrl+I exist.

### The settings editor (Ctrl+I)

Ctrl+I opens the editor over the game and frees your mouse; **Esc** or **Ctrl+I** closes it and gives the mouse back.
While it is open, the game doesn't receive your keys or mouse.

- Every change applies **immediately**, so you can watch a mirror move or a mode resize while you adjust it.
- **Save** writes `config.toml` (the previous file is kept as `config.toml.bak`; comments in the file are not kept).
  **Revert to file** throws away unsaved changes.
- To change a hotkey, click its key button and **press the keys**. Esc cancels.
- Tabs: **Status**, **Defaults**, General, Modes, Hotkeys, Mirrors, Mirror groups, Images, EyeZoom, Ninjabrain Bot,
  Mouse & keys.
- **Edit overlays on screen** (checkbox at the top): every mirror, mirror group, image and the Ninjabrain Bot panel
  gets a labelled box. Drag a box to move it, drag its corner square to resize it (groups move only). "Lock aspect
  ratio" keeps the shape while resizing. Move the editor window aside by its title bar to reach overlays behind it.
- **Mirrors → Create interactively:** draw the area to capture, then where to show it; the new mirror is added to the
  current mode. Esc cancels.

### The config file

Everything is stored in **`~/.config/toolscreen/config.toml`**. It reloads automatically whenever you save it, so
editing it by hand works too. The format is the **same as Windows Toolscreen**: a Windows `config.toml` can be copied
over (Windows paths to Ninjabrain Bot's overlay image are converted automatically).

Hotkeys use Windows key codes: `65`–`90` = A–Z, `48`–`57` = 0–9, `112` = F1, `114` = F3, `32` = Space,
`160` = Left Shift, `162` = Left Ctrl, `164` = Left Alt.

To use a separate config for one instance, create a folder called `toolscreen` inside that instance's `minecraft`
(or `.minecraft`) folder; Toolscreen then uses `<game folder>/toolscreen/config.toml`.

---

## Recommended settings (the Defaults tab)

The installer looks at **your** computer once and builds recommended settings for it. Load them in game with
**Ctrl+I → Defaults**:

- **Load recommended Toolscreen settings** applies them immediately (press **Save** to keep them).
- **Apply recommended Ninjabrain Bot settings** writes them into Ninjabrain Bot's own settings. Ninjabrain Bot must be
  **closed** for this (it saves its own settings and would undo the change); a backup is kept next to its settings
  file.

The tab also shows what the installer detected and warns you when your Minecraft sensitivity has changed since, in
which case run the installer again (or `--build-recommended`) to rebuild them. Make sure your in-game sensitivity is
at its normal value when you do: the installer reads it from `options.txt`, and warns if it finds 0.

### What the recommended Toolscreen settings contain

| Setting | Value | Why |
|---|---|---|
| Modes | Toolscreen's own defaults (Fullscreen, Thin, Wide, EyeZoom 384×16384, Preemptive), fitted to your screen | The standard EyeZoom setup used with Ninjabrain Bot |
| Mirrors | E counter, Mapless, pie chart (Toolscreen defaults) | Standard speedrun overlays |
| Ninjabrain Bot panel | On, **top right**, hidden until your first F3+C in a world, hidden on menus / wall / generation | Keeps the top left free for your F3 coordinates; nothing on screen until it has something to show |
| **P** | Show / hide the panel (also reveals it before your first measurement) | Quick toggle |
| **B** | Mouse sensitivity × *your value* (e.g. ×0.1034 at sensitivity 0.377) | Feels exactly like Minecraft's slider at 0 %, without touching Minecraft's setting |
| **O** | OneShot mode, with the dragon health mirror sized for your GUI scale | See [OneShot](#oneshot-bow-one-shot-on-the-ender-dragon) |

### What the recommended Ninjabrain Bot settings contain

Based on Ninjabrain Bot's README and the community boat-eye setup guides:

| Setting | Value | Why |
|---|---|---|
| Sensitivity (1.13+) | your exact value from `options.txt` | Needed for angle adjustments and boat measurements |
| Pixel adjustment type | Tall resolution, height 16384 | Each adjustment is exactly one EyeZoom pixel |
| Hotkeys | **=** adds one pixel to the last angle, **−** removes one | Correct the measurement after F3+C |
| Adjustment display | Number of adjustments | Shows "+2", "−1", … |
| Boat measurements | On, **Blue boat**, allowable error 0.03, boat standard deviation 0.007 | Standard boat-eye setup (green boat needs an advanced setup) |
| Standard deviation | 0.007, unless you already calibrated one (kept) | README: 0.005–0.010 for adjusted measuring |
| Crosshair correction | 0, or the README value for 1440p fullscreen at GUI scale 3 (0.026) / 6 (0.104) | Only those setups need one |
| Advanced stronghold statistics | On | Recommended by the guides |
| API | On | Needed for Toolscreen's in-game panel |

---

## Measuring eyes: FOV, sensitivity and Ninjabrain Bot

- **FOV:** measure at **FOV 30** when using EyeZoom. Ninjabrain Bot's "Tall resolution" adjustment works out the angle
  of one pixel with tan(15°), i.e. it assumes a 30° FOV, so this holds on every monitor. (Measuring *without* EyeZoom,
  "Perfect Travel", uses FOV 31 at 1920×1080 or 2560×1440 fullscreen and 30 at 1920×1027 or 2560×1387 windowed; the
  Defaults tab shows the value for your resolution.) Toolscreen can't change Minecraft's FOV, so set it in Options
  while measuring.
- **Sensitivity:** don't lower Minecraft's slider for measuring; press **B** instead and press it again afterwards.
  Toolscreen only ever sends the game whole mouse steps, so boat-eye measuring stays exact, and Minecraft's own
  sensitivity (which Ninjabrain Bot relies on) never changes.
- **Ninjabrain Bot panel:** in Ninjabrain Bot tick *Settings → Advanced → Enable API (starts HTTP server)* (the
  recommended Ninjabrain Bot settings do this). The panel shows the predictions (location, certainty, distance, nether
  coordinates, angle with the turn direction), your throws, warnings, blind results and the boat state.

---

## OneShot (bow one-shot on the Ender Dragon)

In the one-shot, the dragon launches you while your bow is drawn and you release at that moment: the arrow keeps
your launch speed, and arrow damage grows with speed (about speed × 2; 200 is needed on a head hit). Toolscreen can't
time the release for you (it only sees the picture, and automating it would be a macro). Press **O** during the fight
for visual aids:

- **OneShot Aim:** the area around your crosshair, magnified 3× on the left, for lining up the dragon's head.
- **Dragon HP:** the boss health bar, doubled, just below your crosshair.

To add it to an existing config by hand, copy [`practice/oneshot-toolscreen.toml`](practice/oneshot-toolscreen.toml)
(the boss-bar numbers in it are for GUI scale 2; the file explains how to change them).

Check your category's rules on the one-shot before using it in runs.

---

## Troubleshooting

**Nothing happens / no "Toolscreen loaded" message**
- Check the instance's **Wrapper command** points to `toolscreen-linux/toolscreen-wrapper.sh`. If it's empty, the
  launcher was probably open during installation: close it and install again.
- Toolscreen writes a log to `~/.config/toolscreen/toolscreen.log`; its messages also appear in the launcher's game
  log, starting with `[Toolscreen]`.

**Hotkeys don't work**
- Close the Ctrl+I editor first: while it is open it takes your key presses.
- Mode hotkeys don't fire while F3 is held.

**Ninjabrain Bot panel not showing**
- Ctrl+I → Status says whether the Ninjabrain Bot API is connected.
- By default it only appears after your first F3+C in the current world (P shows it earlier) and never on menus.

**Config changes don't apply**
- Ctrl+I → Status shows config errors with the line number.

**Performance:** to see what Toolscreen costs per frame, set the environment variable `TOOLSCREEN_PERF=1` for the
instance; it then logs its CPU/GPU time every 5 seconds.

---

## Changes

### 1.4.4-linux.1

Brings in the useful changes of Toolscreen 1.4.4 (the build inside the 1.4.7 download; 1.4.5 to 1.4.7 changed only
the Windows installer):

- **Edit overlays on screen** in the Ctrl+I editor: drag to move, corner to resize, aspect-ratio lock; works for
  mirrors, mirror groups, images and the Ninjabrain Bot panel, with every anchor.
- **Create mirrors interactively** by drawing the capture area and the destination on screen.
- **Hermes game-state support** next to state-output (Hermes is used while its heartbeat is live).
- **Opacity for each Ninjabrain Bot panel colour** (stored as a 4th colour value, as in Toolscreen 1.4).
- **Rebound keys can no longer get stuck:** a key's release now always goes to the key its press went to, even if
  rebinds were toggled or the cursor state changed while it was held.
- Exclusion keys are checked again when a trigger-on-release hotkey fires; your home folder shows as `~` in the editor.
- **EyeZoom numbers are sharp at small sizes:** small text is drawn from glyphs made at its own size instead of a
  shrunk 64 px font.

Windows-only fixes from 1.4 (cursor and alt-tab handling, low-level keyboard hooks, OBS capture) don't apply here.
Toolscreen's CPU time per frame is unchanged.

### 1.3.6-linux.2

- **Settings editor (Ctrl+I)** with tabs for everything in the config, live preview, Save / Revert, and hotkey
  recording by pressing keys. Fixed for modded instances: some mods leave an OpenGL pixel buffer bound, which made the
  editor invisible (and, since it was open, swallowed all hotkeys).
- **Recommended settings** built once by the installer for your computer, loaded from the new **Defaults** tab
  (Toolscreen and Ninjabrain Bot settings, measuring FOV per resolution). New installer option `--build-recommended`.
- **Low-sensitivity hotkey (B)** for measuring eyes, with an on-screen message; turns itself off when you leave a world.
- **Mouse sensitivity scaling now sends whole mouse steps** (as Windows Toolscreen does, but without its rounding
  overshoot), so per-mode sensitivity and sensitivity hotkeys are safe for boat-eye measuring.
- **Ninjabrain Bot panel:** "show only after measuring", "hide in menus", P reveals / hides it, placed top right in the
  recommended settings.
- **OneShot mode**; new `topCenter` / `bottomCenter` anchors for mirrors and images.
- **Performance:** the overlay pass is skipped in frames where nothing is drawn (mirrors report asynchronously whether
  they matched anything), cutting Toolscreen's CPU time per frame from about 0.13 ms to 0.008 ms in normal play.
- Installer: Prism is detected correctly when its folder still has an old `multimc.cfg`; clearer window and messages;
  warns when Minecraft's sensitivity is 0 while building recommended settings.

### 1.3.6-linux.1

- First release: modes and resizing, EyeZoom, mirrors, image overlays, live Ninjabrain Bot panel, hotkeys, sensitivity,
  rebinds, config hot-reload, and the installer for Prism Launcher and MultiMC.

---

## How it works

Minecraft talks to the graphics driver through GLFW and OpenGL, and LWJGL looks up every one of those functions by
name at startup. `libtoolscreen.so` is loaded first (via `LD_PRELOAD`) and answers some of those lookups with its own
functions:

- **Resizing:** when you switch to Thin, Toolscreen tells the game its window is, say, 330×1026 pixels. The game
  renders at that size and the real window stays fullscreen. When the game draws its finished frame, Toolscreen places
  it in the middle of the screen and fills the rest with the background.
- **Overlays:** just before each frame is shown, Toolscreen draws EyeZoom, mirrors, images, the Ninjabrain panel and the
  editor on top, reading pixels straight from the game's own frame.
- **Input:** key and mouse events pass through Toolscreen first, for hotkeys, rebinds, sensitivity, the editor, and
  mapping mouse clicks onto the resized game in menus.

This is the same idea as the Windows version, which does it with DLL injection and Windows API hooks.

## Building from source

You need `g++`, `python3` and Java. A JDK is optional: without `javac`, the build downloads the Eclipse compiler.

```sh
./build.sh
# → native/build/libtoolscreen.so
# → dist/Toolscreen-1.4.4-linux-double-click-me.jar
```

Project layout:

```
native/      the injected library (C++17)
  src/       hooks, config, input, renderer, Ninjabrain Bot client, settings editor (Dear ImGui), Defaults support
  assets/    default config and font, embedded into the library
  test/      a small GLFW program that behaves like Minecraft, for testing without the game
installer/   the double-click installer (Java 8+, Swing); builds the recommended settings
practice/    OneShot config snippet (used by the installer)
```

## Credits

- [jojoe77777](https://github.com/jojoe77777) for [Toolscreen](https://github.com/jojoe77777/Toolscreen). This port
  follows its behaviour, default config and config format.
- [Ninjabrain1](https://github.com/Ninjabrain1) for [Ninjabrain Bot](https://github.com/Ninjabrain1/Ninjabrain-Bot) and
  its API; the recommended Ninjabrain Bot settings follow its README and the community boat-eye guides.
- Third-party libraries: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Licensed under the MIT License; see [LICENSE](LICENSE).
