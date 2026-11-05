// Game state from either source upstream Toolscreen reads, polled on a background thread:
//  - Hermes: <game dir>/hermes/state.json, used while <game dir>/hermes/alive carries this process's pid
//    and a heartbeat under 3 s old;
//  - State Output: <game dir>/wpstateout.txt.
// Hermes wins when both are present; the choice is re-made once a second.
#include "ts.h"

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <thread>

#include "../third_party/json.hpp"

namespace {

std::mutex g_stateMutex;
std::string g_state;
std::atomic<unsigned> g_stateVersion{0};

enum class Source { None, Hermes, StateOutput };

constexpr long long kHermesStaleMs = 3000;

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::optional<std::string> StateFromStateOutput(const std::string& text) {
    std::string c = Trim(text);
    if (c.empty()) return std::nullopt;
    if (c.rfind("generating", 0) == 0 || c.rfind("previewing", 0) == 0) return std::string("generating");
    if (c == "inworld,unpaused") return std::string("inworld,cursor_grabbed");
    if (c == "inworld,paused" || c == "inworld,gamescreenopen") return std::string("inworld,cursor_free");
    if (c == "wall" || c == "title" || c == "waiting" || c == "inworld,cursor_free" || c == "inworld,cursor_grabbed") return c;
    return std::nullopt;
}

// Hermes reports the open screen's class; the level-loading screen and the SeedQueue wall show while a
// world is already set, so they are checked before the world.
std::optional<std::string> StateFromHermes(const std::string& text) {
    if (text.empty()) return std::nullopt;
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    bool inWorld = j.contains("world") && !j["world"].is_null();
    std::string screen;
    auto s = j.find("screen");
    if (s != j.end() && s->is_object()) {
        auto c = s->find("class");
        if (c != s->end() && c->is_string()) screen = c->get<std::string>();
    }
    if (screen == "net.minecraft.class_3928") return std::string("generating");
    if (screen == "me.contaria.seedqueue.gui.wall.SeedQueueWallScreen" || screen == "me.contaria.seedqueue.gui.wall.SeedQueuePreview")
        return std::string("wall");
    if (inWorld) return std::string(screen.empty() ? "inworld,cursor_grabbed" : "inworld,cursor_free");
    return std::string("title");
}

unsigned long long ReadBigEndian64(const unsigned char* b) {
    unsigned long long v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | b[i];
    return v;
}

bool HermesAlive(const std::string& alivePath) {
    FILE* f = fopen(alivePath.c_str(), "rb");
    if (!f) return false;
    unsigned char b[16];
    bool ok = fread(b, 1, 16, f) == 16;
    fclose(f);
    if (!ok) return false;
    if (ReadBigEndian64(b) != static_cast<unsigned long long>(getpid())) return false;
    long long beat = static_cast<long long>(ReadBigEndian64(b + 8));
    long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return std::llabs(now - beat) < kHermesStaleMs;
}

bool IsRegularFile(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

const char* SourceName(Source s) { return s == Source::Hermes ? "Hermes" : s == Source::StateOutput ? "State Output" : "none"; }

void PollLoop(std::string dir) {
    const std::string hermesState = dir + "/hermes/state.json", hermesAlive = dir + "/hermes/alive", stateOutput = dir + "/wpstateout.txt";
    Source active = Source::None, logged = Source::None;
    auto lastSourceCheck = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    struct timespec lastMtime = {0, 0};
    off_t lastSize = -1;
    Source readSource = Source::None;
    std::string buf;
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        auto now = std::chrono::steady_clock::now();
        if (now - lastSourceCheck >= std::chrono::seconds(1)) {
            lastSourceCheck = now;
            active = HermesAlive(hermesAlive) ? Source::Hermes : IsRegularFile(stateOutput) ? Source::StateOutput : Source::None;
            if (active != logged) {
                TsLog("game state source: %s", SourceName(active));
                logged = active;
            }
        }
        if (active == Source::None) continue;
        const std::string& path = active == Source::Hermes ? hermesState : stateOutput;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || st.st_size <= 0 || st.st_size >= 65536) continue;
        if (readSource == active && st.st_mtim.tv_sec == lastMtime.tv_sec && st.st_mtim.tv_nsec == lastMtime.tv_nsec && st.st_size == lastSize)
            continue;
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) continue;
        buf.resize(static_cast<size_t>(st.st_size));
        size_t n = fread(buf.data(), 1, buf.size(), f);
        fclose(f);
        buf.resize(n);
        std::optional<std::string> state = active == Source::Hermes ? StateFromHermes(buf) : StateFromStateOutput(buf);
        // A half-written file is retried on the next poll instead of being marked as read.
        if (!state) continue;
        lastMtime = st.st_mtim;
        lastSize = st.st_size;
        readSource = active;
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (*state != g_state) {
            g_state = *state;
            g_stateVersion++;
        }
    }
}

}  // namespace

void StartGameStateThread() {
    static bool started = false;
    if (started) return;
    started = true;
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) return;
    TsLog("game state: %s/hermes/state.json or %s/wpstateout.txt", cwd, cwd);
    std::thread(PollLoop, std::string(cwd)).detach();
}

std::string GetGameState() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_state;
}

// Main-thread copy, refreshed only when the poller saw a change (no lock or allocation per call otherwise).
const std::string& GetGameStateRef() {
    static std::string cached;
    static unsigned seen = ~0u;
    unsigned v = g_stateVersion.load(std::memory_order_acquire);
    if (v != seen) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        cached = g_state;
        seen = v;
    }
    return cached;
}
