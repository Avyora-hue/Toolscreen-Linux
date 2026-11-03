// Config loading: the same TOML schema as Windows Toolscreen, so a config.toml copied from Windows works unchanged.
#include "ts.h"

#include "config_toml.h"
#include "expression_parser.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

extern "C" const char ts_default_toml[];
extern "C" const unsigned int ts_default_toml_size;

namespace {

bool DirExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool FileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void MakeDirs(const std::string& p) {
    std::string cur;
    std::stringstream ss(p);
    std::string part;
    if (!p.empty() && p[0] == '/') cur = "/";
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        cur += part + "/";
        mkdir(cur.c_str(), 0755);
    }
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

int GetInt(const toml::table& t, const char* key, int def) {
    auto n = t[key];
    if (auto v = n.value<int64_t>()) return static_cast<int>(*v);
    if (auto v = n.value<double>()) return static_cast<int>(*v);
    return def;
}

float GetFloat(const toml::table& t, const char* key, float def) {
    auto n = t[key];
    if (auto v = n.value<double>()) return static_cast<float>(*v);
    return def;
}

bool GetBool(const toml::table& t, const char* key, bool def) {
    if (auto v = t[key].value<bool>()) return *v;
    return def;
}

std::string GetStr(const toml::table& t, const char* key, const std::string& def) {
    if (auto v = t[key].value<std::string>()) return *v;
    return def;
}

Color ParseColor(const toml::node* n, Color def) {
    if (!n) return def;
    const toml::array* a = n->as_array();
    if (!a || a->size() < 3) return def;
    auto comp = [&](size_t i, float d) {
        if (auto v = (*a)[i].value<double>()) return static_cast<float>(*v) / 255.0f;
        return d;
    };
    Color c;
    c.r = comp(0, def.r);
    c.g = comp(1, def.g);
    c.b = comp(2, def.b);
    c.a = a->size() >= 4 ? comp(3, 1.0f) : 1.0f;
    return c;
}

Color GetColor(const toml::table& t, const char* key, Color def) { return ParseColor(t.get(key), def); }

std::vector<int> GetIntArray(const toml::table& t, const char* key) {
    std::vector<int> out;
    if (auto a = t[key].as_array()) {
        for (auto& e : *a) {
            if (auto v = e.value<int64_t>()) out.push_back(static_cast<int>(*v));
        }
    }
    return out;
}

std::vector<std::string> GetStrArray(const toml::table& t, const char* key) {
    std::vector<std::string> out;
    if (auto a = t[key].as_array()) {
        for (auto& e : *a) {
            if (auto v = e.value<std::string>()) out.push_back(*v);
        }
    }
    return out;
}

Dimension ParseDimension(const toml::node* n, int def) {
    Dimension d;
    d.pixels = def;
    if (!n) return d;
    if (auto v = n->value_exact<int64_t>()) {
        d.kind = Dimension::Pixels;
        d.pixels = static_cast<int>(*v);
    } else if (auto f = n->value_exact<double>()) {
        if (*f >= 0.0 && *f <= 1.0) {
            d.kind = Dimension::Relative;
            d.relative = static_cast<float>(*f);
        } else {
            d.kind = Dimension::Pixels;
            d.pixels = static_cast<int>(*f);
        }
    } else if (auto s = n->value<std::string>()) {
        d.kind = Dimension::Expression;
        d.expr = *s;
    }
    return d;
}

BorderConfig ParseBorder(const toml::table* t) {
    BorderConfig b;
    if (!t) return b;
    b.enabled = GetBool(*t, "enabled", false);
    b.color = GetColor(*t, "color", b.color);
    b.width = GetInt(*t, "width", 4);
    b.radius = GetInt(*t, "radius", 0);
    return b;
}

GradientAnim ParseAnim(const std::string& s) {
    if (s == "Rotate") return GradientAnim::Rotate;
    if (s == "Slide") return GradientAnim::Slide;
    if (s == "Wave") return GradientAnim::Wave;
    if (s == "Spiral") return GradientAnim::Spiral;
    if (s == "Fade") return GradientAnim::Fade;
    return GradientAnim::None;
}

MirrorOutput ParseMirrorOutput(const toml::table* t) {
    MirrorOutput o;
    if (!t) return o;
    o.relativeTo = GetStr(*t, "relativeTo", o.relativeTo);
    o.x = GetInt(*t, "x", 0);
    o.y = GetInt(*t, "y", 0);
    o.scale = GetFloat(*t, "scale", 1);
    o.scaleX = GetFloat(*t, "scaleX", 1);
    o.scaleY = GetFloat(*t, "scaleY", 1);
    o.separateScale = GetBool(*t, "separateScale", false);
    o.useRelativePosition = GetBool(*t, "useRelativePosition", false);
    o.relativeX = GetFloat(*t, "relativeX", 0.5f);
    o.relativeY = GetFloat(*t, "relativeY", 0.5f);
    return o;
}

HotkeyConditions ParseConditions(const toml::table* t) {
    HotkeyConditions c;
    if (!t) return c;
    c.gameState = GetStrArray(*t, "gameState");
    c.exclusions = GetIntArray(*t, "exclusions");
    return c;
}

// Image paths written on Windows: Ninjabrain Bot's overlay lives in java.io.tmpdir, which is /tmp on Linux.
std::string TranslateImagePath(const std::string& name, const std::string& path) {
    std::string p = path;
    if (p.empty()) {
        if (name == "Ninjabrain Bot") return "/tmp/nb-overlay.png";
        return p;
    }
    bool windowsPath = (p.size() > 2 && p[1] == ':') || p.find('\\') != std::string::npos;
    if (windowsPath) {
        std::string base = p.substr(p.find_last_of("\\/") + 1);
        if (base == "nb-overlay.png") return "/tmp/nb-overlay.png";
        ReplaceAll(p, "\\", "/");
    }
    if (!p.empty() && p[0] == '~') {
        const char* home = getenv("HOME");
        if (home) p = std::string(home) + p.substr(1);
    }
    if (!p.empty() && p[0] != '/') p = ConfigDir() + "/" + p;
    return p;
}

void ParseConfig(const toml::table& root, Config& c) {
    c.defaultMode = GetStr(root, "defaultMode", "Fullscreen");
    if (root.contains("guiHotkey")) c.guiHotkey = GetIntArray(root, "guiHotkey");
    c.imageOverlaysHotkey = GetIntArray(root, "imageOverlaysHotkey");
    c.ninjabrainOverlayHotkey = GetIntArray(root, "ninjabrainOverlayHotkey");
    c.mouseSensitivity = GetFloat(root, "mouseSensitivity", 1);
    c.mirrorMatchColorspace = GetStr(root, "mirrorMatchColorspace", "Auto");
    c.fpsLimit = GetInt(root, "fpsLimit", 0);
    c.startupIndicatorMode = GetInt(root, "startupIndicatorMode", 1);
    c.fontPath = GetStr(root, "fontPath", "");

    if (auto ez = root["eyezoom"].as_table()) {
        EyeZoomConfig& e = c.eyezoom;
        e.cloneWidth = std::max(2, GetInt(*ez, "cloneWidth", e.cloneWidth));
        e.cloneHeight = std::max(1, GetInt(*ez, "cloneHeight", e.cloneHeight));
        e.overlayWidth = GetInt(*ez, "overlayWidth", e.overlayWidth);
        e.windowWidth = GetInt(*ez, "windowWidth", e.windowWidth);
        e.windowHeight = GetInt(*ez, "windowHeight", e.windowHeight);
        e.useCustomSizePosition = GetBool(*ez, "useCustomSizePosition", false);
        e.zoomAreaWidth = GetInt(*ez, "zoomAreaWidth", 0);
        e.zoomAreaHeight = GetInt(*ez, "zoomAreaHeight", 0);
        e.positionX = GetInt(*ez, "positionX", 0);
        e.positionY = GetInt(*ez, "positionY", 0);
        e.rectHeight = GetInt(*ez, "rectHeight", e.rectHeight);
        e.linkRectToFont = GetBool(*ez, "linkRectToFont", true);
        e.fontSizeMode = GetStr(*ez, "fontSizeMode", "auto");
        e.textFontSize = GetInt(*ez, "textFontSize", e.textFontSize);
        e.textFontPath = GetStr(*ez, "textFontPath", "");
        e.gridColor1 = GetColor(*ez, "gridColor1", e.gridColor1);
        e.gridColor1.a = GetFloat(*ez, "gridColor1Opacity", 1);
        e.gridColor2 = GetColor(*ez, "gridColor2", e.gridColor2);
        e.gridColor2.a = GetFloat(*ez, "gridColor2Opacity", 1);
        e.centerLineColor = GetColor(*ez, "centerLineColor", e.centerLineColor);
        e.centerLineColor.a = GetFloat(*ez, "centerLineColorOpacity", 1);
        e.textColor = GetColor(*ez, "textColor", e.textColor);
        e.textColor.a = GetFloat(*ez, "textColorOpacity", 1);
    }

    if (auto arr = root["mode"].as_array()) {
        for (auto& node : *arr) {
            auto t = node.as_table();
            if (!t) continue;
            ModeConfig m;
            m.id = GetStr(*t, "id", "");
            m.width = ParseDimension(t->get("width"), 0);
            m.height = ParseDimension(t->get("height"), 0);
            if (auto src = (*t)["sources"].as_array()) {
                for (auto& s : *src) {
                    if (auto st = s.as_table()) m.sources.push_back({GetStr(*st, "type", ""), GetStr(*st, "id", "")});
                }
            }
            // Older configs list mirrors/images in separate arrays.
            for (auto& id : GetStrArray(*t, "mirrorIds")) m.sources.push_back({"Mirror", id});
            for (auto& id : GetStrArray(*t, "mirrorGroupIds")) m.sources.push_back({"MirrorGroup", id});
            for (auto& id : GetStrArray(*t, "imageIds")) m.sources.push_back({"Image", id});
            m.border = ParseBorder((*t)["border"].as_table());
            if (auto st = (*t)["stretch"].as_table()) {
                m.stretch.enabled = GetBool(*st, "enabled", false);
                m.stretch.x = GetInt(*st, "x", 0);
                m.stretch.y = GetInt(*st, "y", 0);
                m.stretch.width = GetInt(*st, "width", 0);
                m.stretch.height = GetInt(*st, "height", 0);
            }
            if (auto bg = (*t)["background"].as_table()) {
                BackgroundConfig& b = m.background;
                b.selectedMode = GetStr(*bg, "selectedMode", "color");
                b.color = GetColor(*bg, "color", b.color);
                b.angleDeg = GetFloat(*bg, "gradientAngle", 0);
                b.animation = ParseAnim(GetStr(*bg, "gradientAnimation", "None"));
                b.animationSpeed = GetFloat(*bg, "gradientAnimationSpeed", 1);
                b.colorFade = GetBool(*bg, "gradientColorFade", false);
                b.image = GetStr(*bg, "image", "");
                if (!b.image.empty()) b.image = TranslateImagePath("", b.image);
                if (auto stops = (*bg)["gradientStops"].as_array()) {
                    for (auto& s : *stops) {
                        if (auto stt = s.as_table()) {
                            GradientStop gs;
                            gs.color = GetColor(*stt, "color", gs.color);
                            gs.position = GetFloat(*stt, "position", 0);
                            b.stops.push_back(gs);
                        }
                    }
                }
            }
            m.sensitivityOverrideEnabled = GetBool(*t, "sensitivityOverrideEnabled", false);
            m.modeSensitivity = GetFloat(*t, "modeSensitivity", 1);
            m.modeSensitivityX = GetFloat(*t, "modeSensitivityX", 1);
            m.modeSensitivityY = GetFloat(*t, "modeSensitivityY", 1);
            m.separateXYSensitivity = GetBool(*t, "separateXYSensitivity", false);
            c.modes.push_back(std::move(m));
        }
    }

    if (auto arr = root["mirror"].as_array()) {
        for (auto& node : *arr) {
            auto t = node.as_table();
            if (!t) continue;
            MirrorConfig m;
            m.name = GetStr(*t, "name", "");
            m.captureWidth = std::max(1, GetInt(*t, "captureWidth", 50));
            m.captureHeight = std::max(1, GetInt(*t, "captureHeight", 50));
            if (auto in = (*t)["input"].as_array()) {
                for (auto& i : *in) {
                    if (auto it = i.as_table()) {
                        MirrorInput mi;
                        mi.relativeTo = GetStr(*it, "relativeTo", mi.relativeTo);
                        mi.x = GetInt(*it, "x", 0);
                        mi.y = GetInt(*it, "y", 0);
                        m.inputs.push_back(mi);
                    }
                }
            }
            m.output = ParseMirrorOutput((*t)["output"].as_table());
            if (auto col = (*t)["colors"].as_table()) {
                if (auto tc = (*col)["targetColors"].as_array()) {
                    for (auto& e : *tc) m.targetColors.push_back(ParseColor(&e, Color{}));
                }
                m.outputColor = GetColor(*col, "output", m.outputColor);
                m.borderColor = GetColor(*col, "border", m.borderColor);
            }
            if (m.targetColors.size() > 8) m.targetColors.resize(8);
            m.colorSensitivity = GetFloat(*t, "colorSensitivity", 0.001f);
            m.colorPassthrough = GetBool(*t, "colorPassthrough", false);
            m.rawOutput = GetBool(*t, "rawOutput", false);
            m.fps = GetInt(*t, "fps", 30);
            m.opacity = GetFloat(*t, "opacity", 1);
            m.outputScaleAppliesInGroups = GetBool(*t, "outputScaleAppliesInGroups", false);
            if (auto b = (*t)["border"].as_table()) {
                m.border.isStatic = GetStr(*b, "type", "Dynamic") == "Static";
                m.border.dynamicThickness = std::max(0, GetInt(*b, "dynamicThickness", 1));
                m.border.staticCircle = GetStr(*b, "staticShape", "Rectangle") == "Circle";
                m.border.staticColor = GetColor(*b, "staticColor", m.border.staticColor);
                m.border.staticThickness = GetInt(*b, "staticThickness", 2);
                m.border.staticRadius = GetInt(*b, "staticRadius", 0);
                m.border.staticOffsetX = GetInt(*b, "staticOffsetX", 0);
                m.border.staticOffsetY = GetInt(*b, "staticOffsetY", 0);
                m.border.staticWidth = GetInt(*b, "staticWidth", 0);
                m.border.staticHeight = GetInt(*b, "staticHeight", 0);
            }
            c.mirrors.push_back(std::move(m));
        }
    }

    if (auto arr = root["mirrorGroup"].as_array()) {
        for (auto& node : *arr) {
            auto t = node.as_table();
            if (!t) continue;
            MirrorGroupConfig g;
            g.name = GetStr(*t, "name", "");
            g.output = ParseMirrorOutput((*t)["output"].as_table());
            if (auto ms = (*t)["mirrors"].as_array()) {
                for (auto& e : *ms) {
                    if (auto et = e.as_table()) {
                        MirrorGroupItem it;
                        it.mirrorId = GetStr(*et, "mirrorId", "");
                        it.enabled = GetBool(*et, "enabled", true);
                        it.widthPercent = GetFloat(*et, "widthPercent", 1);
                        it.heightPercent = GetFloat(*et, "heightPercent", 1);
                        it.offsetX = GetInt(*et, "offsetX", 0);
                        it.offsetY = GetInt(*et, "offsetY", 0);
                        g.items.push_back(it);
                    }
                }
            }
            c.mirrorGroups.push_back(std::move(g));
        }
    }

    if (auto arr = root["image"].as_array()) {
        for (auto& node : *arr) {
            auto t = node.as_table();
            if (!t) continue;
            ImageConfig im;
            im.name = GetStr(*t, "name", "");
            im.path = TranslateImagePath(im.name, GetStr(*t, "path", ""));
            im.x = GetInt(*t, "x", 0);
            im.y = GetInt(*t, "y", 0);
            im.relativeTo = GetStr(*t, "relativeTo", im.relativeTo);
            im.scale = GetFloat(*t, "scale", 1);
            im.relativeSizing = GetBool(*t, "relativeSizing", true);
            im.width = GetInt(*t, "width", 0);
            im.height = GetInt(*t, "height", 0);
            im.cropTop = GetInt(*t, "crop_top", 0);
            im.cropBottom = GetInt(*t, "crop_bottom", 0);
            im.cropLeft = GetInt(*t, "crop_left", 0);
            im.cropRight = GetInt(*t, "crop_right", 0);
            im.enableColorKey = GetBool(*t, "enableColorKey", false);
            if (auto ck = (*t)["colorKeys"].as_array()) {
                for (auto& e : *ck) {
                    if (auto et = e.as_table()) {
                        ColorKey k;
                        k.color = GetColor(*et, "color", k.color);
                        k.sensitivity = GetFloat(*et, "sensitivity", 0.05f);
                        im.colorKeys.push_back(k);
                    }
                }
            }
            // Pre-v3 configs used a single colorKey/colorKeySensitivity pair.
            if (t->contains("colorKey")) {
                ColorKey k;
                k.color = GetColor(*t, "colorKey", k.color);
                k.sensitivity = GetFloat(*t, "colorKeySensitivity", 0.05f);
                im.colorKeys.push_back(k);
            }
            im.opacity = GetFloat(*t, "opacity", 1);
            if (auto bg = (*t)["background"].as_table()) {
                im.bgEnabled = GetBool(*bg, "enabled", false);
                im.bgColor = GetColor(*bg, "color", im.bgColor);
                im.bgOpacity = GetFloat(*bg, "opacity", 1);
            }
            im.border = ParseBorder((*t)["border"].as_table());
            im.pixelatedScaling = GetBool(*t, "pixelatedScaling", false);
            c.images.push_back(std::move(im));
        }
    }

    if (auto arr = root["hotkey"].as_array()) {
        for (auto& node : *arr) {
            auto t = node.as_table();
            if (!t) continue;
            HotkeyConfig h;
            h.keys = GetIntArray(*t, "keys");
            h.mainMode = GetStr(*t, "mainMode", "");
            h.secondaryMode = GetStr(*t, "secondaryMode", "");
            if (auto alts = (*t)["altSecondaryModes"].as_array()) {
                for (auto& e : *alts) {
                    if (auto et = e.as_table()) h.altSecondaryModes.push_back({GetIntArray(*et, "keys"), GetStr(*et, "mode", "")});
                }
            }
            h.conditions = ParseConditions((*t)["conditions"].as_table());
            h.debounce = GetInt(*t, "debounce", 100);
            h.triggerOnRelease = GetBool(*t, "triggerOnRelease", false);
            h.triggerOnHold = GetBool(*t, "triggerOnHold", false);
            h.blockKeyFromGame = GetBool(*t, "blockKeyFromGame", false);
            h.allowExitToFullscreenRegardlessOfGameState = GetBool(*t, "allowExitToFullscreenRegardlessOfGameState", false);
            if (!h.keys.empty()) c.hotkeys.push_back(std::move(h));
        }
    }

    if (auto arr = root["sensitivityHotkey"].as_array()) {
        for (auto& node : *arr) {
            auto t = node.as_table();
            if (!t) continue;
            SensitivityHotkeyConfig h;
            h.keys = GetIntArray(*t, "keys");
            h.sensitivity = GetFloat(*t, "sensitivity", 1);
            h.separateXY = GetBool(*t, "separateXY", false);
            h.sensitivityX = GetFloat(*t, "sensitivityX", 1);
            h.sensitivityY = GetFloat(*t, "sensitivityY", 1);
            h.toggle = GetBool(*t, "toggle", false);
            h.conditions = ParseConditions((*t)["conditions"].as_table());
            h.debounce = GetInt(*t, "debounce", 100);
            if (!h.keys.empty()) c.sensitivityHotkeys.push_back(std::move(h));
        }
    }

    if (auto nb = root["ninjabrainOverlay"].as_table()) {
        NinjabrainOverlayConfig& n = c.ninjabrain;
        n.enabled = GetBool(*nb, "enabled", n.enabled);
        n.apiBaseUrl = GetStr(*nb, "apiBaseUrl", n.apiBaseUrl);
        n.allowedModes = GetStrArray(*nb, "allowedModes");
        n.alwaysShow = GetBool(*nb, "alwaysShow", n.alwaysShow);
        n.alwaysShowBoat = GetBool(*nb, "alwaysShowBoat", n.alwaysShowBoat);
        n.hideIfStale = GetBool(*nb, "hideIfStale", n.hideIfStale);
        n.hideIfStaleDelaySeconds = GetInt(*nb, "hideIfStaleDelaySeconds", n.hideIfStaleDelaySeconds);
        n.relativeTo = GetStr(*nb, "relativeTo", n.relativeTo);
        n.x = GetInt(*nb, "x", 0);
        n.y = GetInt(*nb, "y", 0);
        n.overlayScale = GetFloat(*nb, "overlayScale", n.overlayScale);
        n.overlayOpacity = GetFloat(*nb, "overlayOpacity", n.overlayOpacity);
        n.bgEnabled = GetBool(*nb, "bgEnabled", n.bgEnabled);
        n.bgOpacity = GetFloat(*nb, "bgOpacity", n.bgOpacity);
        n.bgColor = GetColor(*nb, "bgColor", n.bgColor);
        n.textColor = GetColor(*nb, "textColor", n.textColor);
        n.dataColor = GetColor(*nb, "dataColor", n.dataColor);
        n.titleTextColor = GetColor(*nb, "titleTextColor", n.titleTextColor);
        n.headerFillColor = GetColor(*nb, "headerFillColor", n.headerFillColor);
        n.chromeColor = GetColor(*nb, "chromeColor", n.chromeColor);
        n.dividerColor = GetColor(*nb, "dividerColor", n.dividerColor);
        n.throwsBackgroundColor = GetColor(*nb, "throwsBackgroundColor", n.throwsBackgroundColor);
        n.throwsTextColor = GetColor(*nb, "throwsTextColor", n.throwsTextColor);
        n.certaintyColor = GetColor(*nb, "certaintyColor", n.certaintyColor);
        n.certaintyMidColor = GetColor(*nb, "certaintyMidColor", n.certaintyMidColor);
        n.certaintyLowColor = GetColor(*nb, "certaintyLowColor", n.certaintyLowColor);
        n.coordPositiveColor = GetColor(*nb, "coordPositiveColor", n.coordPositiveColor);
        n.coordNegativeColor = GetColor(*nb, "coordNegativeColor", n.coordNegativeColor);
        n.subpixelPositiveColor = GetColor(*nb, "subpixelPositiveColor", n.subpixelPositiveColor);
        n.subpixelNegativeColor = GetColor(*nb, "subpixelNegativeColor", n.subpixelNegativeColor);
        n.borderColor = GetColor(*nb, "borderColor", n.borderColor);
        n.borderWidth = GetInt(*nb, "borderWidth", 0);
        n.coordsDisplay = GetStr(*nb, "coordsDisplay", n.coordsDisplay);
        n.shownPredictions = std::clamp(GetInt(*nb, "shownPredictions", n.shownPredictions), 1, 10);
        n.eyeThrowRows = std::clamp(GetInt(*nb, "eyeThrowRows", n.eyeThrowRows), 0, 10);
        n.showThrowDetails = GetBool(*nb, "showThrowDetails", n.showThrowDetails);
        n.showTitleBar = GetBool(*nb, "showTitleBar", n.showTitleBar);
        n.showRowStripes = GetBool(*nb, "showRowStripes", n.showRowStripes);
        n.showSeparators = GetBool(*nb, "showSeparators", n.showSeparators);
        n.showDirectionToStronghold = GetBool(*nb, "showDirectionToStronghold", n.showDirectionToStronghold);
        n.showBoatStateInTopBar = GetBool(*nb, "showBoatStateInTopBar", n.showBoatStateInTopBar);
        n.titleText = GetStr(*nb, "titleText", n.titleText);
        n.showOnlyAfterMeasurement = GetBool(*nb, "showOnlyAfterMeasurement", n.showOnlyAfterMeasurement);
        n.hideInMenus = GetBool(*nb, "hideInMenus", n.hideInMenus);
        if (auto cols = (*nb)["columns"].as_array()) {
            for (auto& e : *cols) {
                if (auto et = e.as_table()) {
                    n.columns.push_back({GetStr(*et, "id", ""), GetStr(*et, "header", ""), GetBool(*et, "show", true),
                                         GetInt(*et, "staticWidth", 0)});
                }
            }
        }
    }
    if (c.ninjabrain.columns.empty()) {
        c.ninjabrain.columns = {{"coords", "Location", true, 0}, {"certainty", "%", true, 0}, {"distance", "Dist.", true, 0},
                                {"nether", "Nether", true, 0}, {"angle", "Angle", true, 0}};
    }

    if (auto kr = root["keyRebinds"].as_table()) {
        c.keyRebindsEnabled = GetBool(*kr, "enabled", false);
        c.rebindToggleHotkey = GetIntArray(*kr, "toggleHotkey");
        if (auto rb = (*kr)["rebinds"].as_array()) {
            for (auto& e : *rb) {
                if (auto et = e.as_table()) {
                    KeyRebind r;
                    r.fromKey = GetInt(*et, "fromKey", 0);
                    r.toKey = GetInt(*et, "toKey", 0);
                    r.enabled = GetBool(*et, "enabled", true);
                    r.cursorState = GetStr(*et, "cursorState", "any");
                    if (r.fromKey) c.rebinds.push_back(r);
                }
            }
        }
    }
}

std::string BuildDefaultConfigText(int screenW, int screenH) {
    std::string s(ts_default_toml, ts_default_toml_size);
    if (screenW > 0 && screenH > 0) {
        ReplaceAll(s, "stretch = { enabled = true, height = 1440, width = 2560, x = 0, y = 0 }",
                   "stretch = { enabled = true, height = " + std::to_string(screenH) + ", width = " + std::to_string(screenW) +
                       ", x = 0, y = 0 }");
    }
    ReplaceAll(s, "fontPath = 'c:\\Windows\\Fonts\\Arial.ttf'", "fontPath = ''");
    return s;
}

}  // namespace

void ConfigFromToml(const toml::table& root, Config& out) {
    Config c;
    ParseConfig(root, c);
    out = std::move(c);
}

std::string DefaultConfigText(int screenW, int screenH) { return BuildDefaultConfigText(screenW, screenH); }

bool ReadConfigToml(toml::table& out, std::string& err) {
    try {
        out = toml::parse_file(ConfigPath());
        return true;
    } catch (const toml::parse_error& e) {
        std::ostringstream ss;
        ss << "config.toml line " << e.source().begin.line << ": " << e.description();
        err = ss.str();
    } catch (const std::exception& e) {
        err = e.what();
    }
    return false;
}

bool WriteConfigToml(const toml::table& doc, std::string& err) {
    std::string path = ConfigPath();
    MakeDirs(ConfigDir());
    std::ifstream old(path, std::ios::binary);
    if (old) {
        std::ofstream bak(path + ".bak", std::ios::binary);
        bak << old.rdbuf();
    }
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        f << "# Toolscreen config (saved by the in-game settings editor)\n\n" << doc << "\n";
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

std::string ConfigDir() {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        std::string local = std::string(cwd) + "/toolscreen";
        if (DirExists(local)) return local;
    }
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/toolscreen";
    const char* home = getenv("HOME");
    return std::string(home ? home : "/tmp") + "/.config/toolscreen";
}

std::string ConfigPath() { return ConfigDir() + "/config.toml"; }

bool LoadConfig(Config& out, std::string& err, int screenW, int screenH) {
    std::string path = ConfigPath();
    if (!FileExists(path)) {
        MakeDirs(ConfigDir());
        std::ofstream f(path);
        f << BuildDefaultConfigText(screenW, screenH);
        TsLog("wrote default config to %s", path.c_str());
    }
    try {
        toml::table root = toml::parse_file(path);
        Config c;
        ParseConfig(root, c);
        out = std::move(c);
        err.clear();
        return true;
    } catch (const toml::parse_error& e) {
        std::ostringstream ss;
        ss << "config.toml line " << e.source().begin.line << ": " << e.description();
        err = ss.str();
    } catch (const std::exception& e) {
        err = e.what();
    }
    if (out.modes.empty()) {
        // Fall back to the built-in defaults so the game still works.
        try {
            toml::table root = toml::parse(BuildDefaultConfigText(screenW, screenH));
            Config c;
            ParseConfig(root, c);
            out = std::move(c);
        } catch (...) {
        }
    }
    return false;
}

const ModeConfig* FindMode(const Config& cfg, const std::string& id) {
    for (auto& m : cfg.modes) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

static int ResolveDim(const Dimension& d, int screenAxis, int screenW, int screenH) {
    switch (d.kind) {
    case Dimension::Pixels:
        return d.pixels;
    case Dimension::Relative:
        return static_cast<int>(std::lround(d.relative * static_cast<float>(screenAxis)));
    case Dimension::Expression:
        return EvaluateExpression(d.expr, screenW, screenH, screenAxis);
    }
    return screenAxis;
}

void ResolveModeSize(const Config& cfg, const ModeConfig& mode, int screenW, int screenH, int& w, int& h) {
    const ModeConfig* src = &mode;
    if (mode.id == "Preemptive") {
        if (const ModeConfig* ez = FindMode(cfg, "EyeZoom")) src = ez;
    }
    w = ResolveDim(src->width, screenW, screenW, screenH);
    h = ResolveDim(src->height, screenH, screenW, screenH);
    if (w <= 0) w = screenW;
    if (h <= 0) h = screenH;
    if (mode.id == "Thin" && w < 330) w = 330;
    w = std::clamp(w, 1, 16384);
    h = std::clamp(h, 1, 16384);
}
