// Input: hotkeys, sensitivity overrides, key rebinding and the Ctrl+I panel.
// Config files store Windows virtual-key codes (VK_*), so GLFW key codes are translated to VK codes here.
#include "ts.h"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <map>

namespace {

constexpr int GLFW_RELEASE = 0, GLFW_PRESS = 1;

bool g_held[512];

struct HotkeyRuntime {
    double lastFire = -1e9;
    bool releaseArmed = false;  // triggerOnRelease: armed on press, cancelled by any other key
    bool holdActive = false;    // triggerOnHold: mode was switched on press
    int altIndex = -1;          // -1 = the configured secondaryMode, otherwise altSecondaryModes[altIndex]
};

std::vector<HotkeyRuntime> g_hotkeyRt;
std::vector<double> g_sensLastFire;
std::vector<bool> g_sensToggled;
size_t g_cfgGeneration = 0;
bool g_rebindsActive = true;
std::string g_lastGameState;

const std::map<int, int>& GlfwToVkMap() {
    static std::map<int, int> m;
    if (!m.empty()) return m;
    m[32] = 0x20;
    m[39] = 0xDE;
    m[44] = 0xBC;
    m[45] = 0xBD;
    m[46] = 0xBE;
    m[47] = 0xBF;
    for (int k = 48; k <= 57; k++) m[k] = k;
    m[59] = 0xBA;
    m[61] = 0xBB;
    for (int k = 65; k <= 90; k++) m[k] = k;
    m[91] = 0xDB;
    m[92] = 0xDC;
    m[93] = 0xDD;
    m[96] = 0xC0;
    m[161] = 0xE2;
    m[162] = 0xE2;
    m[256] = 0x1B;
    m[257] = 0x0D;
    m[258] = 0x09;
    m[259] = 0x08;
    m[260] = 0x2D;
    m[261] = 0x2E;
    m[262] = 0x27;
    m[263] = 0x25;
    m[264] = 0x28;
    m[265] = 0x26;
    m[266] = 0x21;
    m[267] = 0x22;
    m[268] = 0x24;
    m[269] = 0x23;
    m[280] = 0x14;
    m[281] = 0x91;
    m[282] = 0x90;
    m[283] = 0x2C;
    m[284] = 0x13;
    for (int i = 0; i < 24; i++) m[290 + i] = 0x70 + i;  // F1..F24
    for (int i = 0; i < 10; i++) m[320 + i] = 0x60 + i;  // keypad 0..9
    m[330] = 0x6E;
    m[331] = 0x6F;
    m[332] = 0x6A;
    m[333] = 0x6D;
    m[334] = 0x6B;
    m[335] = 0x0D;
    m[340] = 0xA0;
    m[341] = 0xA2;
    m[342] = 0xA4;
    m[343] = 0x5B;
    m[344] = 0xA1;
    m[345] = 0xA3;
    m[346] = 0xA5;
    m[347] = 0x5C;
    m[348] = 0x5D;
    return m;
}

// Mouse buttons as VK codes: left, right, middle, X1, X2.
int MouseButtonToVk(int button) {
    switch (button) {
    case 0: return 0x01;
    case 1: return 0x02;
    case 2: return 0x04;
    case 3: return 0x05;
    case 4: return 0x06;
    default: return 0;
    }
}

// Configured key matches a pressed key, with the generic Shift/Ctrl/Alt codes matching either side.
bool VkMatches(int cfgVk, int pressedVk) {
    if (cfgVk == pressedVk) return true;
    if (cfgVk == 0x10) return pressedVk == 0xA0 || pressedVk == 0xA1;
    if (cfgVk == 0x11) return pressedVk == 0xA2 || pressedVk == 0xA3;
    if (cfgVk == 0x12) return pressedVk == 0xA4 || pressedVk == 0xA5;
    return false;
}

// A combination fires when its last key is pressed while every other key is held.
bool ComboTriggered(const std::vector<int>& keys, int pressedVk) {
    if (keys.empty() || !VkMatches(keys.back(), pressedVk)) return false;
    for (size_t i = 0; i + 1 < keys.size(); i++) {
        if (!IsVkHeld(keys[i])) return false;
    }
    return true;
}

bool ComboUsesKey(const std::vector<int>& keys, int vk) {
    for (int k : keys) {
        if (VkMatches(k, vk)) return true;
    }
    return false;
}

bool StateMatches(const std::string& want, const std::string& state) {
    if (want == "any,cursor_free") return !g_rt.cursorDisabled;
    if (want == "any,cursor_grabbed") return g_rt.cursorDisabled;
    if (state.empty()) {
        // Without the state-output mod, infer in-world state from the cursor.
        return want == (g_rt.cursorDisabled ? "inworld,cursor_grabbed" : "inworld,cursor_free");
    }
    return want == state;
}

bool ConditionsMet(const HotkeyConditions& c) {
    for (int vk : c.exclusions) {
        if (IsVkHeld(vk)) return false;
    }
    if (c.gameState.empty()) return true;
    std::string state = GetGameState();
    for (auto& s : c.gameState) {
        if (StateMatches(s, state)) return true;
    }
    return false;
}

void EnsureRuntimeSized() {
    const Config& cfg = g_rt.cfg;
    if (g_hotkeyRt.size() != cfg.hotkeys.size()) g_hotkeyRt.assign(cfg.hotkeys.size(), HotkeyRuntime{});
    if (g_sensLastFire.size() != cfg.sensitivityHotkeys.size()) {
        g_sensLastFire.assign(cfg.sensitivityHotkeys.size(), -1e9);
        g_sensToggled.assign(cfg.sensitivityHotkeys.size(), false);
    }
    (void)g_cfgGeneration;
}

const std::string& EffectiveSecondary(size_t i) {
    const HotkeyConfig& h = g_rt.cfg.hotkeys[i];
    int alt = g_hotkeyRt[i].altIndex;
    if (alt >= 0 && alt < static_cast<int>(h.altSecondaryModes.size())) return h.altSecondaryModes[alt].mode;
    return h.secondaryMode;
}

void FireToggle(size_t i) {
    const std::string& secondary = EffectiveSecondary(i);
    const std::string& target = (g_rt.currentMode == secondary) ? g_rt.cfg.defaultMode : secondary;
    RequestModeSwitch(target);
}

void OpenWithDesktop(const std::string& path) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", path.c_str(), static_cast<char*>(nullptr));
        }
        _exit(0);
    }
}

// Ctrl+I panel keys. While the panel is open it takes all key presses; key releases still reach the game.
bool HandlePanelKey(int vk) {
    if (vk == 0x1B) {  // Escape closes
        g_rt.guiOpen = false;
        return true;
    }
    if (vk == 'R') {
        // Touching the mtime makes the next poll reload.
        std::string p = ConfigPath();
        if (FILE* f = fopen(p.c_str(), "a")) fclose(f);
        RenderShowToast("Toolscreen: reloading config", 1.5);
        return true;
    }
    if (vk == 'E') {
        OpenWithDesktop(ConfigPath());
        return true;
    }
    if (vk == 'O') {
        OpenWithDesktop(ConfigDir());
        return true;
    }
    return true;
}

// Returns true if the event should be swallowed.
bool ProcessPress(int vk) {
    const Config& cfg = g_rt.cfg;
    EnsureRuntimeSized();
    double now = NowSeconds();

    // Any key press cancels an armed trigger-on-release hotkey whose trigger is not this key.
    for (size_t i = 0; i < cfg.hotkeys.size(); i++) {
        if (g_hotkeyRt[i].releaseArmed && !ComboUsesKey(cfg.hotkeys[i].keys, vk)) g_hotkeyRt[i].releaseArmed = false;
    }

    if (ComboTriggered(cfg.guiHotkey, vk)) {
        g_rt.guiOpen = !g_rt.guiOpen;
        return true;
    }
    if (g_rt.guiOpen) return HandlePanelKey(vk);

    if (ComboTriggered(cfg.imageOverlaysHotkey, vk)) {
        g_rt.imagesVisible = !g_rt.imagesVisible;
        return true;
    }
    if (ComboTriggered(cfg.ninjabrainOverlayHotkey, vk)) {
        // Hide the panel if it is showing, otherwise reveal it (even before the first measurement).
        if (cfg.ninjabrain.enabled) {
            g_rt.nbOverride = g_rt.nbDisplayed ? -1 : 1;
        } else {
            g_rt.ninjabrainVisible = !g_rt.ninjabrainVisible;  // image-based Ninjabrain overlay
        }
        return true;
    }
    if (ComboTriggered(cfg.rebindToggleHotkey, vk)) {
        g_rebindsActive = !g_rebindsActive;
        RenderShowToast(g_rebindsActive ? "Key rebinds on" : "Key rebinds off", 1.5);
        return true;
    }

    bool swallow = false;
    for (size_t i = 0; i < cfg.hotkeys.size(); i++) {
        const HotkeyConfig& h = cfg.hotkeys[i];
        HotkeyRuntime& hr = g_hotkeyRt[i];

        for (size_t a = 0; a < h.altSecondaryModes.size(); a++) {
            if (ComboTriggered(h.altSecondaryModes[a].keys, vk)) {
                const std::string old = EffectiveSecondary(i);
                hr.altIndex = (hr.altIndex == static_cast<int>(a)) ? -1 : static_cast<int>(a);
                if (g_rt.currentMode == old) RequestModeSwitch(EffectiveSecondary(i));
                swallow = true;
            }
        }

        if (!ComboTriggered(h.keys, vk)) continue;
        bool exiting = g_rt.currentMode == EffectiveSecondary(i);
        bool condOk = ConditionsMet(h.conditions) || (exiting && h.allowExitToFullscreenRegardlessOfGameState);
        if (!condOk) continue;
        if (now - hr.lastFire < h.debounce / 1000.0) continue;

        if (h.triggerOnHold) {
            hr.lastFire = now;
            hr.holdActive = true;
            RequestModeSwitch(EffectiveSecondary(i));
        } else if (h.triggerOnRelease) {
            hr.releaseArmed = true;
        } else {
            hr.lastFire = now;
            FireToggle(i);
        }
        if (h.blockKeyFromGame) swallow = true;
    }

    for (size_t i = 0; i < cfg.sensitivityHotkeys.size(); i++) {
        const SensitivityHotkeyConfig& s = cfg.sensitivityHotkeys[i];
        if (!ComboTriggered(s.keys, vk) || !ConditionsMet(s.conditions)) continue;
        if (now - g_sensLastFire[i] < s.debounce / 1000.0) continue;
        g_sensLastFire[i] = now;
        bool on = s.toggle ? !g_sensToggled[i] : true;
        for (size_t j = 0; j < g_sensToggled.size(); j++) g_sensToggled[j] = false;
        g_sensToggled[i] = on;
        g_rt.sensOverrideActive = on;
        g_rt.sensOverrideX = s.separateXY ? s.sensitivityX : s.sensitivity;
        g_rt.sensOverrideY = s.separateXY ? s.sensitivityY : s.sensitivity;
    }
    return swallow;
}

bool ProcessRelease(int vk) {
    const Config& cfg = g_rt.cfg;
    EnsureRuntimeSized();
    if (g_rt.guiOpen) return false;  // releases always reach the game so no key stays stuck down
    bool swallow = false;
    double now = NowSeconds();
    for (size_t i = 0; i < cfg.hotkeys.size(); i++) {
        const HotkeyConfig& h = cfg.hotkeys[i];
        HotkeyRuntime& hr = g_hotkeyRt[i];
        if (h.keys.empty() || !VkMatches(h.keys.back(), vk)) continue;
        if (hr.holdActive) {
            hr.holdActive = false;
            RequestModeSwitch(cfg.defaultMode);
            if (h.blockKeyFromGame) swallow = true;
        }
        if (hr.releaseArmed) {
            hr.releaseArmed = false;
            hr.lastFire = now;
            FireToggle(i);
            if (h.blockKeyFromGame) swallow = true;
        }
        if (h.blockKeyFromGame && ComboUsesKey(h.keys, vk) && !h.triggerOnHold && !h.triggerOnRelease) swallow = true;
    }
    // Hold-style (non-toggle) sensitivity hotkeys end when their trigger key is released.
    for (size_t i = 0; i < cfg.sensitivityHotkeys.size(); i++) {
        const SensitivityHotkeyConfig& s = cfg.sensitivityHotkeys[i];
        if (!s.toggle && g_sensToggled[i] && !s.keys.empty() && VkMatches(s.keys.back(), vk)) {
            g_sensToggled[i] = false;
            g_rt.sensOverrideActive = false;
        }
    }
    return swallow;
}

}  // namespace

int GlfwKeyToVk(int glfwKey) {
    auto& m = GlfwToVkMap();
    auto it = m.find(glfwKey);
    return it == m.end() ? 0 : it->second;
}

int VkToGlfwKey(int vk) {
    static std::map<int, int> rev;
    if (rev.empty()) {
        for (auto& kv : GlfwToVkMap()) {
            if (!rev.count(kv.second)) rev[kv.second] = kv.first;
        }
        rev[0x0D] = 257;
        rev[0xE2] = 161;
        rev[0x10] = 340;
        rev[0x11] = 341;
        rev[0x12] = 342;
    }
    auto it = rev.find(vk);
    return it == rev.end() ? -1 : it->second;
}

bool IsVkHeld(int vk) {
    if (vk == 0x10) return g_held[0xA0] || g_held[0xA1];
    if (vk == 0x11) return g_held[0xA2] || g_held[0xA3];
    if (vk == 0x12) return g_held[0xA4] || g_held[0xA5];
    if (vk < 0 || vk >= 512) return false;
    return g_held[vk];
}

bool InputOnKey(int glfwKey, int scancode, int action, int mods, int& outKey) {
    (void)scancode;
    (void)mods;
    int vk = GlfwKeyToVk(glfwKey);
    if (vk > 0 && vk < 512) {
        if (action == GLFW_PRESS) g_held[vk] = true;
        if (action == GLFW_RELEASE) g_held[vk] = false;
    }

    if (action == GLFW_PRESS && vk && ProcessPress(vk)) return false;
    if (action == GLFW_RELEASE && vk && ProcessRelease(vk)) return false;
    if (g_rt.guiOpen && action != GLFW_RELEASE) return false;

    const Config& cfg = g_rt.cfg;
    if (cfg.keyRebindsEnabled && g_rebindsActive && vk) {
        for (auto& r : cfg.rebinds) {
            if (!r.enabled || r.fromKey != vk) continue;
            if (r.cursorState == "cursor_free" && g_rt.cursorDisabled) continue;
            if (r.cursorState == "cursor_grabbed" && !g_rt.cursorDisabled) continue;
            int target = VkToGlfwKey(r.toKey);
            if (target > 0) outKey = target;
            break;
        }
    }
    return true;
}

bool InputOnMouseButton(int button, int action, int mods) {
    (void)mods;
    int vk = MouseButtonToVk(button);
    if (vk) {
        if (action == GLFW_PRESS) g_held[vk] = true;
        if (action == GLFW_RELEASE) g_held[vk] = false;
        if (action == GLFW_PRESS && ProcessPress(vk)) return false;
        if (action == GLFW_RELEASE && ProcessRelease(vk)) return false;
    }
    return !g_rt.guiOpen || action == GLFW_RELEASE;
}

void InputGetSensitivity(float& sx, float& sy) {
    const Runtime& rt = g_rt;
    if (rt.sensOverrideActive) {
        sx = rt.sensOverrideX;
        sy = rt.sensOverrideY;
        return;
    }
    if (const ModeConfig* m = FindMode(rt.cfg, rt.currentMode)) {
        if (m->sensitivityOverrideEnabled) {
            sx = m->separateXYSensitivity ? m->modeSensitivityX : m->modeSensitivity;
            sy = m->separateXYSensitivity ? m->modeSensitivityY : m->modeSensitivity;
            return;
        }
    }
    sx = sy = rt.cfg.mouseSensitivity;
}

// Leaving the world (wall, title, waiting) drops back to the default mode and clears alternate modes.
void InputPerFrame() {
    std::string state = GetGameState();
    if (state == g_lastGameState) return;
    std::string prev = g_lastGameState;
    g_lastGameState = state;
    bool leftWorld = state == "wall" || state == "title" || state == "waiting";
    if (leftWorld) {
        for (auto& h : g_hotkeyRt) {
            h.altIndex = -1;
            h.holdActive = false;
            h.releaseArmed = false;
        }
        if (g_rt.currentMode != g_rt.cfg.defaultMode) RequestModeSwitch(g_rt.cfg.defaultMode);
    }
}
