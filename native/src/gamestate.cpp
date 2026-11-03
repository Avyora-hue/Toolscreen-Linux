// Game state from the state-output mod: <game dir>/wpstateout.txt, polled on a background thread.
#include "ts.h"

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

std::mutex g_stateMutex;
std::string g_state;
std::atomic<unsigned> g_stateVersion{0};

std::string MapWpState(const std::string& raw) {
    if (raw.rfind("generating", 0) == 0 || raw.rfind("previewing", 0) == 0) return "generating";
    if (raw == "inworld,unpaused") return "inworld,cursor_grabbed";
    if (raw == "inworld,paused" || raw == "inworld,gamescreenopen") return "inworld,cursor_free";
    return raw;  // wall, title, waiting
}

void PollLoop(std::string path) {
    struct timespec lastMtime = {0, 0};
    off_t lastSize = -1;
    while (true) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0 &&
            (st.st_mtim.tv_sec != lastMtime.tv_sec || st.st_mtim.tv_nsec != lastMtime.tv_nsec || st.st_size != lastSize)) {
            lastMtime = st.st_mtim;
            lastSize = st.st_size;
            if (FILE* f = fopen(path.c_str(), "r")) {
                char buf[256] = {0};
                size_t n = fread(buf, 1, sizeof(buf) - 1, f);
                fclose(f);
                buf[n] = 0;
                while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = 0;
                if (n > 0) {
                    std::string mapped = MapWpState(buf);
                    std::lock_guard<std::mutex> lock(g_stateMutex);
                    if (mapped != g_state) {
                        g_state = mapped;
                        g_stateVersion++;
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

}  // namespace

void StartGameStateThread() {
    static bool started = false;
    if (started) return;
    started = true;
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) return;
    std::string path = std::string(cwd) + "/wpstateout.txt";
    TsLog("game state file: %s", path.c_str());
    std::thread(PollLoop, path).detach();
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
