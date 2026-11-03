# Toolscreen for Linux

An **unofficial Linux port** of [Toolscreen](https://github.com/jojoe77777/Toolscreen), the Minecraft speedrunning overlay
tool by jojoe77777. It adds instant resizing (Thin, Wide, EyeZoom), mirrors and overlays to Minecraft Java Edition,
without OBS projectors or leaving fullscreen.

The Windows version injects a DLL into the game. Linux has no equivalent of that, so this port loads a small native
library into the game when it starts (through `LD_PRELOAD`). A one-click installer sets that up in your
**Prism Launcher** or **MultiMC** instance.

> This project is not affiliated with the original Toolscreen. Report problems with the Linux version here, not to
> the Toolscreen author.

---

## What works

| Feature | Status |
|---|---|
| Modes with instant resizing (Fullscreen, Thin, Wide, EyeZoom, Preemptive, custom) | ✅ |
| EyeZoom magnifier with numbered boxes and centre line | ✅ |
| Mirrors and mirror groups (E counter, pie chart, colour filtering, borders) | ✅ |
| Image overlays (including Ninjabrain Bot's `nb-overlay.png`) | ✅ |
| Live Ninjabrain Bot panel (via Ninjabrain Bot's API) | ✅ |
| Animated backgrounds (gradients, images) and mode borders | ✅ |
| Hotkeys, hold/release triggers, game-state conditions | ✅ |
| Per-mode mouse sensitivity and sensitivity hotkeys | ✅ |
| Key rebinding | ✅ (keyboard to keyboard) |
| Config hot-reload, same `config.toml` format as Windows | ✅ |
| In-game status panel (Ctrl+I) | ✅ (shows status and hotkeys; settings are edited in the config file) |
| Full settings editor (Ctrl+I GUI from Windows) | ❌ not yet |
| Window capture overlays, browser overlays, virtual camera, mode animations | ❌ not yet |

Tested with Minecraft 1.16.1 (LWJGL 3.2.2, Fabric, Sodium, MCSR Ranked) on KDE Plasma (X11) with an NVIDIA GPU.

## Requirements

- Linux on x86_64, with glibc 2.34 or newer (any distribution from 2022 onwards)
- **Prism Launcher** (native or Flatpak) or **MultiMC**
- Java, to run the installer (the Java your launcher already uses is fine)
- An X11 session. Wayland sessions are untested; the game runs through XWayland there, so it may work.

---

## Installing

1. **Download** `Toolscreen-1.3.6-linux-double-click-me.jar` from the
   [Releases](../../releases) page.
2. **Open your instance folder.** In Prism Launcher: right-click the instance → **Folder**.
   In MultiMC: right-click → **Instance Folder**.
3. **Copy the jar into that folder.**
4. **Close Prism Launcher / MultiMC completely.** The launcher rewrites the instance settings when it exits, which
   would undo the installation.
5. **Double-click the jar.** If your file manager opens it as an archive instead, right-click → *Open with* →
   *Java*, or run this in a terminal from that folder:
   ```sh
   java -jar Toolscreen-1.3.6-linux-double-click-me.jar
   ```
6. Click **Install**, then reopen your launcher and start the instance as usual.

When the game starts you will see *"Toolscreen … loaded"* in the top-left corner for a few seconds.

You can also run the jar from anywhere: it then lists every MultiMC/Prism instance it finds and lets you pick one.
Without a desktop (or from a script):

```sh
java -jar Toolscreen-1.3.6-linux-double-click-me.jar --list
java -jar Toolscreen-1.3.6-linux-double-click-me.jar --install   "/path/to/instance"
java -jar Toolscreen-1.3.6-linux-double-click-me.jar --uninstall "/path/to/instance"
```

### What the installer changes

- Creates `<instance>/toolscreen-linux/` with `libtoolscreen.so` and a small launch script.
- Sets the instance's **Wrapper command** (Edit instance → Settings → Custom commands) to that script. If you
  already had a wrapper (e.g. `gamemoderun` or `prime-run`), it is kept and still runs.
- Saves a backup of your instance settings as `instance.cfg.toolscreen-backup`.

Nothing outside the instance folder is touched, apart from creating your config file on first launch.

### Uninstalling

Close the launcher, run the jar again and click **Uninstall**. Your instance settings go back to exactly how they were.
Your Toolscreen config is kept.

To launch once **without** Toolscreen, set the environment variable `TOOLSCREEN_DISABLE=1` for the instance
(Edit instance → Settings → Environment variables).

---

## Using it

### Default hotkeys

| Key | Action |
|---|---|
| **Z** | Thin mode (press again to go back) |
| **J** | EyeZoom (press again to go back) |
| **Left Alt** | Wide mode |
| **Ctrl + I** | Status panel: current mode, game state, hotkeys, config location |

Hotkeys don't fire while F3 is held, so F3 shortcuts keep working. When you leave a world (title screen, wall),
Toolscreen switches back to your default mode by itself. That last part needs the
[state-output](https://github.com/tildejustin/state-output) mod; MCSR Ranked and most speedrun packs already include it.

While the Ctrl+I panel is open it takes your key presses. In it, **E** opens the config file, **O** opens the config
folder, **R** reloads the config, and **Esc** (or Ctrl+I) closes it.

### The config file

Everything is set in **`~/.config/toolscreen/config.toml`**, which is created the first time the game starts.
Toolscreen reloads it automatically every time you save it, so you can tweak things while the game is running.

The format is the **same as Windows Toolscreen**. If you already have a Windows config, copy its `config.toml` over
the Linux one. Windows paths to Ninjabrain Bot's overlay image are converted automatically.

Hotkeys use Windows key codes, for example `90` = Z, `74` = J, `164` = Left Alt, `162` = Left Ctrl, `80` = P,
`114` = F3. Here are some common changes:

```toml
# Change EyeZoom to the C key:
[[hotkey]]
keys = [ 67 ]
secondaryMode = 'EyeZoom'

# Lower the mouse sensitivity only while in EyeZoom: in the EyeZoom [[mode]] block
sensitivityOverrideEnabled = true
modeSensitivity = 0.1
```

To keep a separate config per instance, create a folder called `toolscreen` inside the instance's `minecraft`
(or `.minecraft`) folder. Toolscreen then uses `<game folder>/toolscreen/config.toml` instead.

### Ninjabrain Bot

There are two ways to show Ninjabrain Bot in game.

**Live panel (recommended).** Toolscreen reads Ninjabrain Bot's data directly and draws its own table.

1. In Ninjabrain Bot, open **Settings → Advanced** and tick **Enable API (starts HTTP server)**.
2. In `config.toml`, under `[ninjabrainOverlay]`, set `enabled = true`.

Useful options under `[ninjabrainOverlay]`:

```toml
enabled = true
showOnlyAfterMeasurement = true   # stay hidden until your first F3+C in the current world
hideInMenus = true                # hide on the title screen, MCSR Ranked menus, wall and world generation
relativeTo = 'topLeftScreen'      # position anchor, plus x / y offsets
overlayScale = 0.3                # size
shownPredictions = 5              # rows in the table
```

To toggle the panel with a key, add `ninjabrainOverlayHotkey = [ 80 ]` near the top of the file (80 = P). Pressing it
hides the panel, or shows it even before you've measured anything. That choice lasts until you enter the next world.

**Image overlay.** Tick **OBS overlay** in Ninjabrain Bot. It saves an image to `/tmp/nb-overlay.png`, and the
default config's *"Ninjabrain Bot"* image shows it. If you use the live panel, remove the image from your modes'
`sources` so the two don't overlap.

---

## Troubleshooting

**Nothing happens / no "Toolscreen loaded" message**
- Check the launcher's log for lines starting with `[Toolscreen]`.
- Check that the instance's **Wrapper command** points to `toolscreen-linux/toolscreen-wrapper.sh`. If it's empty,
  the launcher was probably open during installation: close it and install again.
- Toolscreen also writes `~/.config/toolscreen/toolscreen.log`.

**Hotkeys don't work**
- Close the Ctrl+I panel first, because it takes key presses while it's open.
- Hotkeys don't fire while one of their *exclusion* keys (F3 by default) is held.

**Ninjabrain panel not showing**
- Open Ctrl+I: it says whether the Ninjabrain Bot API is connected.
- With `showOnlyAfterMeasurement = true`, it only appears after your first F3+C in the current world.

**Config changes don't apply**
- Ctrl+I shows config errors in red, with the line number.

**Other overlays (MangoHud etc.)**
- Toolscreen and MangoHud both hook into OpenGL at startup. If you use both and something misbehaves, try without
  MangoHud first.

---

## How it works (for the curious)

Minecraft talks to the graphics driver through GLFW and OpenGL, and LWJGL looks up every one of those functions by
name at startup. `libtoolscreen.so` is loaded first (via `LD_PRELOAD`) and answers some of those lookups with its own
functions:

- **Resizing**: when you switch to Thin, Toolscreen tells the game its window is, say, 330×1026 pixels. The game
  renders at that size and the real window stays fullscreen. When the game draws its finished frame, Toolscreen
  places it in the middle of the screen and fills the rest with the background.
- **Overlays**: just before each frame is shown, Toolscreen draws EyeZoom, mirrors, images and the Ninjabrain panel
  on top, reading pixels straight from the game's own frame.
- **Input**: key and mouse events pass through Toolscreen first, for hotkeys, rebinds, sensitivity, and mapping mouse
  clicks onto the resized game in menus.

This is the same idea as the Windows version, which does it with DLL injection and Windows API hooks.

## Building from source

You need `g++`, `python3` and Java. A JDK is optional: without `javac`, the build downloads the Eclipse compiler.

```sh
./build.sh
# → native/build/libtoolscreen.so
# → dist/Toolscreen-1.3.6-linux-double-click-me.jar
```

Project layout:

```
native/      the injected library (C++17): hooks, config, input, renderer, Ninjabrain Bot client
  src/       sources
  assets/    default config and font, embedded into the library
  test/      a small GLFW program that behaves like Minecraft, for testing without the game
installer/   the double-click installer (Java 8+, Swing)
```

## Credits

- [jojoe77777](https://github.com/jojoe77777) for [Toolscreen](https://github.com/jojoe77777/Toolscreen). This port
  follows its behaviour, default config and config format.
- [Ninjabrain1](https://github.com/Ninjabrain1) for [Ninjabrain Bot](https://github.com/Ninjabrain1/Ninjabrain-Bot) and
  its API.
- Third-party libraries: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Licensed under the MIT License; see [LICENSE](LICENSE).
