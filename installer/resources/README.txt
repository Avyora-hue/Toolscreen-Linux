Toolscreen for Linux
====================

This folder was created by the Toolscreen installer. The instance's Wrapper command
(Edit instance -> Settings -> Custom commands) runs toolscreen-wrapper.sh, which starts
the game with libtoolscreen.so preloaded.

  Config file:   ~/.config/toolscreen/config.toml   (or <game dir>/toolscreen/config.toml if that folder exists)
  Log file:      next to the config, toolscreen.log; messages also appear in the launcher's log as [Toolscreen]
  In game:       Ctrl+I shows status and hotkeys; the config reloads automatically when saved
  Defaults:      Z = Thin, J = EyeZoom, Left Alt = Wide (same as Windows Toolscreen)

The config format is the same as Windows Toolscreen, so a config.toml copied from Windows works.
Windows paths to Ninjabrain Bot's nb-overlay.png are mapped to /tmp/nb-overlay.png.

To launch once without Toolscreen, set the environment variable TOOLSCREEN_DISABLE=1.
To remove it, run the installer jar again and choose Uninstall.
