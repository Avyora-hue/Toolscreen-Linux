Toolscreen for Linux
====================

This folder was created by the Toolscreen installer. The instance's Wrapper command
(Edit instance -> Settings -> Custom commands) runs toolscreen-wrapper.sh, which starts
the game with libtoolscreen.so preloaded.

  Config file:   ~/.config/toolscreen/config.toml   (reloads automatically when saved)
  Recommended:   ~/.config/toolscreen/recommended.toml, built by the installer for this computer;
                 load it in game with Ctrl+I -> Defaults
  Log file:      ~/.config/toolscreen/toolscreen.log; messages also appear in the launcher's game log as [Toolscreen]

  In game:       Ctrl+I opens the settings editor (Esc or Ctrl+I closes it)
  Hotkeys:       Z Thin, J EyeZoom, Left Alt Wide; with the recommended settings also
                 O OneShot, B low mouse sensitivity, P Ninjabrain Bot panel

The config format is the same as Windows Toolscreen, so a config.toml copied from Windows works.

To launch once without Toolscreen, set the environment variable TOOLSCREEN_DISABLE=1.
To update or remove it, close the launcher, run the installer jar again and choose
Reinstall / Update or Uninstall.
