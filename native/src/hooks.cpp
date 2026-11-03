// Hook layer. LWJGL and its bundled GLFW resolve every native entry point through dlsym() (and GL through
// glXGetProcAddress), so interposing dlsym is enough to wrap the GLFW and GL calls Toolscreen needs:
//  - window/framebuffer size queries and callbacks: the game is told the active mode's size;
//  - glViewport/glBlitFramebuffer on framebuffer 0: the final present is moved into the mode's screen rectangle;
//  - glfwSwapBuffers: overlays are composited before the real swap;
//  - key/mouse/cursor callbacks: hotkeys, key rebinds, sensitivity scaling and menu cursor mapping.
#include "ts.h"
#include "gui.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <link.h>
#include <sys/stat.h>
#include <unistd.h>

#include <GL/gl.h>
#include <GL/glext.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>

Runtime g_rt;
PerfStats g_perf;

bool PerfEnabled() {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("TOOLSCREEN_PERF");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on == 1;
}

static void PerfReport() {
    static double last = 0;
    double now = NowSeconds();
    if (last == 0) last = now;
    if (now - last < 5.0 || g_perf.frames == 0) return;
    PerfStats& p = g_perf;
    TsLog("perf: %d frames (%.0f fps), overlay drawn %d, screen-copy fallback %d | per frame: overlay CPU %.3f ms (max %.3f), "
          "input/poll CPU %.3f ms, overlay GPU %.3f ms",
          p.frames, p.frames / (now - last), p.renderedFrames, p.fallbackFrames, p.renderCpu / p.frames, p.renderCpuMax,
          p.pollCpu / p.frames, p.gpuSamples ? p.gpuMs / p.gpuSamples : 0.0);
    std::string stages;
    for (int i = 0; i < 8; i++) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%s%s %.3f (max %.3f)", i ? ", " : "", kPerfStageNames[i], p.stage[i] / p.frames, p.stageMax[i]);
        stages += buf;
    }
    TsLog("perf stages (ms): %s", stages.c_str());
    p = PerfStats{};
    last = now;
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static FILE* g_logFile = nullptr;
static std::mutex g_logMutex;

void TsLog(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "[Toolscreen] %s\n", buf);
    if (!g_logFile) {
        std::string p = ConfigDir() + "/toolscreen.log";
        g_logFile = fopen(p.c_str(), "w");
    }
    if (g_logFile) {
        time_t t = time(nullptr);
        char ts[32];
        strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&t));
        fprintf(g_logFile, "%s %s\n", ts, buf);
        fflush(g_logFile);
    }
}

static bool EnvFlag(const char* name) {
    const char* e = getenv(name);
    return e && *e && *e != '0';
}

double NowSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ---------------------------------------------------------------------------
// Real dlsym
// ---------------------------------------------------------------------------
using DlsymFn = void* (*)(void*, const char*);
static DlsymFn RealDlsym() {
    static DlsymFn fn = nullptr;
    if (!fn) {
        // dlvsym is not interposed, so it reaches glibc's own dlsym.
        fn = reinterpret_cast<DlsymFn>(dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34"));
        if (!fn) fn = reinterpret_cast<DlsymFn>(dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5"));
    }
    return fn;
}

// dlsym(RTLD_NEXT, ...) depends on the caller's object, which the wrapper hides. Resolve it by walking the
// link map from the caller onwards and returning the first definition found in a later object.
static void* ResolveNext(void* caller, const char* name) {
    Dl_info info;
    struct link_map* lm = nullptr;
    if (!dladdr1(caller, &info, reinterpret_cast<void**>(&lm), RTLD_DL_LINKMAP) || !lm) {
        return RealDlsym()(RTLD_DEFAULT, name);
    }
    for (struct link_map* m = lm->l_next; m; m = m->l_next) {
        void* h = dlopen(m->l_name && m->l_name[0] ? m->l_name : nullptr, RTLD_LAZY | RTLD_NOLOAD);
        if (!h) continue;
        void* sym = RealDlsym()(h, name);
        dlclose(h);
        if (!sym) continue;
        Dl_info si;
        if (dladdr(sym, &si) && si.dli_fname && m->l_name && strcmp(si.dli_fname, m->l_name) == 0) return sym;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// GLFW types and constants (GLFW 3.3 ABI)
// ---------------------------------------------------------------------------
struct GLFWwindow;
using GLFWframebuffersizefun = void (*)(GLFWwindow*, int, int);
using GLFWwindowsizefun = void (*)(GLFWwindow*, int, int);
using GLFWcursorposfun = void (*)(GLFWwindow*, double, double);
using GLFWkeyfun = void (*)(GLFWwindow*, int, int, int, int);
using GLFWmousebuttonfun = void (*)(GLFWwindow*, int, int, int);
using GLFWcharfun = void (*)(GLFWwindow*, unsigned int);
using GLFWcharmodsfun = void (*)(GLFWwindow*, unsigned int, int);
using GLFWscrollfun = void (*)(GLFWwindow*, double, double);

constexpr int GLFW_CURSOR = 0x00033001;
constexpr int GLFW_CURSOR_NORMAL = 0x00034001;
constexpr int GLFW_CURSOR_DISABLED = 0x00034003;
constexpr int GLFW_PRESS = 1;
constexpr int GLFW_RELEASE = 0;

// Real function pointers (filled when the game resolves them).
static GLFWwindow* (*real_glfwCreateWindow)(int, int, const char*, void*, GLFWwindow*);
static void (*real_glfwSwapBuffers)(GLFWwindow*);
static void (*real_glfwPollEvents)();
static GLFWframebuffersizefun (*real_glfwSetFramebufferSizeCallback)(GLFWwindow*, GLFWframebuffersizefun);
static GLFWwindowsizefun (*real_glfwSetWindowSizeCallback)(GLFWwindow*, GLFWwindowsizefun);
static void (*real_glfwGetFramebufferSize)(GLFWwindow*, int*, int*);
static void (*real_glfwGetWindowSize)(GLFWwindow*, int*, int*);
static GLFWcursorposfun (*real_glfwSetCursorPosCallback)(GLFWwindow*, GLFWcursorposfun);
static void (*real_glfwGetCursorPos)(GLFWwindow*, double*, double*);
static void (*real_glfwSetCursorPos)(GLFWwindow*, double, double);
static GLFWkeyfun (*real_glfwSetKeyCallback)(GLFWwindow*, GLFWkeyfun);
static GLFWmousebuttonfun (*real_glfwSetMouseButtonCallback)(GLFWwindow*, GLFWmousebuttonfun);
static GLFWcharfun (*real_glfwSetCharCallback)(GLFWwindow*, GLFWcharfun);
static GLFWcharmodsfun (*real_glfwSetCharModsCallback)(GLFWwindow*, GLFWcharmodsfun);
static GLFWscrollfun (*real_glfwSetScrollCallback)(GLFWwindow*, GLFWscrollfun);
static void (*real_glfwSetInputMode)(GLFWwindow*, int, int);
static int (*real_glfwGetKey)(GLFWwindow*, int);
static int (*real_glfwGetMouseButton)(GLFWwindow*, int);

// The game's own callbacks.
static GLFWframebuffersizefun game_fbSizeCb;
static GLFWwindowsizefun game_winSizeCb;
static GLFWcursorposfun game_cursorPosCb;
static GLFWkeyfun game_keyCb;
static GLFWmousebuttonfun game_mouseButtonCb;
static GLFWcharfun game_charCb;
static GLFWcharmodsfun game_charModsCb;
static GLFWscrollfun game_scrollCb;

static GLFWwindow* g_window = nullptr;
static int g_realWinW = 0, g_realWinH = 0;

// Cursor state for sensitivity scaling (disabled cursor) and menu mapping (normal cursor).
static bool g_haveLastCursor = false;
static double g_lastRealX = 0, g_lastRealY = 0, g_virtX = 0, g_virtY = 0;

// ---------------------------------------------------------------------------
// Mode layout
// ---------------------------------------------------------------------------
static void ComputeDestRect() {
    Runtime& rt = g_rt;
    const ModeConfig* mode = FindMode(rt.cfg, rt.currentMode);
    int sw = rt.screenW, sh = rt.screenH;
    if (mode && mode->stretch.enabled && mode->stretch.width > 0 && mode->stretch.height > 0) {
        bool fullscreenSized = rt.gameW == sw && rt.gameH == sh;
        if (mode->id == "Fullscreen" || fullscreenSized) {
            rt.destX = 0;
            rt.destY = 0;
            rt.destW = sw;
            rt.destH = sh;
        } else {
            rt.destX = mode->stretch.x;
            rt.destY = mode->stretch.y;
            rt.destW = mode->stretch.width;
            rt.destH = mode->stretch.height;
        }
    } else {
        rt.destW = rt.gameW;
        rt.destH = rt.gameH;
        rt.destX = (sw - rt.gameW) / 2;
        rt.destY = (sh - rt.gameH) / 2;
    }
}

// Resolves the size the game should render at for the current mode and tells the game if it changed.
static void SyncGameSize(bool forceNotify) {
    Runtime& rt = g_rt;
    if (rt.screenW <= 0 || rt.screenH <= 0) return;
    int w = rt.screenW, h = rt.screenH;
    if (const ModeConfig* mode = FindMode(rt.cfg, rt.currentMode)) ResolveModeSize(rt.cfg, *mode, rt.screenW, rt.screenH, w, h);
    bool changed = (w != rt.gameW || h != rt.gameH);
    rt.gameW = w;
    rt.gameH = h;
    rt.gameSizeFaked = (w != rt.screenW || h != rt.screenH);
    ComputeDestRect();
    if ((changed || forceNotify) && g_window) {
        if (game_winSizeCb) game_winSizeCb(g_window, w, h);
        if (game_fbSizeCb) game_fbSizeCb(g_window, w, h);
    }
}

void ApplyModeLayout() { SyncGameSize(false); }

void RequestModeSwitch(const std::string& mode) {
    g_rt.pendingMode = mode;
    g_rt.modeDirty = true;
}

static void ApplyPendingMode() {
    Runtime& rt = g_rt;
    if (!rt.modeDirty) return;
    rt.modeDirty = false;
    std::string target = rt.pendingMode;
    if (!FindMode(rt.cfg, target)) {
        TsLog("mode '%s' does not exist in config", target.c_str());
        return;
    }
    if (target == rt.currentMode) return;
    TsLog("mode: %s -> %s", rt.currentMode.c_str(), target.c_str());
    rt.currentMode = target;
    SyncGameSize(false);
}

// Config hot reload: checked about twice a second on the main thread.
static void MaybeReloadConfig(bool force) {
    static double lastCheck = 0;
    static time_t lastMtime = 0;
    double now = NowSeconds();
    if (!force && now - lastCheck < 0.5) return;
    lastCheck = now;
    struct stat st;
    std::string path = ConfigPath();
    time_t mtime = (stat(path.c_str(), &st) == 0) ? st.st_mtime : 0;
    if (!force && mtime == lastMtime) return;
    lastMtime = mtime;

    Runtime& rt = g_rt;
    std::lock_guard<std::mutex> lock(rt.cfgMutex);
    bool firstLoad = !rt.cfgLoaded;
    std::string err;
    bool ok = LoadConfig(rt.cfg, err, rt.screenW, rt.screenH);
    // The first load may have just written the default file.
    if (stat(path.c_str(), &st) == 0) lastMtime = st.st_mtime;
    rt.cfgLoaded = true;
    rt.cfgError = err;
    if (ok) {
        TsLog("config %s: %zu modes, %zu mirrors, %zu images, %zu hotkeys", firstLoad ? "loaded" : "reloaded", rt.cfg.modes.size(),
              rt.cfg.mirrors.size(), rt.cfg.images.size(), rt.cfg.hotkeys.size());
        if (!firstLoad) RenderShowToast("Toolscreen: config reloaded", 2.0);
    } else {
        TsLog("config error: %s", err.c_str());
        RenderShowToast("Toolscreen config error: " + err, 8.0);
    }
    if (rt.cfg.ninjabrain.enabled) NinjabrainSetUrl(rt.cfg.ninjabrain.apiBaseUrl);
    if (firstLoad || !FindMode(rt.cfg, rt.currentMode)) rt.currentMode = rt.cfg.defaultMode;
    if (!FindMode(rt.cfg, rt.currentMode) && !rt.cfg.modes.empty()) rt.currentMode = rt.cfg.modes[0].id;
    SyncGameSize(false);
}

// ---------------------------------------------------------------------------
// Coordinate mapping between the real window and the game's (mode-sized) window
// ---------------------------------------------------------------------------
static void RealToGame(double rx, double ry, double& gx, double& gy) {
    Runtime& rt = g_rt;
    if (!rt.gameSizeFaked && rt.destW == rt.screenW && rt.destH == rt.screenH) {
        gx = rx;
        gy = ry;
        return;
    }
    double fbScaleX = g_realWinW > 0 ? static_cast<double>(rt.screenW) / g_realWinW : 1.0;
    double fbScaleY = g_realWinH > 0 ? static_cast<double>(rt.screenH) / g_realWinH : 1.0;
    double fx = rx * fbScaleX, fy = ry * fbScaleY;
    gx = (fx - rt.destX) * rt.gameW / (rt.destW > 0 ? rt.destW : 1);
    gy = (fy - rt.destY) * rt.gameH / (rt.destH > 0 ? rt.destH : 1);
}

static void GameToReal(double gx, double gy, double& rx, double& ry) {
    Runtime& rt = g_rt;
    if (!rt.gameSizeFaked && rt.destW == rt.screenW && rt.destH == rt.screenH) {
        rx = gx;
        ry = gy;
        return;
    }
    double fbScaleX = g_realWinW > 0 ? static_cast<double>(rt.screenW) / g_realWinW : 1.0;
    double fbScaleY = g_realWinH > 0 ? static_cast<double>(rt.screenH) / g_realWinH : 1.0;
    rx = (rt.destX + gx * rt.destW / (rt.gameW > 0 ? rt.gameW : 1)) / fbScaleX;
    ry = (rt.destY + gy * rt.destH / (rt.gameH > 0 ? rt.gameH : 1)) / fbScaleY;
}

// ---------------------------------------------------------------------------
// GLFW wrappers
// ---------------------------------------------------------------------------
static void RefreshRealSizes(GLFWwindow* w) {
    if (!w) return;
    int fw = 0, fh = 0;
    if (real_glfwGetFramebufferSize) real_glfwGetFramebufferSize(w, &fw, &fh);
    if (real_glfwGetWindowSize) real_glfwGetWindowSize(w, &g_realWinW, &g_realWinH);
    if (fw > 0 && fh > 0) {
        g_rt.screenW = fw;
        g_rt.screenH = fh;
    }
}

static void ts_fbSizeCb(GLFWwindow* w, int fw, int fh) {
    if (fw <= 0 || fh <= 0) {  // minimised
        if (game_fbSizeCb) game_fbSizeCb(w, fw, fh);
        return;
    }
    g_rt.screenW = fw;
    g_rt.screenH = fh;
    if (real_glfwGetWindowSize) real_glfwGetWindowSize(w, &g_realWinW, &g_realWinH);
    SyncGameSize(true);
}

static void ts_winSizeCb(GLFWwindow* w, int ww, int wh) {
    g_realWinW = ww;
    g_realWinH = wh;
    // The framebuffer callback carries the authoritative size; it notifies the game for both.
    if (ww <= 0 || wh <= 0) {
        if (game_winSizeCb) game_winSizeCb(w, ww, wh);
    }
}

// Scales a mouse movement by the active Toolscreen sensitivity in whole counts, carrying the remainder, as
// Windows Toolscreen does. The game then still only sees whole mouse counts, so every view angle stays on the
// steps its own sensitivity allows; boat-eye measuring depends on that.
void ScaleMouseDelta(double dx, double dy, float sx, float sy, double& outX, double& outY) {
    static double accX = 0, accY = 0;
    static float lastSx = 1, lastSy = 1;
    static bool haveLast = false;
    if (!haveLast || std::fabs(sx - lastSx) > 1e-6f || std::fabs(sy - lastSy) > 1e-6f) {
        accX = accY = 0;
        lastSx = sx;
        lastSy = sy;
        haveLast = true;
    }
    // A remainder from one direction must not delay movement the other way.
    if (dx != 0 && accX != 0 && ((dx > 0) != (accX > 0))) accX = 0;
    if (dy != 0 && accY != 0 && ((dy > 0) != (accY > 0))) accY = 0;
    if (sx == 1.0f && sy == 1.0f) {
        accX = accY = 0;
        outX = dx;
        outY = dy;
        return;
    }
    accX += dx * sx;
    accY += dy * sy;
    // Truncating (not rounding) keeps the remainder's sign equal to the movement's, so the direction check above
    // never discards it and the average output is exactly the input times the sensitivity.
    outX = std::trunc(accX);
    outY = std::trunc(accY);
    accX -= outX;
    accY -= outY;
}

static void ts_cursorPosCb(GLFWwindow* w, double x, double y) {
    if (g_rt.guiOpen) {
        double sx = g_realWinW > 0 ? static_cast<double>(g_rt.screenW) / g_realWinW : 1.0;
        double sy = g_realWinH > 0 ? static_cast<double>(g_rt.screenH) / g_realWinH : 1.0;
        GuiOnCursor(x * sx, y * sy);
        return;
    }
    if (g_rt.cursorDisabled) {
        if (!g_haveLastCursor) {
            g_lastRealX = x;
            g_lastRealY = y;
            g_virtX = x;
            g_virtY = y;
            g_haveLastCursor = true;
        }
        float sx, sy;
        InputGetSensitivity(sx, sy);
        double outX, outY;
        ScaleMouseDelta(x - g_lastRealX, y - g_lastRealY, sx, sy, outX, outY);
        g_virtX += outX;
        g_virtY += outY;
        g_lastRealX = x;
        g_lastRealY = y;
        if (game_cursorPosCb) game_cursorPosCb(w, g_virtX, g_virtY);
    } else {
        double gx, gy;
        RealToGame(x, y, gx, gy);
        if (game_cursorPosCb) game_cursorPosCb(w, gx, gy);
    }
}

static void ts_keyCb(GLFWwindow* w, int key, int scancode, int action, int mods) {
    if (g_rt.guiOpen && !GuiCapturingKeys()) GuiOnKey(key, action);
    int outKey = key;
    if (!InputOnKey(key, scancode, action, mods, outKey)) return;
    if (game_keyCb) game_keyCb(w, outKey, scancode, action, mods);
}

static void ts_mouseButtonCb(GLFWwindow* w, int button, int action, int mods) {
    if (g_rt.guiOpen && !GuiCapturingKeys()) GuiOnMouseButton(button, action);
    if (!InputOnMouseButton(button, action, mods)) return;
    if (game_mouseButtonCb) game_mouseButtonCb(w, button, action, mods);
}

static void ts_charCb(GLFWwindow* w, unsigned int c) {
    if (g_rt.guiOpen) {
        GuiOnChar(c);
        return;
    }
    if (game_charCb) game_charCb(w, c);
}

static void ts_charModsCb(GLFWwindow* w, unsigned int c, int mods) {
    if (g_rt.guiOpen) return;
    if (game_charModsCb) game_charModsCb(w, c, mods);
}

static void ts_scrollCb(GLFWwindow* w, double dx, double dy) {
    if (g_rt.guiOpen) {
        GuiOnScroll(dx, dy);
        return;
    }
    if (game_scrollCb) game_scrollCb(w, dx, dy);
}

extern "C" {

static void EnsureGlfwReals();

static GLFWwindow* hk_glfwCreateWindow(int w, int h, const char* title, void* monitor, GLFWwindow* share) {
    GLFWwindow* win = real_glfwCreateWindow(w, h, title, monitor, share);
    if (win && !g_window) {
        EnsureGlfwReals();
        g_window = win;
        RefreshRealSizes(win);
        TsLog("window created: %dx%d framebuffer", g_rt.screenW, g_rt.screenH);
        StartGameStateThread();
        MaybeReloadConfig(true);
        // Our wrappers must be installed even if the game never sets some of these callbacks.
        real_glfwSetFramebufferSizeCallback(win, ts_fbSizeCb);
        real_glfwSetWindowSizeCallback(win, ts_winSizeCb);
        if (real_glfwSetKeyCallback) real_glfwSetKeyCallback(win, ts_keyCb);
        if (real_glfwSetMouseButtonCallback) real_glfwSetMouseButtonCallback(win, ts_mouseButtonCb);
        if (real_glfwSetCursorPosCallback) real_glfwSetCursorPosCallback(win, ts_cursorPosCb);
    }
    return win;
}

#define TS_SWAP_CB(name, type, gameVar, wrapper)                     \
    static type hk_##name(GLFWwindow* w, type cb) {                  \
        if (w != g_window || !g_window) return real_##name(w, cb);   \
        type prev = gameVar;                                         \
        gameVar = cb;                                                \
        real_##name(w, wrapper);                                     \
        return prev;                                                 \
    }

TS_SWAP_CB(glfwSetFramebufferSizeCallback, GLFWframebuffersizefun, game_fbSizeCb, ts_fbSizeCb)
TS_SWAP_CB(glfwSetWindowSizeCallback, GLFWwindowsizefun, game_winSizeCb, ts_winSizeCb)
TS_SWAP_CB(glfwSetCursorPosCallback, GLFWcursorposfun, game_cursorPosCb, ts_cursorPosCb)
TS_SWAP_CB(glfwSetKeyCallback, GLFWkeyfun, game_keyCb, ts_keyCb)
TS_SWAP_CB(glfwSetMouseButtonCallback, GLFWmousebuttonfun, game_mouseButtonCb, ts_mouseButtonCb)
TS_SWAP_CB(glfwSetCharCallback, GLFWcharfun, game_charCb, ts_charCb)
TS_SWAP_CB(glfwSetCharModsCallback, GLFWcharmodsfun, game_charModsCb, ts_charModsCb)
TS_SWAP_CB(glfwSetScrollCallback, GLFWscrollfun, game_scrollCb, ts_scrollCb)

static void hk_glfwGetFramebufferSize(GLFWwindow* w, int* outW, int* outH) {
    real_glfwGetFramebufferSize(w, outW, outH);
    if (w == g_window && g_rt.gameSizeFaked) {
        if (outW) *outW = g_rt.gameW;
        if (outH) *outH = g_rt.gameH;
    }
}

static void hk_glfwGetWindowSize(GLFWwindow* w, int* outW, int* outH) {
    real_glfwGetWindowSize(w, outW, outH);
    if (w == g_window && g_rt.gameSizeFaked) {
        if (outW) *outW = g_rt.gameW;
        if (outH) *outH = g_rt.gameH;
    }
}

static void hk_glfwGetCursorPos(GLFWwindow* w, double* x, double* y) {
    double rx = 0, ry = 0;
    real_glfwGetCursorPos(w, &rx, &ry);
    if (w != g_window) {
        if (x) *x = rx;
        if (y) *y = ry;
        return;
    }
    double gx, gy;
    if (g_rt.cursorDisabled && g_haveLastCursor) {
        gx = g_virtX;
        gy = g_virtY;
    } else if (g_rt.cursorDisabled) {
        gx = rx;
        gy = ry;
    } else {
        RealToGame(rx, ry, gx, gy);
    }
    if (x) *x = gx;
    if (y) *y = gy;
}

static void hk_glfwSetCursorPos(GLFWwindow* w, double x, double y) {
    if (w != g_window) return real_glfwSetCursorPos(w, x, y);
    if (g_rt.cursorDisabled) {
        real_glfwSetCursorPos(w, x, y);
        g_haveLastCursor = false;
        return;
    }
    double rx, ry;
    GameToReal(x, y, rx, ry);
    real_glfwSetCursorPos(w, rx, ry);
}

static void hk_glfwSetInputMode(GLFWwindow* w, int mode, int value) {
    if (w == g_window && mode == GLFW_CURSOR && g_rt.guiOpen) {
        // The editor keeps the cursor free; remember what the game wants for when it closes.
        g_rt.cursorDisabled = (value == GLFW_CURSOR_DISABLED);
        return;
    }
    real_glfwSetInputMode(w, mode, value);
    if (w == g_window && mode == GLFW_CURSOR) {
        bool disabled = (value == GLFW_CURSOR_DISABLED);
        if (disabled != g_rt.cursorDisabled) g_haveLastCursor = false;
        g_rt.cursorDisabled = disabled;
    }
}

static int hk_glfwGetKey(GLFWwindow* w, int key) {
    if (w == g_window && g_rt.guiOpen) return GLFW_RELEASE;
    return real_glfwGetKey(w, key);
}

static int hk_glfwGetMouseButton(GLFWwindow* w, int button) {
    if (w == g_window && g_rt.guiOpen) return GLFW_RELEASE;
    return real_glfwGetMouseButton(w, button);
}

// Frees the cursor when the editor opens and gives it back to the game when it closes.
static void SyncGuiCursor() {
    static bool wasOpen = false;
    if (g_rt.guiOpen == wasOpen) return;
    wasOpen = g_rt.guiOpen;
    if (g_rt.guiOpen) {
        GuiOnOpened();
        if (g_rt.cursorDisabled) {
            real_glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            if (real_glfwSetCursorPos) real_glfwSetCursorPos(g_window, g_realWinW / 2.0, g_realWinH / 2.0);
        }
    } else if (g_rt.cursorDisabled) {
        real_glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        g_haveLastCursor = false;
    }
}

static void hk_glfwPollEvents() {
    real_glfwPollEvents();
    if (!g_window) return;
    double t0 = PerfEnabled() ? NowSeconds() : 0;
    struct PollTimer {
        double t0;
        ~PollTimer() {
            if (t0 > 0) g_perf.pollCpu += (NowSeconds() - t0) * 1000.0;
        }
    } timer{t0};
    SyncGuiCursor();
    MaybeReloadConfig(false);
    InputPerFrame();
    ApplyPendingMode();
}

static bool CountOnly() {
    static int on = -1;
    if (on < 0) on = EnvFlag("TOOLSCREEN_DEBUG_COUNT_ONLY") ? 1 : 0;
    return on == 1;
}

static void hk_glfwSwapBuffers(GLFWwindow* w) {
    if (CountOnly()) {
        g_perf.frames++;
        PerfReport();
        real_glfwSwapBuffers(w);
        return;
    }
    if (w == g_window) {
        static int noRender = -1;
        if (noRender < 0) noRender = EnvFlag("TOOLSCREEN_DEBUG_NO_RENDER") ? 1 : 0;
        if (PerfEnabled()) {
            double t0 = NowSeconds();
            if (!noRender) RenderFrame();
            double dt = (NowSeconds() - t0) * 1000.0;
            g_perf.renderCpu += dt;
            g_perf.renderCpuMax = std::max(g_perf.renderCpuMax, dt);
            g_perf.frames++;
            PerfReport();
        } else if (!noRender) {
            RenderFrame();
        }
        real_glfwSwapBuffers(w);
        int limit = g_rt.cfg.fpsLimit;
        if (limit > 0) {
            static double next = 0;
            double now = NowSeconds();
            double frame = 1.0 / limit;
            if (next == 0 || now > next + frame) next = now;
            next += frame;
            double wait = next - NowSeconds();
            if (wait > 0) std::this_thread::sleep_for(std::chrono::duration<double>(wait));
        }
        return;
    }
    real_glfwSwapBuffers(w);
}

}  // extern "C"

// ---------------------------------------------------------------------------
// GL wrappers
// ---------------------------------------------------------------------------
static void(APIENTRY* real_glViewport)(GLint, GLint, GLsizei, GLsizei);
static void(APIENTRY* real_glBindFramebuffer)(GLenum, GLuint);
static void(APIENTRY* real_glBindFramebufferEXT)(GLenum, GLuint);
static void(APIENTRY* real_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
static void(APIENTRY* real_glBlitFramebufferEXT)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
static void(APIENTRY* real_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
static void(APIENTRY* real_glFramebufferTexture2DEXT)(GLenum, GLenum, GLenum, GLuint, GLint);
static void(APIENTRY* real_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
static void(APIENTRY* real_glDeleteTextures)(GLsizei, const GLuint*);
static void(APIENTRY* real_glDrawArrays)(GLenum, GLint, GLsizei);
static void(APIENTRY* real_glGetIntegerv)(GLenum, GLint*);

static GLuint g_drawFb = 0;
static bool g_presentPending = false;

// True while the game's draw target is the window and it sets a full-frame viewport: its final present.
static bool IsPresentViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    return g_drawFb == 0 && x == 0 && y == 0 && w == g_rt.gameW && h == g_rt.gameH && g_window != nullptr;
}

static void GetPresentRectGl(GLint& x, GLint& y, GLsizei& w, GLsizei& h) {
    x = g_rt.destX;
    y = g_rt.screenH - g_rt.destY - g_rt.destH;
    w = g_rt.destW;
    h = g_rt.destH;
}

static void APIENTRY hk_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    if (IsPresentViewport(x, y, w, h)) {
        g_presentPending = true;
        if (g_rt.gameSizeFaked || g_rt.destW != g_rt.gameW || g_rt.destH != g_rt.gameH) {
            GetPresentRectGl(x, y, w, h);
        }
    }
    real_glViewport(x, y, w, h);
}

static void APIENTRY hk_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (g_presentPending && g_drawFb == 0) {
        // The texture bound for the full-screen quad is the game's final colour buffer.
        g_presentPending = false;
        GLint tex = 0;
        if (real_glGetIntegerv) real_glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
        if (tex > 0) RenderNoteGameTextureAttach(static_cast<unsigned>(tex));
    }
    real_glDrawArrays(mode, first, count);
}

static void TrackBind(GLenum target, GLuint fb) {
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
        g_drawFb = fb;
        if (fb != 0) g_presentPending = false;
    }
}

static void APIENTRY hk_glBindFramebuffer(GLenum target, GLuint fb) {
    TrackBind(target, fb);
    real_glBindFramebuffer(target, fb);
}

static void APIENTRY hk_glBindFramebufferEXT(GLenum target, GLuint fb) {
    TrackBind(target, fb);
    real_glBindFramebufferEXT(target, fb);
}

static void RemapBlit(GLint& dx0, GLint& dy0, GLint& dx1, GLint& dy1) {
    if (g_drawFb != 0 || !g_window) return;
    if (dx0 == 0 && dy0 == 0 && dx1 == g_rt.gameW && dy1 == g_rt.gameH &&
        (g_rt.gameSizeFaked || g_rt.destW != g_rt.gameW || g_rt.destH != g_rt.gameH)) {
        GLint x, y;
        GLsizei w, h;
        GetPresentRectGl(x, y, w, h);
        dx0 = x;
        dy0 = y;
        dx1 = x + w;
        dy1 = y + h;
    }
}

static void APIENTRY hk_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                                          GLbitfield mask, GLenum filter) {
    RemapBlit(dx0, dy0, dx1, dy1);
    real_glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
}

static void APIENTRY hk_glBlitFramebufferEXT(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                                             GLbitfield mask, GLenum filter) {
    RemapBlit(dx0, dy0, dx1, dy1);
    real_glBlitFramebufferEXT(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
}

static void APIENTRY hk_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint tex, GLint level) {
    real_glFramebufferTexture2D(target, attachment, textarget, tex, level);
}

static void APIENTRY hk_glFramebufferTexture2DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint tex, GLint level) {
    real_glFramebufferTexture2DEXT(target, attachment, textarget, tex, level);
}

static void APIENTRY hk_glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border,
                                     GLenum format, GLenum type, const void* data) {
    real_glTexImage2D(target, level, internalFormat, w, h, border, format, type, data);
    if (target == GL_TEXTURE_2D && level == 0 && real_glGetIntegerv) {
        GLint tex = 0;
        real_glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
        if (tex > 0) RenderNoteTextureImage(static_cast<unsigned>(tex), w, h);
    }
}

static void APIENTRY hk_glDeleteTextures(GLsizei n, const GLuint* textures) {
    for (GLsizei i = 0; i < n; i++) RenderNoteTextureDeleted(textures[i]);
    real_glDeleteTextures(n, textures);
}

// ---------------------------------------------------------------------------
// Hook table
// ---------------------------------------------------------------------------
struct HookEntry {
    const char* name;
    void* hook;
    void** real;
};

#define H(fn) {#fn, reinterpret_cast<void*>(&hk_##fn), reinterpret_cast<void**>(&real_##fn)}
static HookEntry g_glfwHooks[] = {
    H(glfwCreateWindow),
    H(glfwSwapBuffers),
    H(glfwPollEvents),
    H(glfwSetFramebufferSizeCallback),
    H(glfwSetWindowSizeCallback),
    H(glfwGetFramebufferSize),
    H(glfwGetWindowSize),
    H(glfwSetCursorPosCallback),
    H(glfwGetCursorPos),
    H(glfwSetCursorPos),
    H(glfwSetKeyCallback),
    H(glfwSetMouseButtonCallback),
    H(glfwSetCharCallback),
    H(glfwSetCharModsCallback),
    H(glfwSetScrollCallback),
    H(glfwSetInputMode),
    H(glfwGetKey),
    H(glfwGetMouseButton),
};

static HookEntry g_glHooks[] = {
    H(glViewport),
    H(glBindFramebuffer),
    H(glBindFramebufferEXT),
    H(glBlitFramebuffer),
    H(glBlitFramebufferEXT),
    H(glFramebufferTexture2D),
    H(glFramebufferTexture2DEXT),
    H(glTexImage2D),
    H(glDeleteTextures),
    H(glDrawArrays),
};
#undef H

static const char* (*real_glfwGetClipboardString)(GLFWwindow*);
static void (*real_glfwSetClipboardString)(GLFWwindow*, const char*);

const char* TsGetClipboard() {
    const char* s = (real_glfwGetClipboardString && g_window) ? real_glfwGetClipboardString(g_window) : nullptr;
    return s ? s : "";
}

void TsSetClipboard(const char* text) {
    if (real_glfwSetClipboardString && g_window) real_glfwSetClipboardString(g_window, text);
}

// Resolves any GLFW function the game has not looked up yet, from the library that provides glfwCreateWindow.
static void EnsureGlfwReals() {
    Dl_info info;
    if (!real_glfwCreateWindow || !dladdr(reinterpret_cast<void*>(real_glfwCreateWindow), &info) || !info.dli_fname) return;
    void* lib = dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD);
    if (!lib) return;
    for (auto& e : g_glfwHooks) {
        if (!*e.real) *e.real = RealDlsym()(lib, e.name);
    }
    real_glfwGetClipboardString = reinterpret_cast<decltype(real_glfwGetClipboardString)>(RealDlsym()(lib, "glfwGetClipboardString"));
    real_glfwSetClipboardString = reinterpret_cast<decltype(real_glfwSetClipboardString)>(RealDlsym()(lib, "glfwSetClipboardString"));
    dlclose(lib);
}

using GetProcFn = void* (*)(const unsigned char*);
static GetProcFn real_glXGetProcAddress, real_glXGetProcAddressARB;

static void* LookupHook(HookEntry* table, size_t n, const char* name, void* realPtr) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(table[i].name, name) == 0) {
            if (!realPtr) return nullptr;
            if (!*table[i].real) *table[i].real = realPtr;
            return table[i].hook;
        }
    }
    return realPtr;
}

static void* MaybeWrapGl(const char* name, void* realPtr) {
    if (!realPtr || name[0] != 'g' || name[1] != 'l') return realPtr;
    static int noGlHooks = -1;
    if (noGlHooks < 0) noGlHooks = EnvFlag("TOOLSCREEN_DEBUG_NO_GL_HOOKS") ? 1 : 0;
    if (noGlHooks) return realPtr;
    if (strcmp(name, "glGetIntegerv") == 0 && !real_glGetIntegerv) {
        real_glGetIntegerv = reinterpret_cast<decltype(real_glGetIntegerv)>(realPtr);
    }
    return LookupHook(g_glHooks, sizeof(g_glHooks) / sizeof(g_glHooks[0]), name, realPtr);
}

static void* hk_glXGetProcAddress(const unsigned char* name) {
    void* p = real_glXGetProcAddress(name);
    return MaybeWrapGl(reinterpret_cast<const char*>(name), p);
}

static void* hk_glXGetProcAddressARB(const unsigned char* name) {
    void* p = real_glXGetProcAddressARB(name);
    return MaybeWrapGl(reinterpret_cast<const char*>(name), p);
}

// Used by the renderer: real GL entry points, never our wrappers.
void* TsGetGLProc(const char* name) {
    static GetProcFn gpa = nullptr;
    if (!gpa) {
        void* lib = dlopen("libGL.so.1", RTLD_LAZY | RTLD_NOLOAD);
        if (!lib) lib = dlopen("libGL.so.1", RTLD_LAZY);
        if (lib) gpa = reinterpret_cast<GetProcFn>(RealDlsym()(lib, "glXGetProcAddressARB"));
    }
    void* p = gpa ? gpa(reinterpret_cast<const unsigned char*>(name)) : nullptr;
    if (!p) p = RealDlsym()(RTLD_DEFAULT, name);
    return p;
}

static bool IsDisabled() {
    static int disabled = -1;
    if (disabled < 0) {
        const char* e = getenv("TOOLSCREEN_DISABLE");
        disabled = (e && *e && *e != '0') ? 1 : 0;
    }
    return disabled == 1;
}

static bool CallerIsSelf(void* ret) {
    static void* selfBase = nullptr;
    if (!selfBase) {
        Dl_info info;
        if (dladdr(reinterpret_cast<void*>(&CallerIsSelf), &info)) selfBase = info.dli_fbase;
    }
    Dl_info ci;
    return selfBase && dladdr(ret, &ci) && ci.dli_fbase == selfBase;
}

extern "C" __attribute__((visibility("default"))) void* dlsym(void* handle, const char* name) {
    DlsymFn real = RealDlsym();
    void* ret = __builtin_return_address(0);
    void* p = (handle == RTLD_NEXT) ? ResolveNext(ret, name) : real(handle, name);
    if (!p || !name || name[0] != 'g' || name[1] != 'l' || IsDisabled()) return p;
    // Toolscreen's own lookups (ImGui's OpenGL loader) must get the real functions, not the game hooks.
    if (CallerIsSelf(ret)) return p;

    if (name[2] == 'f' && name[3] == 'w') {
        if (CountOnly()) {
            if (strcmp(name, "glfwSwapBuffers") != 0) return p;
            if (!real_glfwSwapBuffers) real_glfwSwapBuffers = reinterpret_cast<decltype(real_glfwSwapBuffers)>(p);
            return reinterpret_cast<void*>(&hk_glfwSwapBuffers);
        }
        return LookupHook(g_glfwHooks, sizeof(g_glfwHooks) / sizeof(g_glfwHooks[0]), name, p);
    }
    if (CountOnly()) return p;
    if (name[2] == 'X') {
        if (strcmp(name, "glXGetProcAddressARB") == 0) {
            if (!real_glXGetProcAddressARB) real_glXGetProcAddressARB = reinterpret_cast<GetProcFn>(p);
            return reinterpret_cast<void*>(&hk_glXGetProcAddressARB);
        }
        if (strcmp(name, "glXGetProcAddress") == 0) {
            if (!real_glXGetProcAddress) real_glXGetProcAddress = reinterpret_cast<GetProcFn>(p);
            return reinterpret_cast<void*>(&hk_glXGetProcAddress);
        }
        return p;
    }
    return MaybeWrapGl(name, p);
}

__attribute__((constructor)) static void TsInit() {
    if (IsDisabled()) return;
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        if (strstr(exe, "java")) {
            g_rt.startTime = NowSeconds();
            fprintf(stderr, "[Toolscreen] Toolscreen %s for Linux loaded into %s\n", TS_VERSION, exe);
        }
    }
}
