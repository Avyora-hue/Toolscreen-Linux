// Toolscreen for Linux: shared declarations.
// A Linux port of the core of Toolscreen (https://github.com/jojoe77777/Toolscreen, MIT).
// Loaded into the Minecraft JVM through LD_PRELOAD; hooks GLFW and OpenGL entry points via dlsym.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#define TS_VERSION "1.4.4-linux.1"

// ---------------------------------------------------------------------------
// Logging (stderr, which MultiMC/Prism show in the instance log, plus a file)
// ---------------------------------------------------------------------------
void TsLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// ---------------------------------------------------------------------------
// Configuration (same TOML schema as Windows Toolscreen, configVersion 4)
// ---------------------------------------------------------------------------
struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};

struct GradientStop {
    Color color;
    float position = 0;
};

enum class GradientAnim { None = 0, Rotate, Slide, Wave, Spiral, Fade };

struct BackgroundConfig {
    std::string selectedMode = "color";  // color | gradient | image
    Color color;
    std::vector<GradientStop> stops;
    float angleDeg = 0;
    GradientAnim animation = GradientAnim::None;
    float animationSpeed = 1;
    bool colorFade = false;
    std::string image;
};

struct BorderConfig {
    bool enabled = false;
    Color color{1, 1, 1, 1};
    int width = 4;
    int radius = 0;
};

struct StretchConfig {
    bool enabled = false;
    int x = 0, y = 0, width = 0, height = 0;
};

// A mode dimension is an int, a 0..1 fraction of the screen, or an expression.
struct Dimension {
    enum Kind { Pixels, Relative, Expression } kind = Pixels;
    int pixels = 0;
    float relative = 0;
    std::string expr;
};

struct SourceRef {
    std::string type;  // Mirror | MirrorGroup | Image | WindowOverlay | BrowserOverlay
    std::string id;
};

struct ModeConfig {
    std::string id;
    Dimension width, height;
    std::vector<SourceRef> sources;
    BorderConfig border;
    StretchConfig stretch;
    BackgroundConfig background;
    bool sensitivityOverrideEnabled = false;
    float modeSensitivity = 1, modeSensitivityX = 1, modeSensitivityY = 1;
    bool separateXYSensitivity = false;
};

struct MirrorInput {
    std::string relativeTo = "topLeftViewport";
    int x = 0, y = 0;
};

struct MirrorOutput {
    std::string relativeTo = "centerViewport";
    int x = 0, y = 0;
    float scale = 1, scaleX = 1, scaleY = 1;
    bool separateScale = false;
    bool useRelativePosition = false;
    float relativeX = 0.5f, relativeY = 0.5f;
};

struct MirrorBorder {
    bool isStatic = false;
    int dynamicThickness = 1;
    bool staticCircle = false;
    Color staticColor{1, 1, 1, 1};
    int staticThickness = 2, staticRadius = 0, staticOffsetX = 0, staticOffsetY = 0, staticWidth = 0, staticHeight = 0;
};

struct MirrorConfig {
    std::string name;
    int captureWidth = 50, captureHeight = 50;
    std::vector<MirrorInput> inputs;
    MirrorOutput output;
    std::vector<Color> targetColors;
    Color outputColor{1, 1, 1, 1};
    Color borderColor{0, 0, 0, 1};
    float colorSensitivity = 0.001f;
    bool colorPassthrough = false;
    bool rawOutput = false;
    int fps = 30;
    float opacity = 1;
    MirrorBorder border;
    bool outputScaleAppliesInGroups = false;
};

struct MirrorGroupItem {
    std::string mirrorId;
    bool enabled = true;
    float widthPercent = 1, heightPercent = 1;
    int offsetX = 0, offsetY = 0;
};

struct MirrorGroupConfig {
    std::string name;
    MirrorOutput output;
    std::vector<MirrorGroupItem> items;
};

struct ColorKey {
    Color color;
    float sensitivity = 0.05f;
};

struct ImageConfig {
    std::string name, path;
    int x = 0, y = 0;
    std::string relativeTo = "topLeftScreen";
    float scale = 1;
    bool relativeSizing = true;
    int width = 0, height = 0;
    int cropTop = 0, cropBottom = 0, cropLeft = 0, cropRight = 0;
    bool enableColorKey = false;
    std::vector<ColorKey> colorKeys;
    float opacity = 1;
    bool bgEnabled = false;
    Color bgColor;
    float bgOpacity = 1;
    BorderConfig border;
    bool pixelatedScaling = false;
};

struct EyeZoomConfig {
    int cloneWidth = 30, cloneHeight = 1300, overlayWidth = 12;
    int windowWidth = 384, windowHeight = 16384;
    bool useCustomSizePosition = false;
    int zoomAreaWidth = 0, zoomAreaHeight = 0, positionX = 0, positionY = 0;
    int rectHeight = 50;
    bool linkRectToFont = true;
    std::string fontSizeMode = "auto";  // auto | perSquareAuto | manual
    int textFontSize = 42;
    std::string textFontPath;
    Color gridColor1{255 / 255.f, 182 / 255.f, 193 / 255.f, 1};
    Color gridColor2{173 / 255.f, 216 / 255.f, 230 / 255.f, 1};
    Color centerLineColor{1, 1, 1, 1};
    Color textColor{0, 0, 0, 1};
};

struct HotkeyConditions {
    std::vector<std::string> gameState;
    std::vector<int> exclusions;
};

struct AltSecondaryMode {
    std::vector<int> keys;
    std::string mode;
};

struct HotkeyConfig {
    std::vector<int> keys;
    std::string mainMode, secondaryMode;
    std::vector<AltSecondaryMode> altSecondaryModes;
    HotkeyConditions conditions;
    int debounce = 100;
    bool triggerOnRelease = false, triggerOnHold = false, blockKeyFromGame = false;
    bool allowExitToFullscreenRegardlessOfGameState = false;
};

struct SensitivityHotkeyConfig {
    std::vector<int> keys;
    float sensitivity = 1, sensitivityX = 1, sensitivityY = 1;
    bool separateXY = false, toggle = false;
    HotkeyConditions conditions;
    int debounce = 100;
};

struct KeyRebind {
    int fromKey = 0, toKey = 0;
    bool enabled = true;
    std::string cursorState = "any";  // any | cursor_free | cursor_grabbed
};

struct NinjabrainColumn {
    std::string id, header;
    bool show = true;
    int staticWidth = 0;
};

// [ninjabrainOverlay]: live panel fed by Ninjabrain Bot's HTTP API (Settings > Advanced > Enable API).
struct NinjabrainOverlayConfig {
    bool enabled = false;
    std::string apiBaseUrl = "http://127.0.0.1:52533";
    std::vector<std::string> allowedModes;
    bool alwaysShow = true, alwaysShowBoat = false;
    bool hideIfStale = false;
    int hideIfStaleDelaySeconds = 30;
    std::string relativeTo = "topLeftScreen";
    int x = 0, y = 0;
    float overlayScale = 0.3f, overlayOpacity = 1;
    bool bgEnabled = true;
    Color bgColor{55 / 255.f, 60 / 255.f, 66 / 255.f, 1};
    float bgOpacity = 1;
    Color textColor{229 / 255.f, 229 / 255.f, 229 / 255.f, 1};
    Color dataColor{1, 1, 1, 1};
    Color titleTextColor{1, 1, 1, 1};
    Color headerFillColor{45 / 255.f, 50 / 255.f, 56 / 255.f, 1};
    Color chromeColor{46 / 255.f, 51 / 255.f, 61 / 255.f, 1};
    Color dividerColor{42 / 255.f, 46 / 255.f, 50 / 255.f, 1};
    Color throwsBackgroundColor{51 / 255.f, 56 / 255.f, 61 / 255.f, 1};
    Color throwsTextColor{192 / 255.f, 192 / 255.f, 192 / 255.f, 1};
    Color certaintyColor{0, 206 / 255.f, 41 / 255.f, 1};
    Color certaintyMidColor{1, 1, 0, 1};
    Color certaintyLowColor{1, 0, 0, 1};
    Color coordPositiveColor{1, 1, 1, 1}, coordNegativeColor{1, 1, 1, 1};
    Color subpixelPositiveColor{117 / 255.f, 204 / 255.f, 108 / 255.f, 1};
    Color subpixelNegativeColor{204 / 255.f, 110 / 255.f, 114 / 255.f, 1};
    Color borderColor{71 / 255.f, 74 / 255.f, 79 / 255.f, 1};
    int borderWidth = 0;
    std::string coordsDisplay = "block";  // block | chunk
    int shownPredictions = 5, eyeThrowRows = 3;
    bool showThrowDetails = true, showTitleBar = false, showRowStripes = true, showSeparators = true;
    bool showDirectionToStronghold = true, showBoatStateInTopBar = true;
    std::string titleText = "Ninjabrain Bot";
    std::vector<NinjabrainColumn> columns;
    // Linux-port additions: stay hidden until an F3+C measurement arrives (after the current world was
    // entered), and hide whenever the game is not in a world (title / MCSR Ranked menus / wall / generating).
    bool showOnlyAfterMeasurement = false;
    bool hideInMenus = false;
};

struct Config {
    std::string defaultMode = "Fullscreen";
    std::vector<int> guiHotkey{162, 73};
    std::vector<int> imageOverlaysHotkey, ninjabrainOverlayHotkey;
    float mouseSensitivity = 1;
    std::string mirrorMatchColorspace = "Auto";
    int fpsLimit = 0;
    int startupIndicatorMode = 1;
    std::string fontPath;
    std::vector<ModeConfig> modes;
    std::vector<MirrorConfig> mirrors;
    std::vector<MirrorGroupConfig> mirrorGroups;
    std::vector<ImageConfig> images;
    EyeZoomConfig eyezoom;
    std::vector<HotkeyConfig> hotkeys;
    std::vector<SensitivityHotkeyConfig> sensitivityHotkeys;
    bool keyRebindsEnabled = false;
    std::vector<KeyRebind> rebinds;
    std::vector<int> rebindToggleHotkey;
    NinjabrainOverlayConfig ninjabrain;
};

// Loads the config (writing the screen-patched default the first time). Returns false on parse error,
// in which case `out` keeps its previous contents and `err` holds the message.
bool LoadConfig(Config& out, std::string& err, int screenW, int screenH);
std::string ConfigPath();
std::string ConfigDir();
const ModeConfig* FindMode(const Config& cfg, const std::string& id);
void ResolveModeSize(const Config& cfg, const ModeConfig& mode, int screenW, int screenH, int& w, int& h);

// ---------------------------------------------------------------------------
// Runtime state shared between the hook, input and render units
// ---------------------------------------------------------------------------
struct Runtime {
    std::mutex cfgMutex;  // guards cfg against hot reload (taken on the render/main thread only)
    Config cfg;
    bool cfgLoaded = false;
    std::string cfgError;
    std::string cfgErrorShown;

    std::string currentMode = "Fullscreen";
    std::string pendingMode;  // set by hotkeys, applied on the main thread in glfwPollEvents
    bool modeDirty = false;

    // Physical framebuffer (screen) size and the size the game was last told about.
    int screenW = 0, screenH = 0;
    int gameW = 0, gameH = 0;
    bool gameSizeFaked = false;

    // Where the game frame is presented on screen (top-left origin).
    int destX = 0, destY = 0, destW = 0, destH = 0;

    bool cursorDisabled = false;
    bool guiOpen = false;
    bool imagesVisible = true;
    bool ninjabrainVisible = true;
    // Ninjabrain panel hotkey override for the current world: 0 = automatic, 1 = forced shown, -1 = forced hidden.
    int nbOverride = 0;
    bool nbDisplayed = false;  // whether the panel was drawn last frame

    // Sensitivity overrides from [[sensitivityHotkey]]
    bool sensOverrideActive = false;
    float sensOverrideX = 1, sensOverrideY = 1;

    double startTime = 0;
};

extern Runtime g_rt;

double NowSeconds();

// Game state as reported by the state-output mod (wpstateout.txt) or hermes.
void StartGameStateThread();
std::string GetGameState();  // e.g. "title", "wall", "generating", "inworld,cursor_grabbed"
const std::string& GetGameStateRef();  // main thread only; no lock unless the state changed

// Ninjabrain Bot API client (ninjabrain.cpp)
struct NbThrow {
    double x = 0, z = 0, angle = 0, angleWithoutCorrection = 0, correction = 0, error = 0;
    int correctionIncrements = 0;
    bool hasIncrements = false;
};
struct NbPrediction {
    int chunkX = 0, chunkZ = 0;
    double certainty = 0, overworldDistance = 0;
};
struct NbMessage {
    std::string severity, type, message;
};
struct NinjabrainData {
    bool connected = false;
    double lastUpdate = 0;
    double lastMeasurement = 0;  // time of the latest F3+C measurement (new eye throw or blind result)
    std::string resultType = "NONE";  // NONE | TRIANGULATION | FAILED | BLIND | DIVINE
    bool hasPlayer = false, playerInNether = false;
    double playerX = 0, playerZ = 0, playerAngle = 0;
    std::vector<NbThrow> throws;
    std::vector<NbPrediction> predictions;
    std::vector<NbMessage> messages;
    std::string boatState = "NONE";
    bool blindEnabled = false, hasBlindResult = false;
    std::string blindEvaluation;
    double blindX = 0, blindZ = 0, blindHighroll = 0, blindHighrollThreshold = 0, blindImproveDistance = 0;
    std::string blindImproveDirection;
};
void NinjabrainSetUrl(const std::string& baseUrl);  // starts the client threads on first call
NinjabrainData NinjabrainSnapshot();
// Just the fields the panel's show/hide decision needs (no copies of the tables).
struct NinjabrainFlags {
    bool connected = false, hasData = false;
    double lastUpdate = 0, lastMeasurement = 0;
};
NinjabrainFlags NinjabrainFlagsNow();

// Input unit
bool InputOnKey(int glfwKey, int scancode, int action, int mods, int& outKey);  // false = swallow
bool InputOnMouseButton(int button, int action, int mods);                     // false = swallow
void InputGetSensitivity(float& sx, float& sy);
void InputPerFrame();
int GlfwKeyToVk(int glfwKey);
int VkToGlfwKey(int vk);
bool IsVkHeld(int vk);

// Display name of a key combination (e.g. "LCtrl+I").
std::string VkComboName(const std::vector<int>& keys);
// Opens a file or folder with the desktop's default application (xdg-open).
void TsOpenPath(const std::string& path);
// Clipboard through the game's GLFW.
const char* TsGetClipboard();
void TsSetClipboard(const char* text);

// Per-frame cost measurement (TOOLSCREEN_PERF=1 logs averages every 5 seconds).
struct PerfStats {
    double renderCpu = 0, renderCpuMax = 0, pollCpu = 0;
    double gpuMs = 0;
    int gpuSamples = 0, frames = 0, renderedFrames = 0, fallbackFrames = 0;
    // Stage timings inside the overlay pass (ms, summed) and their worst single frame.
    double stage[8] = {0}, stageMax[8] = {0};
};
const char* const kPerfStageNames[8] = {"init+save", "frame source", "background", "eyezoom", "sources", "ninjabrain", "gui/toast", "restore"};
extern PerfStats g_perf;
bool PerfEnabled();

// Render unit
void RenderInitIfNeeded();
void RenderFrame();  // called from glfwSwapBuffers before the real swap
void RenderNoteGameTextureAttach(unsigned int tex);
void RenderNoteTextureImage(unsigned int tex, int w, int h);
void RenderNoteTextureDeleted(unsigned int tex);
void RenderShowToast(const std::string& text, double seconds);

// Screen rectangle of an overlay drawn this frame, recorded while the editor is open so the on-screen
// overlay editor can move and resize it. Groups record their own box (union of their mirrors).
enum class EditKind { Mirror, Group, Image, Ninjabrain };
struct EditRect {
    EditKind kind;
    std::string name;
    float x, y, w, h;
};
const std::vector<EditRect>& RenderEditRects();
// Anchor maths of the overlays, and their inverses (screen position -> config offset).
void RelativeCoords(const std::string& type, int relX, int relY, int w, int h, int cw, int ch, int& outX, int& outY);
void RelativeCoordsInverse(const std::string& type, int x, int y, int w, int h, int cw, int ch, int& relX, int& relY);
void ImageCoords(const std::string& type, int relX, int relY, int w, int h, int& outX, int& outY);
void ImageCoordsInverse(const std::string& type, int x, int y, int w, int h, int& relX, int& relY);

// Mode switching
void RequestModeSwitch(const std::string& mode);
void ApplyModeLayout();  // recompute dest rect for the current mode/screen
