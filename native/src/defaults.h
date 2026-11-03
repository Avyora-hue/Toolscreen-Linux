// Support for the Defaults tab (defaults.cpp). The recommended settings themselves are built once by the installer
// and saved in <config dir>/recommended.toml.
#pragma once

#include "ts.h"

#include <map>

// Values from Minecraft's options.txt in the game directory.
struct McOptions {
    bool found = false;
    double sensitivity = 0.5;
    std::string sensitivityText;
    int guiScale = 0;
    double fov = 70;
    bool fullscreen = false;
};

bool ReadMinecraftOptions(McOptions& out);
bool NinjabrainRunning();
std::map<std::string, std::string> ReadNinjabrainPrefs();
bool WriteNinjabrainPrefs(const std::map<std::string, std::string>& changes, std::string& err);
