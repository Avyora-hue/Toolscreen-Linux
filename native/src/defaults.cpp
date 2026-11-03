// Support for the Defaults tab: reading Minecraft's options.txt and Ninjabrain Bot's preferences, and writing the
// recommended Ninjabrain Bot settings that the installer saved in recommended.toml.
// Ninjabrain Bot (Java) keeps its settings in ~/.java/.userPrefs/ninjabrainbot/prefs.xml; it reads them at start-up
// and writes them back when they change, so the file is only edited while Ninjabrain Bot is closed.
#include "defaults.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace {

std::string HomeDir() {
    const char* h = getenv("HOME");
    return h ? h : "/tmp";
}

std::string NbPrefsPath() { return HomeDir() + "/.java/.userPrefs/ninjabrainbot/prefs.xml"; }

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string XmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '"') o += "&quot;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else o += c;
    }
    return o;
}

}  // namespace

bool ReadMinecraftOptions(McOptions& out) {
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) return false;
    std::ifstream f(std::string(cwd) + "/options.txt");
    if (!f) return false;
    std::string line;
    out = McOptions{};
    while (std::getline(f, line)) {
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string key = line.substr(0, c), value = line.substr(c + 1);
        if (key == "mouseSensitivity") {
            out.sensitivity = atof(value.c_str());
            out.sensitivityText = value;
            out.found = true;
        } else if (key == "guiScale") {
            out.guiScale = atoi(value.c_str());
        } else if (key == "fov") {
            out.fov = 70.0 + atof(value.c_str()) * 40.0;  // options.txt stores (fov - 70) / 40
        } else if (key == "fullscreen") {
            out.fullscreen = value == "true";
        }
    }
    return out.found;
}

bool NinjabrainRunning() {
    DIR* d = opendir("/proc");
    if (!d) return false;
    bool found = false;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        std::string cmd = ReadFile(std::string("/proc/") + e->d_name + "/cmdline");
        for (auto& ch : cmd) {
            if (ch == '\0') ch = ' ';
        }
        if (cmd.find("Ninjabrain") != std::string::npos || cmd.find("ninjabrain") != std::string::npos) {
            if (cmd.find("java") != std::string::npos) {
                found = true;
                break;
            }
        }
    }
    closedir(d);
    return found;
}

std::map<std::string, std::string> ReadNinjabrainPrefs() {
    std::map<std::string, std::string> out;
    std::string xml = ReadFile(NbPrefsPath());
    size_t pos = 0;
    while ((pos = xml.find("<entry key=\"", pos)) != std::string::npos) {
        size_t ks = pos + 12, ke = xml.find('"', ks);
        size_t vs = xml.find("value=\"", ke);
        if (ke == std::string::npos || vs == std::string::npos) break;
        vs += 7;
        size_t ve = xml.find('"', vs);
        if (ve == std::string::npos) break;
        out[xml.substr(ks, ke - ks)] = xml.substr(vs, ve - vs);
        pos = ve;
    }
    return out;
}

bool WriteNinjabrainPrefs(const std::map<std::string, std::string>& changes, std::string& err) {
    std::string path = NbPrefsPath();
    std::string xml = ReadFile(path);
    if (xml.empty()) {
        err = "Ninjabrain Bot's settings file was not found (start Ninjabrain Bot once first): " + path;
        return false;
    }
    std::ofstream(path + ".toolscreen-backup", std::ios::binary) << xml;
    for (auto& kv : changes) {
        std::string entry = "<entry key=\"" + kv.first + "\" value=\"" + XmlEscape(kv.second) + "\"/>";
        size_t pos = xml.find("<entry key=\"" + kv.first + "\"");
        if (pos != std::string::npos) {
            size_t end = xml.find("/>", pos);
            if (end == std::string::npos) {
                err = "unexpected format in " + path;
                return false;
            }
            xml.replace(pos, end + 2 - pos, entry);
        } else {
            size_t close = xml.rfind("</map>");
            if (close == std::string::npos) {
                err = "unexpected format in " + path;
                return false;
            }
            xml.insert(close, "  " + entry + "\n");
        }
    }
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        f << xml;
        if (!f) {
            err = "cannot write " + tmp;
            return false;
        }
    }
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        err = "cannot replace " + path;
        return false;
    }
    return true;
}
