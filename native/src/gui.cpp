// In-game settings editor (Ctrl+I), built with Dear ImGui.
// The editor works on the TOML document itself, so saving keeps every key (including settings this port does
// not use yet) and stays compatible with Windows Toolscreen. Every edit is re-parsed into the live config, so
// changes show in game immediately; Save writes config.toml.
#include "config_toml.h"
#include "defaults.h"
#include "gui.h"
#include "imgui_assert.h"

#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_impl_opengl3.h"
#include "../third_party/imgui/imgui_stdlib.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

extern "C" const unsigned char ts_font_ttf[];
extern "C" const unsigned int ts_font_ttf_size;

namespace {

bool g_imguiReady = false;
double g_lastFrame = 0;
float g_uiScale = 1;

toml::table g_doc;
bool g_docLoaded = false;
bool g_dirty = false;
std::string g_status;
double g_statusUntil = 0;

// Key capture for hotkey fields.
toml::table* g_capTable = nullptr;
std::string g_capKey;
std::vector<int> g_capKeys;
bool g_capAnyHeld = false;

void Status(const std::string& s, double secs = 3) {
    g_status = s;
    g_statusUntil = NowSeconds() + secs;
}

// ---------------------------------------------------------------------------
// Document <-> runtime
// ---------------------------------------------------------------------------
void ApplyDoc() {
    ConfigFromToml(g_doc, g_rt.cfg);
    if (!FindMode(g_rt.cfg, g_rt.currentMode)) g_rt.currentMode = g_rt.cfg.defaultMode;
    ApplyModeLayout();
}

void LoadDoc() {
    std::string err;
    toml::table t;
    if (ReadConfigToml(t, err)) {
        g_doc = std::move(t);
        g_docLoaded = true;
        g_dirty = false;
    } else {
        Status("Cannot read config: " + err, 8);
    }
}

void Changed() {
    g_dirty = true;
    ApplyDoc();
}

// ---------------------------------------------------------------------------
// Value helpers over toml tables
// ---------------------------------------------------------------------------
toml::table& SubTable(toml::table& t, const char* key, bool inlineTable = false) {
    if (!t[key].as_table()) {
        toml::table n;
        n.is_inline(inlineTable);
        t.insert_or_assign(key, std::move(n));
    }
    return *t[key].as_table();
}

toml::array& SubArray(toml::table& t, const char* key) {
    if (!t[key].as_array()) t.insert_or_assign(key, toml::array{});
    return *t[key].as_array();
}

int64_t GetI(toml::table& t, const char* key, int64_t def) {
    if (auto v = t[key].value<int64_t>()) return *v;
    if (auto v = t[key].value<double>()) return static_cast<int64_t>(*v);
    return def;
}

double GetF(toml::table& t, const char* key, double def) {
    if (auto v = t[key].value<double>()) return *v;
    return def;
}

bool GetB(toml::table& t, const char* key, bool def) { return t[key].value_or(def); }

std::string GetS(toml::table& t, const char* key, const std::string& def) {
    if (auto v = t[key].value<std::string>()) return *v;
    return def;
}

bool EdInt(const char* label, toml::table& t, const char* key, int64_t def, int minV = INT32_MIN, int maxV = INT32_MAX) {
    int v = static_cast<int>(GetI(t, key, def));
    ImGui::SetNextItemWidth(160 * g_uiScale);
    if (ImGui::InputInt(label, &v)) {
        v = std::clamp(v, minV, maxV);
        t.insert_or_assign(key, static_cast<int64_t>(v));
        Changed();
        return true;
    }
    return false;
}

bool EdFloat(const char* label, toml::table& t, const char* key, double def, float speed = 0.01f, float minV = 0, float maxV = 0,
             const char* fmt = "%.3f") {
    float v = static_cast<float>(GetF(t, key, def));
    ImGui::SetNextItemWidth(160 * g_uiScale);
    if (ImGui::DragFloat(label, &v, speed, minV, maxV, fmt)) {
        t.insert_or_assign(key, static_cast<double>(v));
        Changed();
        return true;
    }
    return false;
}

bool EdBool(const char* label, toml::table& t, const char* key, bool def) {
    bool v = GetB(t, key, def);
    if (ImGui::Checkbox(label, &v)) {
        t.insert_or_assign(key, v);
        Changed();
        return true;
    }
    return false;
}

bool EdStr(const char* label, toml::table& t, const char* key, const std::string& def, float width = 320) {
    std::string v = GetS(t, key, def);
    ImGui::SetNextItemWidth(width * g_uiScale);
    if (ImGui::InputText(label, &v)) {
        t.insert_or_assign(key, v);
        Changed();
        return true;
    }
    return false;
}

bool EdCombo(const char* label, toml::table& t, const char* key, const std::vector<std::string>& options, const std::string& def) {
    std::string cur = GetS(t, key, def);
    bool changed = false;
    ImGui::SetNextItemWidth(220 * g_uiScale);
    if (ImGui::BeginCombo(label, cur.c_str())) {
        for (auto& o : options) {
            if (ImGui::Selectable(o.c_str(), o == cur)) {
                t.insert_or_assign(key, o);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (changed) Changed();
    return changed;
}

// Colours are stored as [r, g, b] (0-255); an optional opacity key holds alpha.
bool EdColor(const char* label, toml::table& t, const char* key, const char* opacityKey = nullptr) {
    float c[4] = {1, 1, 1, 1};
    if (auto a = t[key].as_array()) {
        for (size_t i = 0; i < 3 && i < a->size(); i++) {
            if (auto v = (*a)[i].value<double>()) c[i] = static_cast<float>(*v) / 255.0f;
        }
    }
    if (opacityKey) c[3] = static_cast<float>(GetF(t, opacityKey, 1.0));
    bool changed = opacityKey ? ImGui::ColorEdit4(label, c, ImGuiColorEditFlags_AlphaBar)
                              : ImGui::ColorEdit3(label, c);
    if (changed) {
        toml::array a;
        for (int i = 0; i < 3; i++) a.push_back(static_cast<int64_t>(std::lround(c[i] * 255)));
        t.insert_or_assign(key, std::move(a));
        if (opacityKey) t.insert_or_assign(opacityKey, static_cast<double>(c[3]));
        Changed();
    }
    return changed;
}

std::vector<int> GetKeys(toml::table& t, const char* key) {
    std::vector<int> out;
    if (auto a = t[key].as_array()) {
        for (auto& e : *a) {
            if (auto v = e.value<int64_t>()) out.push_back(static_cast<int>(*v));
        }
    }
    return out;
}

void SetKeys(toml::table& t, const std::string& key, const std::vector<int>& keys) {
    toml::array a;
    for (int k : keys) a.push_back(static_cast<int64_t>(k));
    t.insert_or_assign(key, std::move(a));
}

// A button showing the combination; click it, then press the keys (or mouse buttons) to record.
bool EdKeys(const char* label, toml::table& t, const char* key) {
    ImGui::PushID(label);
    bool capturing = g_capTable == &t && g_capKey == key;
    std::string text = capturing ? (g_capKeys.empty() ? std::string("Press keys...  (Esc cancels)") : VkComboName(g_capKeys) + " ...")
                                 : VkComboName(GetKeys(t, key));
    if (ImGui::Button(text.c_str(), ImVec2(240 * g_uiScale, 0)) && !capturing) {
        g_capTable = &t;
        g_capKey = key;
        g_capKeys.clear();
        g_capAnyHeld = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        SetKeys(t, key, {});
        Changed();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return false;
}

const std::vector<std::string> kAnchors = {"topLeftScreen", "topRightScreen", "bottomLeftScreen", "bottomRightScreen", "centerScreen",
                                          "topCenterScreen", "bottomCenterScreen", "topLeftViewport", "topRightViewport",
                                          "bottomLeftViewport", "bottomRightViewport", "centerViewport", "topCenterViewport",
                                          "bottomCenterViewport"};
const std::vector<std::string> kInputAnchors = {"topLeftViewport", "topRightViewport", "bottomLeftViewport", "bottomRightViewport",
                                               "centerViewport", "topCenterViewport", "bottomCenterViewport", "pieLeft", "pieRight"};

std::vector<std::string> ModeIds() {
    std::vector<std::string> ids;
    for (auto& m : g_rt.cfg.modes) ids.push_back(m.id);
    return ids;
}

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}

// List of tables with add / remove / reorder; `body` draws one entry. Returns the name key's value for headers.
template <typename Body>
void TableList(const char* arrayKey, const char* nameKey, const char* addLabel, toml::table (*makeDefault)(), Body body) {
    toml::array& arr = SubArray(g_doc, arrayKey);
    if (ImGui::Button(addLabel)) {
        arr.push_back(makeDefault());
        Changed();
    }
    int removeAt = -1, moveUp = -1;
    for (size_t i = 0; i < arr.size(); i++) {
        toml::table* t = arr[i].as_table();
        if (!t) continue;
        std::string name = GetS(*t, nameKey, "");
        if (name.empty()) name = "(unnamed)";
        ImGui::PushID(static_cast<int>(i));
        bool open = ImGui::CollapsingHeader((name + "###hdr").c_str());
        // Buttons sit at fixed positions on the right of the header row.
        float right = ImGui::GetWindowContentRegionMax().x;
        if (i > 0) {
            ImGui::SameLine(right - 130 * g_uiScale);
            if (ImGui::SmallButton("Up")) moveUp = static_cast<int>(i);
        }
        ImGui::SameLine(right - 90 * g_uiScale);
        if (ImGui::SmallButton("Delete")) removeAt = static_cast<int>(i);
        if (open) {
            ImGui::Indent();
            body(*t);
            ImGui::Unindent();
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    if (removeAt >= 0) {
        arr.erase(arr.begin() + removeAt);
        Changed();
    } else if (moveUp > 0) {
        auto a = std::move(*arr[moveUp - 1].as_table());
        auto b = std::move(*arr[moveUp].as_table());
        arr.replace(arr.begin() + moveUp - 1, std::move(b));
        arr.replace(arr.begin() + moveUp, std::move(a));
        Changed();
    }
}

// ---------------------------------------------------------------------------
// Defaults for new list entries
// ---------------------------------------------------------------------------
toml::table InlineTable(std::initializer_list<toml::impl::table_init_pair> init) {
    toml::table t(init);
    t.is_inline(true);
    return t;
}

toml::table NewMode() {
    toml::table m{{"id", "NewMode"}, {"width", 400}, {"height", 800}, {"sources", toml::array{}},
                  {"sensitivityOverrideEnabled", false}, {"modeSensitivity", 1.0}};
    m.insert("border", InlineTable({{"enabled", false}, {"color", toml::array{255, 255, 255}}, {"width", 4}, {"radius", 0}}));
    m.insert("stretch", InlineTable({{"enabled", false}, {"x", 0}, {"y", 0}, {"width", 0}, {"height", 0}}));
    m.insert("background", toml::table{{"selectedMode", "color"}, {"color", toml::array{24, 21, 37}}});
    return m;
}

toml::table NewHotkey() {
    toml::table h{{"keys", toml::array{}}, {"mainMode", "Fullscreen"}, {"secondaryMode", "Thin"}, {"debounce", 100},
                  {"triggerOnHold", false}, {"triggerOnRelease", false}, {"altSecondaryModes", toml::array{}}};
    h.insert("conditions", toml::table{{"exclusions", toml::array{114}}, {"gameState", toml::array{}}});
    return h;
}

toml::table NewMirror() {
    toml::table m{{"name", "New Mirror"}, {"captureWidth", 50}, {"captureHeight", 50}, {"colorSensitivity", 0.001},
                  {"colorPassthrough", false}, {"rawOutput", true}, {"fps", 30}, {"opacity", 1.0}};
    toml::array in;
    in.push_back(InlineTable({{"relativeTo", "centerViewport"}, {"x", 0}, {"y", 0}}));
    m.insert("input", std::move(in));
    m.insert("output", InlineTable({{"relativeTo", "centerScreen"}, {"x", 0}, {"y", 0}, {"scale", 2.0}, {"separateScale", false},
                                    {"scaleX", 1.0}, {"scaleY", 1.0}}));
    m.insert("colors", InlineTable({{"targetColors", toml::array{}}, {"output", toml::array{255, 255, 255}},
                                    {"border", toml::array{0, 0, 0}}}));
    m.insert("border", toml::table{{"type", "Dynamic"}, {"dynamicThickness", 2}});
    return m;
}

toml::table NewGroup() {
    toml::table g{{"name", "New Group"}, {"mirrors", toml::array{}}};
    g.insert("output", InlineTable({{"relativeTo", "centerViewport"}, {"x", 0}, {"y", 0}}));
    return g;
}

toml::table NewImage() {
    toml::table i{{"name", "New Image"}, {"path", ""}, {"x", 0}, {"y", 0}, {"relativeTo", "topLeftScreen"}, {"scale", 1.0},
                  {"opacity", 1.0}, {"enableColorKey", false}, {"colorKeys", toml::array{}}, {"pixelatedScaling", false}};
    i.insert("background", InlineTable({{"enabled", false}, {"color", toml::array{0, 0, 0}}, {"opacity", 0.5}}));
    i.insert("border", InlineTable({{"enabled", false}, {"color", toml::array{255, 255, 255}}, {"width", 4}, {"radius", 0}}));
    return i;
}

toml::table NewSensHotkey() {
    toml::table s{{"keys", toml::array{}}, {"sensitivity", 0.5}, {"separateXY", false}, {"sensitivityX", 1.0},
                  {"sensitivityY", 1.0}, {"toggle", true}, {"debounce", 100}};
    s.insert("conditions", toml::table{{"exclusions", toml::array{}}, {"gameState", toml::array{}}});
    return s;
}

// ---------------------------------------------------------------------------
// Editors for shared pieces
// ---------------------------------------------------------------------------
void EdDimension(const char* label, toml::table& t, const char* key) {
    ImGui::PushID(key);
    toml::node* n = t.get(key);
    int kind = 0;  // 0 pixels, 1 fraction, 2 expression
    if (n && n->is_string()) kind = 2;
    else if (n && n->is_floating_point()) kind = 1;
    ImGui::TextUnformatted(label);
    ImGui::SameLine(110 * g_uiScale);
    int newKind = kind;
    ImGui::RadioButton("pixels", &newKind, 0);
    ImGui::SameLine();
    ImGui::RadioButton("fraction of screen", &newKind, 1);
    ImGui::SameLine();
    ImGui::RadioButton("expression", &newKind, 2);
    if (newKind != kind) {
        if (newKind == 0) t.insert_or_assign(key, static_cast<int64_t>(400));
        if (newKind == 1) t.insert_or_assign(key, 0.5);
        if (newKind == 2) t.insert_or_assign(key, std::string(key[0] == 'w' ? "screenWidth" : "screenHeight"));
        Changed();
    }
    ImGui::SameLine();
    if (newKind == 0) EdInt("##v", t, key, 400, 1, 16384);
    else if (newKind == 1) EdFloat("##v", t, key, 0.5, 0.005f, 0.0f, 1.0f);
    else EdStr("##v", t, key, "screenWidth", 260);
    if (newKind == 2) HelpMarker("Variables: screenWidth, screenHeight. Functions: min, max, floor, ceil, round, abs, roundEven.");
    ImGui::PopID();
}

void EdBorderInline(toml::table& parent, const char* key) {
    toml::table& b = SubTable(parent, key, true);
    ImGui::PushID(key);
    EdBool("Border", b, "enabled", false);
    if (GetB(b, "enabled", false)) {
        ImGui::SameLine();
        EdColor("##bc", b, "color");
        EdInt("Width##bw", b, "width", 4, 0, 200);
        ImGui::SameLine();
        EdInt("Corner radius##br", b, "radius", 0, 0, 500);
    }
    ImGui::PopID();
}

void EdOutput(toml::table& parent, bool forGroup) {
    toml::table& o = SubTable(parent, "output", true);
    ImGui::PushID("output");
    EdCombo("Position anchor", o, "relativeTo", kAnchors, "centerScreen");
    EdInt("X##ox", o, "x", 0);
    ImGui::SameLine();
    EdInt("Y##oy", o, "y", 0);
    if (!forGroup) {
        EdBool("Separate X/Y scale", o, "separateScale", false);
        if (GetB(o, "separateScale", false)) {
            EdFloat("Scale X", o, "scaleX", 1.0, 0.05f, 0.05f, 50.0f);
            ImGui::SameLine();
            EdFloat("Scale Y", o, "scaleY", 1.0, 0.05f, 0.05f, 50.0f);
        } else {
            EdFloat("Scale", o, "scale", 1.0, 0.05f, 0.05f, 50.0f);
        }
    }
    ImGui::PopID();
}

void EdConditions(toml::table& parent) {
    toml::table& c = SubTable(parent, "conditions");
    ImGui::PushID("cond");
    EdKeys("Blocked while held (exclusions)", c, "exclusions");
    ImGui::TextUnformatted("Only in game states (none ticked = always):");
    static const char* states[] = {"title", "wall", "waiting", "generating", "inworld,cursor_grabbed", "inworld,cursor_free",
                                   "any,cursor_grabbed", "any,cursor_free"};
    toml::array& gs = SubArray(c, "gameState");
    for (int i = 0; i < 8; i++) {
        bool on = false;
        size_t at = 0;
        for (size_t j = 0; j < gs.size(); j++) {
            if (gs[j].value_or(std::string()) == states[i]) {
                on = true;
                at = j;
            }
        }
        if (i % 4) ImGui::SameLine();
        if (ImGui::Checkbox(states[i], &on)) {
            if (on) gs.push_back(std::string(states[i]));
            else gs.erase(gs.begin() + at);
            Changed();
        }
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------
void TabGeneral() {
    EdCombo("Default mode", g_doc, "defaultMode", ModeIds(), "Fullscreen");
    HelpMarker("The mode Toolscreen returns to when a hotkey toggles back, and on the title screen / wall.");
    EdKeys("Open this editor", g_doc, "guiHotkey");
    EdKeys("Toggle image overlays", g_doc, "imageOverlaysHotkey");
    EdKeys("Toggle Ninjabrain Bot panel", g_doc, "ninjabrainOverlayHotkey");
    ImGui::Separator();
    EdFloat("Mouse sensitivity multiplier", g_doc, "mouseSensitivity", 1.0, 0.005f, 0.01f, 10.0f);
    HelpMarker("Multiplies in-game mouse movement in every mode (1.0 = unchanged). Modes and sensitivity hotkeys can override it.");
    EdInt("FPS limit (0 = off)", g_doc, "fpsLimit", 0, 0, 2000);
    EdCombo("Mirror colour matching", g_doc, "mirrorMatchColorspace", {"Auto", "sRGB", "Linear"}, "Auto");
    int ind = static_cast<int>(GetI(g_doc, "startupIndicatorMode", 1));
    bool showInd = ind != 0;
    if (ImGui::Checkbox("Show the \"Toolscreen loaded\" message at startup", &showInd)) {
        g_doc.insert_or_assign("startupIndicatorMode", static_cast<int64_t>(showInd ? 1 : 0));
        Changed();
    }
    EdStr("Overlay font (path to .ttf, empty = built-in)", g_doc, "fontPath", "");
}

void TabModes() {
    ImGui::TextWrapped("Modes resize the game instantly. Fullscreen should stay screen-sized. Sources are the mirrors, mirror "
                       "groups and images shown while the mode is active.");
    TableList("mode", "id", "Add mode", &NewMode, [](toml::table& m) {
        EdStr("Name (id)", m, "id", "");
        EdDimension("Width", m, "width");
        EdDimension("Height", m, "height");
        EdBorderInline(m, "border");
        toml::table& st = SubTable(m, "stretch", true);
        ImGui::PushID("stretch");
        EdBool("Stretch the game to a rectangle", st, "enabled", false);
        if (GetB(st, "enabled", false)) {
            EdInt("X", st, "x", 0);
            ImGui::SameLine();
            EdInt("Y", st, "y", 0);
            EdInt("Width", st, "width", 0, 0, 16384);
            ImGui::SameLine();
            EdInt("Height", st, "height", 0, 0, 16384);
        }
        ImGui::PopID();

        if (ImGui::TreeNode("Background (around the game)")) {
            toml::table& bg = SubTable(m, "background");
            EdCombo("Type", bg, "selectedMode", {"color", "gradient", "image"}, "color");
            std::string type = GetS(bg, "selectedMode", "color");
            EdColor("Colour", bg, "color");
            if (type == "gradient") {
                EdCombo("Animation", bg, "gradientAnimation", {"None", "Rotate", "Slide", "Wave", "Spiral", "Fade"}, "None");
                EdFloat("Angle (degrees)", bg, "gradientAngle", 0, 1.0f, -360.0f, 360.0f, "%.0f");
                EdFloat("Animation speed", bg, "gradientAnimationSpeed", 1.0, 0.01f, 0.0f, 10.0f);
                EdBool("Colour fade", bg, "gradientColorFade", false);
                toml::array& stops = SubArray(bg, "gradientStops");
                int remove = -1;
                for (size_t i = 0; i < stops.size(); i++) {
                    toml::table* s = stops[i].as_table();
                    if (!s) continue;
                    ImGui::PushID(static_cast<int>(i));
                    EdColor("##sc", *s, "color");
                    ImGui::SameLine();
                    EdFloat("Position", *s, "position", 0, 0.005f, 0.0f, 1.0f);
                    ImGui::SameLine();
                    if (stops.size() > 2 && ImGui::SmallButton("x")) remove = static_cast<int>(i);
                    ImGui::PopID();
                }
                if (remove >= 0) {
                    stops.erase(stops.begin() + remove);
                    Changed();
                }
                if (stops.size() < 8 && ImGui::SmallButton("Add colour stop")) {
                    stops.push_back(InlineTable({{"color", toml::array{255, 255, 255}}, {"position", 1.0}}));
                    Changed();
                }
            }
            if (type == "image") EdStr("Image path", bg, "image", "");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Mouse sensitivity in this mode")) {
            EdBool("Override sensitivity", m, "sensitivityOverrideEnabled", false);
            if (GetB(m, "sensitivityOverrideEnabled", false)) {
                EdBool("Separate X/Y", m, "separateXYSensitivity", false);
                if (GetB(m, "separateXYSensitivity", false)) {
                    EdFloat("X", m, "modeSensitivityX", 1.0, 0.005f, 0.01f, 10.0f);
                    EdFloat("Y", m, "modeSensitivityY", 1.0, 0.005f, 0.01f, 10.0f);
                } else {
                    EdFloat("Sensitivity", m, "modeSensitivity", 1.0, 0.005f, 0.01f, 10.0f);
                }
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Sources (shown in this mode, drawn in order)")) {
            toml::array& src = SubArray(m, "sources");
            int remove = -1;
            for (size_t i = 0; i < src.size(); i++) {
                toml::table* s = src[i].as_table();
                if (!s) continue;
                ImGui::PushID(static_cast<int>(i));
                ImGui::BulletText("%s: %s", GetS(*s, "type", "?").c_str(), GetS(*s, "id", "?").c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) remove = static_cast<int>(i);
                ImGui::PopID();
            }
            if (remove >= 0) {
                src.erase(src.begin() + remove);
                Changed();
            }
            if (ImGui::BeginCombo("Add source", "choose...")) {
                auto add = [&](const char* type, const std::string& id) {
                    if (ImGui::Selectable((std::string(type) + ": " + id).c_str())) {
                        src.push_back(InlineTable({{"type", type}, {"id", id}}));
                        Changed();
                    }
                };
                for (auto& mi : g_rt.cfg.mirrors) add("Mirror", mi.name);
                for (auto& g : g_rt.cfg.mirrorGroups) add("MirrorGroup", g.name);
                for (auto& im : g_rt.cfg.images) add("Image", im.name);
                ImGui::EndCombo();
            }
            ImGui::TreePop();
        }
    });
}

void TabHotkeys() {
    ImGui::TextWrapped("A hotkey switches to its mode, and pressing it again returns to the default mode. For a combination, "
                       "the last key pressed is the trigger and the others must be held.");
    TableList("hotkey", "secondaryMode", "Add hotkey", &NewHotkey, [](toml::table& h) {
        EdKeys("Keys", h, "keys");
        EdCombo("Mode", h, "secondaryMode", ModeIds(), "Thin");
        EdBool("Hold (active only while held)", h, "triggerOnHold", false);
        ImGui::SameLine();
        EdBool("Trigger on release", h, "triggerOnRelease", false);
        EdBool("Hide the key from the game", h, "blockKeyFromGame", false);
        EdBool("Always allow switching back", h, "allowExitToFullscreenRegardlessOfGameState", false);
        EdInt("Debounce (ms)", h, "debounce", 100, 0, 5000);
        if (ImGui::TreeNode("Conditions")) {
            EdConditions(h);
            ImGui::TreePop();
        }
    });
}

void TabMirrors() {
    ImGui::TextWrapped("Mirrors copy part of the game (in game pixels) and show it elsewhere, magnified. With target colours, only "
                       "matching pixels are shown (in the output colour, or their own colour with passthrough). Raw shows the "
                       "region as it is.");
    TableList("mirror", "name", "Add mirror", &NewMirror, [](toml::table& m) {
        EdStr("Name", m, "name", "");
        EdInt("Capture width", m, "captureWidth", 50, 1, 4096);
        ImGui::SameLine();
        EdInt("Capture height", m, "captureHeight", 50, 1, 4096);
        if (ImGui::TreeNode("Capture regions")) {
            toml::array& in = SubArray(m, "input");
            int remove = -1;
            for (size_t i = 0; i < in.size(); i++) {
                toml::table* t = in[i].as_table();
                if (!t) continue;
                ImGui::PushID(static_cast<int>(i));
                EdCombo("Anchor", *t, "relativeTo", kInputAnchors, "centerViewport");
                EdInt("X", *t, "x", 0);
                ImGui::SameLine();
                EdInt("Y", *t, "y", 0);
                ImGui::SameLine();
                if (in.size() > 1 && ImGui::SmallButton("Remove")) remove = static_cast<int>(i);
                ImGui::PopID();
            }
            if (remove >= 0) {
                in.erase(in.begin() + remove);
                Changed();
            }
            if (ImGui::SmallButton("Add region")) {
                in.push_back(InlineTable({{"relativeTo", "centerViewport"}, {"x", 0}, {"y", 0}}));
                Changed();
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Output")) {
            EdOutput(m, false);
            EdFloat("Opacity", m, "opacity", 1.0, 0.01f, 0.0f, 1.0f);
            EdInt("Updates per second (0 = every frame)", m, "fps", 30, 0, 1000);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Colours")) {
            EdBool("Raw (copy the region as it is)", m, "rawOutput", false);
            toml::table& col = SubTable(m, "colors", true);
            if (!GetB(m, "rawOutput", false)) {
                EdBool("Passthrough (keep matched pixels' own colour)", m, "colorPassthrough", false);
                EdFloat("Match sensitivity", m, "colorSensitivity", 0.001, 0.0005f, 0.0f, 1.0f, "%.4f");
                toml::array& tc = SubArray(col, "targetColors");
                int remove = -1;
                for (size_t i = 0; i < tc.size(); i++) {
                    ImGui::PushID(static_cast<int>(i));
                    float c[3] = {0, 0, 0};
                    if (auto a = tc[i].as_array()) {
                        for (size_t k = 0; k < 3 && k < a->size(); k++) c[k] = static_cast<float>((*a)[k].value_or(0.0)) / 255.0f;
                    }
                    if (ImGui::ColorEdit3("Target", c)) {
                        tc.replace(tc.begin() + i, toml::array{static_cast<int64_t>(std::lround(c[0] * 255)),
                                                                static_cast<int64_t>(std::lround(c[1] * 255)),
                                                                static_cast<int64_t>(std::lround(c[2] * 255))});
                        Changed();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
                    ImGui::PopID();
                }
                if (remove >= 0) {
                    tc.erase(tc.begin() + remove);
                    Changed();
                }
                if (tc.size() < 8 && ImGui::SmallButton("Add target colour")) {
                    tc.push_back(toml::array{255, 255, 255});
                    Changed();
                }
                if (!GetB(m, "colorPassthrough", false)) EdColor("Output colour", col, "output");
            }
            EdColor("Border colour", col, "border");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Border")) {
            toml::table& b = SubTable(m, "border");
            EdCombo("Type", b, "type", {"Dynamic", "Static"}, "Dynamic");
            if (GetS(b, "type", "Dynamic") == "Dynamic") {
                EdInt("Outline thickness", b, "dynamicThickness", 1, 0, 50);
            } else {
                EdCombo("Shape", b, "staticShape", {"Rectangle", "Circle"}, "Rectangle");
                EdColor("Colour", b, "staticColor");
                EdInt("Thickness", b, "staticThickness", 2, 0, 100);
                EdInt("Corner radius", b, "staticRadius", 0, 0, 1000);
                EdInt("Width (0 = mirror size)", b, "staticWidth", 0, 0, 8192);
                ImGui::SameLine();
                EdInt("Height (0 = mirror size)", b, "staticHeight", 0, 0, 8192);
                EdInt("Offset X", b, "staticOffsetX", 0);
                ImGui::SameLine();
                EdInt("Offset Y", b, "staticOffsetY", 0);
            }
            ImGui::TreePop();
        }
    });
}

void TabGroups() {
    ImGui::TextWrapped("A mirror group places several mirrors together; each item is offset from the group position and scaled.");
    TableList("mirrorGroup", "name", "Add mirror group", &NewGroup, [](toml::table& g) {
        EdStr("Name", g, "name", "");
        EdOutput(g, true);
        toml::array& items = SubArray(g, "mirrors");
        int remove = -1;
        for (size_t i = 0; i < items.size(); i++) {
            toml::table* it = items[i].as_table();
            if (!it) continue;
            ImGui::PushID(static_cast<int>(i));
            ImGui::Separator();
            std::vector<std::string> names;
            for (auto& mi : g_rt.cfg.mirrors) names.push_back(mi.name);
            EdCombo("Mirror", *it, "mirrorId", names, "");
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) remove = static_cast<int>(i);
            EdBool("Enabled", *it, "enabled", true);
            EdInt("Offset X", *it, "offsetX", 0);
            ImGui::SameLine();
            EdInt("Offset Y", *it, "offsetY", 0);
            EdFloat("Width scale", *it, "widthPercent", 1.0, 0.01f, 0.01f, 50.0f);
            ImGui::SameLine();
            EdFloat("Height scale", *it, "heightPercent", 1.0, 0.01f, 0.01f, 50.0f);
            ImGui::PopID();
        }
        if (remove >= 0) {
            items.erase(items.begin() + remove);
            Changed();
        }
        if (ImGui::SmallButton("Add mirror to group")) {
            items.push_back(InlineTable({{"mirrorId", g_rt.cfg.mirrors.empty() ? std::string() : g_rt.cfg.mirrors[0].name},
                                         {"enabled", true}, {"offsetX", 0}, {"offsetY", 0}, {"widthPercent", 1.0},
                                         {"heightPercent", 1.0}}));
            Changed();
        }
    });
}

void TabImages() {
    ImGui::TextWrapped("Image overlays show a picture file. The image is reloaded automatically when the file changes "
                       "(for example Ninjabrain Bot's /tmp/nb-overlay.png).");
    TableList("image", "name", "Add image", &NewImage, [](toml::table& im) {
        EdStr("Name", im, "name", "");
        EdStr("File", im, "path", "");
        EdCombo("Anchor", im, "relativeTo", kAnchors, "topLeftScreen");
        EdInt("X", im, "x", 0);
        ImGui::SameLine();
        EdInt("Y", im, "y", 0);
        EdFloat("Scale", im, "scale", 1.0, 0.01f, 0.01f, 50.0f);
        EdFloat("Opacity", im, "opacity", 1.0, 0.01f, 0.0f, 1.0f);
        EdBool("Pixelated scaling", im, "pixelatedScaling", false);
        if (ImGui::TreeNode("Crop")) {
            EdInt("Top", im, "crop_top", 0, 0, 8192);
            ImGui::SameLine();
            EdInt("Bottom", im, "crop_bottom", 0, 0, 8192);
            EdInt("Left", im, "crop_left", 0, 0, 8192);
            ImGui::SameLine();
            EdInt("Right", im, "crop_right", 0, 0, 8192);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Colour key (make a colour transparent)")) {
            EdBool("Enabled", im, "enableColorKey", false);
            toml::array& keys = SubArray(im, "colorKeys");
            int remove = -1;
            for (size_t i = 0; i < keys.size(); i++) {
                toml::table* k = keys[i].as_table();
                if (!k) continue;
                ImGui::PushID(static_cast<int>(i));
                EdColor("Colour", *k, "color");
                ImGui::SameLine();
                EdFloat("Sensitivity", *k, "sensitivity", 0.05, 0.002f, 0.0f, 1.0f);
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
                ImGui::PopID();
            }
            if (remove >= 0) {
                keys.erase(keys.begin() + remove);
                Changed();
            }
            if (keys.size() < 8 && ImGui::SmallButton("Add colour key")) {
                keys.push_back(InlineTable({{"color", toml::array{0, 0, 0}}, {"sensitivity", 0.05}}));
                Changed();
            }
            ImGui::TreePop();
        }
        toml::table& bg = SubTable(im, "background", true);
        ImGui::PushID("imgbg");
        EdBool("Background", bg, "enabled", false);
        if (GetB(bg, "enabled", false)) {
            ImGui::SameLine();
            EdColor("##c", bg, "color");
            ImGui::SameLine();
            EdFloat("Opacity##o", bg, "opacity", 0.5, 0.01f, 0.0f, 1.0f);
        }
        ImGui::PopID();
        EdBorderInline(im, "border");
    });
}

void TabEyeZoom() {
    toml::table& ez = SubTable(g_doc, "eyezoom");
    ImGui::TextWrapped("EyeZoom magnifies a narrow strip around the crosshair while the EyeZoom mode is active.");
    EdInt("Captured width (game pixels)", ez, "cloneWidth", 30, 2, 400);
    EdInt("Captured height (game pixels)", ez, "cloneHeight", 1300, 10, 16384);
    EdInt("Numbered boxes per side", ez, "overlayWidth", 12, 0, 200);
    EdBool("Custom position and size", ez, "useCustomSizePosition", false);
    if (GetB(ez, "useCustomSizePosition", false)) {
        EdInt("X", ez, "positionX", 0);
        ImGui::SameLine();
        EdInt("Y", ez, "positionY", 0);
        EdInt("Width", ez, "zoomAreaWidth", 400, 1, 8192);
        ImGui::SameLine();
        EdInt("Height", ez, "zoomAreaHeight", 800, 1, 8192);
    }
    EdCombo("Number size", ez, "fontSizeMode", {"auto", "perSquareAuto", "manual"}, "auto");
    if (GetS(ez, "fontSizeMode", "auto") == "manual") EdInt("Font size", ez, "textFontSize", 42, 4, 400);
    EdBool("Box height follows the font", ez, "linkRectToFont", true);
    if (!GetB(ez, "linkRectToFont", true)) EdInt("Box height", ez, "rectHeight", 50, 1, 1000);
    EdColor("Box colour 1", ez, "gridColor1", "gridColor1Opacity");
    EdColor("Box colour 2", ez, "gridColor2", "gridColor2Opacity");
    EdColor("Centre line", ez, "centerLineColor", "centerLineColorOpacity");
    EdColor("Numbers", ez, "textColor", "textColorOpacity");
    EdStr("Number font (.ttf path, empty = default)", ez, "textFontPath", "");
}

void TabNinjabrain() {
    toml::table& nb = SubTable(g_doc, "ninjabrainOverlay");
    NinjabrainData d = NinjabrainSnapshot();
    ImGui::TextWrapped("Shows Ninjabrain Bot's results in game. In Ninjabrain Bot, tick Settings > Advanced > \"Enable API (starts "
                       "HTTP server)\".");
    ImGui::Text("API status: %s", g_rt.cfg.ninjabrain.enabled ? (d.connected ? "connected" : "not connected") : "panel off");
    EdBool("Show the Ninjabrain Bot panel", nb, "enabled", false);
    EdBool("Hidden until the first F3+C in the current world", nb, "showOnlyAfterMeasurement", false);
    EdBool("Hidden on the title / MCSR Ranked screens, wall and while generating", nb, "hideInMenus", false);
    EdBool("Show when empty (once connected)", nb, "alwaysShow", true);
    EdBool("Hide after a while without updates", nb, "hideIfStale", false);
    if (GetB(nb, "hideIfStale", false)) EdInt("Seconds", nb, "hideIfStaleDelaySeconds", 30, 1, 3600);
    ImGui::Separator();
    EdCombo("Anchor", nb, "relativeTo", kAnchors, "topLeftScreen");
    EdInt("X", nb, "x", 0);
    ImGui::SameLine();
    EdInt("Y", nb, "y", 0);
    EdFloat("Size", nb, "overlayScale", 0.3, 0.005f, 0.05f, 2.0f);
    EdFloat("Opacity", nb, "overlayOpacity", 1.0, 0.01f, 0.0f, 1.0f);
    EdInt("Predictions shown", nb, "shownPredictions", 5, 1, 10);
    EdInt("Eye throw rows", nb, "eyeThrowRows", 3, 0, 10);
    EdBool("Show eye throws", nb, "showThrowDetails", true);
    EdBool("Show turn direction in the angle column", nb, "showDirectionToStronghold", true);
    EdBool("Title bar", nb, "showTitleBar", false);
    EdCombo("Coordinates", nb, "coordsDisplay", {"block", "chunk"}, "block");
    if (ImGui::TreeNode("Colours")) {
        EdBool("Background", nb, "bgEnabled", true);
        EdColor("Background colour", nb, "bgColor");
        EdColor("Text", nb, "textColor");
        EdColor("Values", nb, "dataColor");
        EdColor("Header", nb, "headerFillColor");
        EdColor("High certainty", nb, "certaintyColor");
        EdColor("Medium certainty", nb, "certaintyMidColor");
        EdColor("Low certainty", nb, "certaintyLowColor");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Columns")) {
        toml::array& cols = SubArray(nb, "columns");
        for (size_t i = 0; i < cols.size(); i++) {
            toml::table* c = cols[i].as_table();
            if (!c) continue;
            ImGui::PushID(static_cast<int>(i));
            EdBool(GetS(*c, "id", "?").c_str(), *c, "show", true);
            ImGui::SameLine(160 * g_uiScale);
            EdStr("Header", *c, "header", "", 160);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

void TabInput() {
    ImGui::TextUnformatted("Sensitivity hotkeys");
    ImGui::TextWrapped("Temporarily change mouse sensitivity while a key is held (or toggle it).");
    TableList("sensitivityHotkey", "", "Add sensitivity hotkey", &NewSensHotkey, [](toml::table& s) {
        EdKeys("Keys", s, "keys");
        EdBool("Toggle (otherwise hold)", s, "toggle", true);
        EdBool("Separate X/Y", s, "separateXY", false);
        if (GetB(s, "separateXY", false)) {
            EdFloat("X", s, "sensitivityX", 1.0, 0.005f, 0.01f, 10.0f);
            EdFloat("Y", s, "sensitivityY", 1.0, 0.005f, 0.01f, 10.0f);
        } else {
            EdFloat("Sensitivity", s, "sensitivity", 0.5, 0.005f, 0.01f, 10.0f);
        }
        if (ImGui::TreeNode("Conditions")) {
            EdConditions(s);
            ImGui::TreePop();
        }
    });
    ImGui::Separator();
    ImGui::TextUnformatted("Key rebinds");
    toml::table& kr = SubTable(g_doc, "keyRebinds");
    EdBool("Enable key rebinds", kr, "enabled", false);
    EdKeys("Toggle rebinds on/off", kr, "toggleHotkey");
    toml::array& rb = SubArray(kr, "rebinds");
    int remove = -1;
    for (size_t i = 0; i < rb.size(); i++) {
        toml::table* r = rb[i].as_table();
        if (!r) continue;
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator();
        ImGui::Text("%s  ->  %s", VkComboName({static_cast<int>(GetI(*r, "fromKey", 0))}).c_str(),
                    VkComboName({static_cast<int>(GetI(*r, "toKey", 0))}).c_str());
        EdInt("From (key code)", *r, "fromKey", 0, 0, 255);
        ImGui::SameLine();
        EdInt("To (key code)", *r, "toKey", 0, 0, 255);
        EdBool("Enabled", *r, "enabled", true);
        ImGui::SameLine();
        EdCombo("When", *r, "cursorState", {"any", "cursor_grabbed", "cursor_free"}, "any");
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) remove = static_cast<int>(i);
        ImGui::PopID();
    }
    if (remove >= 0) {
        rb.erase(rb.begin() + remove);
        Changed();
    }
    if (ImGui::SmallButton("Add rebind")) {
        rb.push_back(InlineTable({{"fromKey", 0}, {"toKey", 0}, {"enabled", true}, {"cursorState", "any"}}));
        Changed();
    }
    ImGui::TextDisabled("Key codes: A-Z = 65-90, 0-9 = 48-57, F1 = 112, Space = 32, LShift = 160, LCtrl = 162, LAlt = 164.");
}

void TabStatus() {
    const Runtime& rt = g_rt;
    std::string state = GetGameState();
    ImGui::Text("Toolscreen %s for Linux", TS_VERSION);
    ImGui::Text("Config: %s", ConfigPath().c_str());
    ImGui::Text("Mode: %s   Game %dx%d   Screen %dx%d", rt.currentMode.c_str(), rt.gameW, rt.gameH, rt.screenW, rt.screenH);
    ImGui::Text("Game state: %s", state.empty() ? "(no wpstateout.txt - install the state-output mod)" : state.c_str());
    if (rt.cfg.ninjabrain.enabled) ImGui::Text("Ninjabrain Bot API: %s", NinjabrainSnapshot().connected ? "connected" : "not connected");
    if (!rt.cfgError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Config error: %s", rt.cfgError.c_str());
    ImGui::Separator();
    ImGui::TextUnformatted("Hotkeys:");
    for (auto& h : rt.cfg.hotkeys) ImGui::BulletText("%s  ->  %s", VkComboName(h.keys).c_str(), h.secondaryMode.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("Saving from this editor rewrites config.toml without its comments (the previous file is kept as "
                       "config.toml.bak). Editing the file by hand also works: it reloads automatically when saved.");
}

// Defaults: loads the recommended settings the installer built for this computer (recommended.toml).
void TabDefaults() {
    static toml::table rec;
    static bool recLoaded = false;
    static std::string recError;
    static double lastLoad = -10;
    std::string path = ConfigDir() + "/recommended.toml";
    if (NowSeconds() - lastLoad > 2.0) {
        lastLoad = NowSeconds();
        try {
            rec = toml::parse_file(path);
            recLoaded = true;
            recError.clear();
        } catch (const toml::parse_error& e) {
            recLoaded = false;
            recError = std::string(e.description());
        } catch (...) {
            recLoaded = false;
            recError.clear();
        }
    }
    ImGui::TextWrapped("Recommended settings for this computer, built once by the installer from your Minecraft mouse "
                       "sensitivity and GUI scale and your screen resolution. The measuring FOV is recommended from the "
                       "resolution, not taken from your gameplay FOV.");
    ImGui::TextDisabled("%s", path.c_str());
    if (!recLoaded) {
        ImGui::Separator();
        if (!recError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "recommended.toml has an error: %s", recError.c_str());
        else ImGui::TextWrapped("No recommended settings yet. Close the launcher and run the Toolscreen installer jar again "
                                "(Reinstall / Update): it builds them for this computer.");
        return;
    }
    toml::table* info = rec["recommendedInfo"].as_table();
    toml::table* nbRec = rec["recommendedNinjabrainBot"].as_table();
    if (info) {
        ImGui::Separator();
        auto row = [&](const char* label, const char* key) {
            toml::node* n = info->get(key);
            std::string v = n ? (n->is_string() ? n->value_or(std::string()) : std::to_string(n->value_or(int64_t(0)))) : "?";
            ImGui::TextUnformatted(label);
            ImGui::SameLine(190 * g_uiScale);
            ImGui::TextUnformatted(v.c_str());
        };
        row("Built on:", "generatedOn");
        row("Instance:", "instance");
        row("Screen:", "screen");
        row("Mouse sensitivity:", "minecraftSensitivity");
        row("GUI scale:", "guiScale");
        ImGui::Separator();
        ImGui::TextUnformatted("Measuring FOV (set it in Minecraft only while measuring; not your gameplay FOV)");
        row("With EyeZoom:", "measuringFovEyeZoom");
        HelpMarker("Ninjabrain Bot's \"Tall resolution\" adjustment works out a pixel's angle with tan(15 degrees), "
                   "so measuring with EyeZoom must be done at FOV 30 on any monitor.");
        int64_t pt = (*info)["measuringFovPerfectTravel"].value_or(int64_t(0));
        if (pt > 0) {
            row("Perfect Travel:", "measuringFovPerfectTravel");
        } else {
            ImGui::TextUnformatted("Perfect Travel:");
            ImGui::SameLine(190 * g_uiScale);
            ImGui::TextUnformatted("not listed for this resolution (find it with the Perfect Travel document)");
        }
        HelpMarker("Perfect Travel is subpixel measuring without EyeZoom: FOV 31 at 1920x1080 or 2560x1440 fullscreen, "
                   "30 at 1920x1027 or 2560x1387 windowed.");
        McOptions now;
        if (ReadMinecraftOptions(now)) {
            std::string saved = (*info)["minecraftSensitivity"].value_or(std::string());
            if (!saved.empty() && std::fabs(atof(saved.c_str()) - now.sensitivity) > 1e-6) {
                ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1),
                                   "Your Minecraft sensitivity is now %s. Re-run the installer to rebuild these settings.",
                                   now.sensitivityText.c_str());
            }
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Toolscreen");
    ImGui::BulletText("Toolscreen's default modes and mirrors, fitted to this screen; P shows/hides the Ninjabrain Bot panel");
    ImGui::BulletText("Ninjabrain Bot panel on, hidden until your first F3+C and in menus; OneShot mode on O");
    if (ImGui::Button("Load recommended Toolscreen settings")) {
        toml::table doc = rec;
        doc.erase("recommendedInfo");
        doc.erase("recommendedNinjabrainBot");
        g_doc = std::move(doc);
        Changed();
        Status("Recommended settings loaded - press Save to keep them", 6);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(applies now; Save keeps them)");

    ImGui::Separator();
    ImGui::TextUnformatted("Ninjabrain Bot");
    ImGui::BulletText("= adds one pixel to the last angle, - removes one (tall resolution 16384)");
    ImGui::BulletText("Sensitivity set to your Minecraft sensitivity; API on for the in-game panel");
    ImGui::BulletText("Boat measurements: blue boat, allowable error 0.03, boat standard deviation 0.007");
    if (!nbRec) {
        ImGui::TextDisabled("(no Ninjabrain Bot section in recommended.toml)");
        return;
    }
    bool running = NinjabrainRunning();
    if (running) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Close Ninjabrain Bot first: it saves its own settings and would undo these.");
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Apply recommended Ninjabrain Bot settings")) {
        auto current = ReadNinjabrainPrefs();
        std::map<std::string, std::string> changes;
        for (auto& kv : *nbRec) {
            std::string key(kv.first.str());
            if (key == "sigma" && current.count("sigma")) continue;  // keep a calibrated standard deviation
            if (auto v = kv.second.value<std::string>()) changes[key] = *v;
        }
        std::string err;
        if (WriteNinjabrainPrefs(changes, err)) Status("Ninjabrain Bot settings written - start Ninjabrain Bot again", 6);
        else Status("Could not write Ninjabrain Bot settings: " + err, 8);
    }
    ImGui::EndDisabled();
}

void DrawEditor() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 size(std::min(io.DisplaySize.x * 0.9f, 980 * g_uiScale), std::min(io.DisplaySize.y * 0.88f, 760 * g_uiScale));
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    std::string title = std::string("Toolscreen ") + TS_VERSION + " Settings" + (g_dirty ? "  (unsaved changes)" : "") + "###tsgui";
    bool open = true;
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (!open) g_rt.guiOpen = false;

    if (ImGui::Button("Save")) {
        std::string err;
        if (WriteConfigToml(g_doc, err)) {
            g_dirty = false;
            Status("Saved to " + ConfigPath());
        } else {
            Status("Save failed: " + err, 8);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert to file")) {
        LoadDoc();
        ApplyDoc();
        Status("Reloaded config.toml");
    }
    ImGui::SameLine();
    if (ImGui::Button("Open config folder")) TsOpenPath(ConfigDir());
    ImGui::SameLine();
    if (ImGui::Button("Edit config.toml")) TsOpenPath(ConfigPath());
    ImGui::SameLine();
    if (ImGui::Button("Close")) g_rt.guiOpen = false;
    ImGui::SameLine();
    if (NowSeconds() < g_statusUntil) ImGui::TextUnformatted(g_status.c_str());
    else ImGui::TextDisabled("Changes apply immediately; Save writes them to config.toml.");
    ImGui::Separator();

    if (ImGui::BeginTabBar("tabs")) {
        struct Tab {
            const char* name;
            void (*fn)();
        } tabs[] = {{"Status", TabStatus},   {"Defaults", TabDefaults}, {"General", TabGeneral}, {"Modes", TabModes},
                    {"Hotkeys", TabHotkeys},
                    {"Mirrors", TabMirrors}, {"Mirror groups", TabGroups}, {"Images", TabImages}, {"EyeZoom", TabEyeZoom},
                    {"Ninjabrain Bot", TabNinjabrain}, {"Mouse & keys", TabInput}};
        for (auto& t : tabs) {
            if (ImGui::BeginTabItem(t.name)) {
                ImGui::BeginChild("tabbody");
                t.fn();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Input mapping
// ---------------------------------------------------------------------------
ImGuiKey GlfwToImGuiKey(int k) {
    if (k >= 65 && k <= 90) return static_cast<ImGuiKey>(ImGuiKey_A + (k - 65));
    if (k >= 48 && k <= 57) return static_cast<ImGuiKey>(ImGuiKey_0 + (k - 48));
    if (k >= 290 && k <= 301) return static_cast<ImGuiKey>(ImGuiKey_F1 + (k - 290));
    if (k >= 320 && k <= 329) return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (k - 320));
    switch (k) {
    case 32: return ImGuiKey_Space;
    case 39: return ImGuiKey_Apostrophe;
    case 44: return ImGuiKey_Comma;
    case 45: return ImGuiKey_Minus;
    case 46: return ImGuiKey_Period;
    case 47: return ImGuiKey_Slash;
    case 59: return ImGuiKey_Semicolon;
    case 61: return ImGuiKey_Equal;
    case 91: return ImGuiKey_LeftBracket;
    case 92: return ImGuiKey_Backslash;
    case 93: return ImGuiKey_RightBracket;
    case 96: return ImGuiKey_GraveAccent;
    case 256: return ImGuiKey_Escape;
    case 257: return ImGuiKey_Enter;
    case 258: return ImGuiKey_Tab;
    case 259: return ImGuiKey_Backspace;
    case 260: return ImGuiKey_Insert;
    case 261: return ImGuiKey_Delete;
    case 262: return ImGuiKey_RightArrow;
    case 263: return ImGuiKey_LeftArrow;
    case 264: return ImGuiKey_DownArrow;
    case 265: return ImGuiKey_UpArrow;
    case 266: return ImGuiKey_PageUp;
    case 267: return ImGuiKey_PageDown;
    case 268: return ImGuiKey_Home;
    case 269: return ImGuiKey_End;
    case 330: return ImGuiKey_KeypadDecimal;
    case 331: return ImGuiKey_KeypadDivide;
    case 332: return ImGuiKey_KeypadMultiply;
    case 333: return ImGuiKey_KeypadSubtract;
    case 334: return ImGuiKey_KeypadAdd;
    case 335: return ImGuiKey_KeypadEnter;
    case 340: return ImGuiKey_LeftShift;
    case 341: return ImGuiKey_LeftCtrl;
    case 342: return ImGuiKey_LeftAlt;
    case 343: return ImGuiKey_LeftSuper;
    case 344: return ImGuiKey_RightShift;
    case 345: return ImGuiKey_RightCtrl;
    case 346: return ImGuiKey_RightAlt;
    case 347: return ImGuiKey_RightSuper;
    default: return ImGuiKey_None;
    }
}

void InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = [](ImGuiContext*) { return TsGetClipboard(); };
    pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { TsSetClipboard(text); };

    g_uiScale = std::max(1.0f, g_rt.screenH / 1080.0f);
    ImFontConfig fc;
    fc.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(ts_font_ttf), static_cast<int>(ts_font_ttf_size), 19.0f * g_uiScale, &fc);

    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 6;
    st.FrameRounding = 4;
    st.GrabRounding = 4;
    st.ScaleAllSizes(g_uiScale);
    ImVec4 purple(0.48f, 0.08f, 0.64f, 1.0f);
    st.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.06f, 0.42f, 1.0f);
    st.Colors[ImGuiCol_Header] = ImVec4(0.36f, 0.10f, 0.50f, 0.65f);
    st.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.14f, 0.62f, 0.85f);
    st.Colors[ImGuiCol_Button] = ImVec4(0.36f, 0.10f, 0.50f, 0.70f);
    st.Colors[ImGuiCol_ButtonHovered] = purple;
    st.Colors[ImGuiCol_Tab] = ImVec4(0.26f, 0.08f, 0.36f, 1.0f);
    st.Colors[ImGuiCol_TabSelected] = purple;
    st.Colors[ImGuiCol_TabHovered] = ImVec4(0.58f, 0.18f, 0.76f, 1.0f);
    st.Colors[ImGuiCol_CheckMark] = ImVec4(0.85f, 0.6f, 1.0f, 1.0f);
    st.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.14f, 0.24f, 1.0f);
    st.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.06f, 0.10f, 0.96f);

    bool ok = ImGui_ImplOpenGL3_Init("#version 330");
    TsLog("settings editor: GUI renderer %s", ok ? "started" : "FAILED to start");
    g_imguiReady = true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
void TsImGuiAssert(const char* expr, const char* file, int line) {
    static int reported = 0;
    if (reported++ < 20) TsLog("settings editor: GUI check failed: %s (%s:%d)", expr, file, line);
}

bool GuiCapturingKeys() { return g_capTable != nullptr; }

bool GuiWantsTextInput() { return g_imguiReady && ImGui::GetIO().WantTextInput; }

void GuiCaptureKey(int vk, bool pressed) {
    if (!g_capTable) return;
    if (pressed) {
        if (vk == 0x1B && g_capKeys.empty()) {  // Escape cancels
            g_capTable = nullptr;
            return;
        }
        if (std::find(g_capKeys.begin(), g_capKeys.end(), vk) == g_capKeys.end()) g_capKeys.push_back(vk);
        g_capAnyHeld = true;
        return;
    }
    // Finish once every recorded key is released.
    bool anyHeld = false;
    for (int k : g_capKeys) {
        if (k != vk && IsVkHeld(k)) anyHeld = true;
    }
    if (!anyHeld && g_capAnyHeld && !g_capKeys.empty()) {
        SetKeys(*g_capTable, g_capKey, g_capKeys);
        g_capTable = nullptr;
        Changed();
    }
}

void GuiOnKey(int glfwKey, int action) {
    if (!g_imguiReady) return;
    ImGuiIO& io = ImGui::GetIO();
    bool down = action != 0;
    if (glfwKey == 340 || glfwKey == 344) io.AddKeyEvent(ImGuiMod_Shift, down);
    if (glfwKey == 341 || glfwKey == 345) io.AddKeyEvent(ImGuiMod_Ctrl, down);
    if (glfwKey == 342 || glfwKey == 346) io.AddKeyEvent(ImGuiMod_Alt, down);
    if (glfwKey == 343 || glfwKey == 347) io.AddKeyEvent(ImGuiMod_Super, down);
    ImGuiKey k = GlfwToImGuiKey(glfwKey);
    if (k != ImGuiKey_None) io.AddKeyEvent(k, down);
}

void GuiOnChar(unsigned int c) {
    if (g_imguiReady && !g_capTable) ImGui::GetIO().AddInputCharacter(c);
}

void GuiOnMouseButton(int button, int action) {
    if (g_imguiReady && button >= 0 && button < 5) ImGui::GetIO().AddMouseButtonEvent(button, action != 0);
}

void GuiOnCursor(double fbX, double fbY) {
    if (g_imguiReady) ImGui::GetIO().AddMousePosEvent(static_cast<float>(fbX), static_cast<float>(fbY));
}

void GuiOnScroll(double dx, double dy) {
    if (g_imguiReady) ImGui::GetIO().AddMouseWheelEvent(static_cast<float>(dx), static_cast<float>(dy));
}

void GuiOnOpened() {
    TsLog("settings editor opened (screen %dx%d)", g_rt.screenW, g_rt.screenH);
    LoadDoc();
    g_capTable = nullptr;
}

void GuiRender() {
    if (!g_imguiReady) InitImGui();
    if (!g_docLoaded) LoadDoc();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(g_rt.screenW), static_cast<float>(g_rt.screenH));
    double now = NowSeconds();
    io.DeltaTime = g_lastFrame > 0 ? static_cast<float>(std::max(1e-4, now - g_lastFrame)) : 1.0f / 60.0f;
    g_lastFrame = now;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    DrawEditor();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
