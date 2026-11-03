// TOML-level access to the config, for the settings editor.
#pragma once

#include "ts.h"

#define TOML_EXCEPTIONS 1
#include "../third_party/toml.hpp"

// Fills `out` from a parsed config document (the same parsing LoadConfig uses).
void ConfigFromToml(const toml::table& root, Config& out);
// Reads config.toml into `out`; on failure returns false with a message in `err`.
bool ReadConfigToml(toml::table& out, std::string& err);
// Writes the document to config.toml (keeping the previous file as config.toml.bak).
bool WriteConfigToml(const toml::table& doc, std::string& err);
// The built-in default config, adjusted to the screen size.
std::string DefaultConfigText(int screenW, int screenH);
