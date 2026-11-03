// Ninjabrain Bot API client. Ninjabrain Bot (with "Enable API" on) serves server-sent events on
// http://127.0.0.1:52533/api/v1/<topic>/events; each topic gets its own connection on a background thread,
// reconnecting every second while Ninjabrain Bot is closed.
#include "ts.h"

#include "../third_party/json.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

using json = nlohmann::json;

namespace {

std::mutex g_mutex;
NinjabrainData g_data;
std::string g_host = "127.0.0.1";
int g_port = 52533;
int g_liveConnections = 0;
bool g_strongholdPrimed = false;  // false until the first (replayed) stronghold event of a connection
bool g_blindPrimed = false;

double Num(const json& j, const char* key, double def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return def;
    return it->get<double>();
}

std::string Str(const json& j, const char* key, const std::string& def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    if (it->is_string()) return it->get<std::string>();
    return it->dump();
}

void ApplyStronghold(const json& j, NinjabrainData& d) {
    d.resultType = Str(j, "resultType", "NONE");
    d.hasPlayer = false;
    d.throws.clear();
    d.predictions.clear();
    if (d.resultType == "NONE") return;
    auto pp = j.find("playerPosition");
    if (pp != j.end() && pp->is_object() && !pp->empty()) {
        d.hasPlayer = true;
        d.playerX = Num(*pp, "xInOverworld", 0);
        d.playerZ = Num(*pp, "zInOverworld", 0);
        d.playerInNether = pp->value("isInNether", false);
        d.playerAngle = Num(*pp, "horizontalAngle", 0);
    }
    auto et = j.find("eyeThrows");
    if (et != j.end() && et->is_array()) {
        for (auto& t : *et) {
            NbThrow th;
            th.x = Num(t, "xInOverworld", d.playerX);
            th.z = Num(t, "zInOverworld", d.playerZ);
            th.angle = Num(t, "angle", 0);
            th.angleWithoutCorrection = Num(t, "angleWithoutCorrection", th.angle);
            th.correction = Num(t, "correction", 0);
            th.error = Num(t, "error", 0);
            auto ci = t.find("correctionIncrements");
            if (ci != t.end() && ci->is_number()) {
                th.correctionIncrements = ci->get<int>();
                th.hasIncrements = true;
            }
            d.throws.push_back(th);
        }
    }
    auto pr = j.find("predictions");
    if (pr != j.end() && pr->is_array()) {
        for (auto& p : *pr) {
            NbPrediction np;
            np.chunkX = static_cast<int>(Num(p, "chunkX", 0));
            np.chunkZ = static_cast<int>(Num(p, "chunkZ", 0));
            np.certainty = Num(p, "certainty", 0);
            np.overworldDistance = Num(p, "overworldDistance", 0);
            d.predictions.push_back(np);
        }
    }
}

void ApplyMessages(const json& j, NinjabrainData& d) {
    d.messages.clear();
    auto im = j.find("informationMessages");
    if (im == j.end() || !im->is_array()) return;
    for (auto& m : *im) d.messages.push_back({Str(m, "severity", "INFO"), Str(m, "type", ""), Str(m, "message", "")});
}

void ApplyBoat(const json& j, NinjabrainData& d) { d.boatState = Str(j, "boatState", "NONE"); }

void ApplyBlind(const json& j, NinjabrainData& d) {
    d.blindEnabled = j.value("isBlindModeEnabled", false);
    auto br = j.find("blindResult");
    d.hasBlindResult = br != j.end() && br->is_object() && !br->empty();
    if (!d.hasBlindResult) return;
    d.blindEvaluation = Str(*br, "evaluation", "");
    d.blindX = Num(*br, "xInNether", 0);
    d.blindZ = Num(*br, "zInNether", 0);
    d.blindHighroll = Num(*br, "highrollProbability", 0);
    d.blindHighrollThreshold = Num(*br, "highrollThreshold", 0);
    d.blindImproveDistance = Num(*br, "improveDistance", 0);
    d.blindImproveDirection = Str(*br, "improveDirection", "");
}

void Apply(const std::string& topic, const std::string& payload) {
    json j = json::parse(payload, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    double now = NowSeconds();
    if (topic == "stronghold") {
        size_t before = g_data.throws.size();
        double lastAngle = before ? g_data.throws.back().angle : 0;
        ApplyStronghold(j, g_data);
        // A measurement adds a throw or changes the latest one (re-measure / correction).
        size_t after = g_data.throws.size();
        // The first event of a connection replays Ninjabrain Bot's existing state, which is not a new measurement.
        if (g_strongholdPrimed && (after > before || (after > 0 && after == before && g_data.throws.back().angle != lastAngle))) {
            g_data.lastMeasurement = now;
        }
        g_strongholdPrimed = true;
    } else if (topic == "information-messages") {
        ApplyMessages(j, g_data);
    } else if (topic == "boat") {
        ApplyBoat(j, g_data);
    } else if (topic == "blind") {
        bool had = g_data.hasBlindResult;
        double oldX = g_data.blindX, oldZ = g_data.blindZ;
        ApplyBlind(j, g_data);
        if (g_blindPrimed && g_data.hasBlindResult && (!had || g_data.blindX != oldX || g_data.blindZ != oldZ)) g_data.lastMeasurement = now;
        g_blindPrimed = true;
    }
    g_data.lastUpdate = now;
}

int Connect(const std::string& host, int port) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return -1;
    int fd = -1;
    for (addrinfo* a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

// Reads one SSE stream until the connection closes. Handles chunked transfer encoding (Java's HttpServer
// uses it for streamed responses).
void ReadStream(int fd, const std::string& topic) {
    std::string buf, body, event;
    bool headersDone = false, chunked = false;
    size_t chunkLeft = 0;
    char tmp[8192];
    while (true) {
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return;
        buf.append(tmp, static_cast<size_t>(n));
        if (!headersDone) {
            size_t end = buf.find("\r\n\r\n");
            if (end == std::string::npos) continue;
            std::string headers = buf.substr(0, end);
            for (auto& c : headers) c = static_cast<char>(tolower(c));
            if (headers.compare(0, 12, "http/1.1 200") != 0 && headers.compare(0, 12, "http/1.0 200") != 0) return;
            chunked = headers.find("transfer-encoding: chunked") != std::string::npos;
            buf.erase(0, end + 4);
            headersDone = true;
        }
        // De-chunk into body.
        if (chunked) {
            while (true) {
                if (chunkLeft == 0) {
                    size_t eol = buf.find("\r\n");
                    if (eol == std::string::npos) break;
                    if (eol == 0) {  // CRLF after a chunk's data
                        buf.erase(0, 2);
                        continue;
                    }
                    chunkLeft = strtoul(buf.substr(0, eol).c_str(), nullptr, 16);
                    buf.erase(0, eol + 2);
                    if (chunkLeft == 0) return;  // end of stream
                }
                size_t take = std::min(chunkLeft, buf.size());
                if (take == 0) break;
                body.append(buf, 0, take);
                buf.erase(0, take);
                chunkLeft -= take;
            }
        } else {
            body += buf;
            buf.clear();
        }
        // Parse complete SSE lines; an empty line dispatches the accumulated data.
        size_t eol;
        while ((eol = body.find('\n')) != std::string::npos) {
            std::string line = body.substr(0, eol);
            body.erase(0, eol + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) {
                if (!event.empty()) Apply(topic, event);
                event.clear();
            } else if (line.compare(0, 5, "data:") == 0) {
                size_t start = (line.size() > 5 && line[5] == ' ') ? 6 : 5;
                if (!event.empty()) event += "\n";
                event += line.substr(start);
            }
        }
    }
}

void TopicLoop(std::string topic) {
    bool loggedFailure = false;
    while (true) {
        std::string host;
        int port;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            host = g_host;
            port = g_port;
        }
        int fd = Connect(host, port);
        if (fd >= 0) {
            std::string req = "GET /api/v1/" + topic + "/events HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) +
                              "\r\nAccept: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n";
            if (send(fd, req.data(), req.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(req.size())) {
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_liveConnections++;
                    g_data.connected = true;
                    if (topic == "stronghold") g_strongholdPrimed = false;
                    if (topic == "blind") g_blindPrimed = false;
                }
                if (topic == "stronghold") TsLog("connected to Ninjabrain Bot API at %s:%d", host.c_str(), port);
                loggedFailure = false;
                ReadStream(fd, topic);
                std::lock_guard<std::mutex> lock(g_mutex);
                g_liveConnections--;
                g_data.connected = g_liveConnections > 0;
            }
            close(fd);
        } else if (!loggedFailure && topic == "stronghold") {
            TsLog("Ninjabrain Bot API not reachable at %s:%d (enable the API in Ninjabrain Bot's settings)", host.c_str(), port);
            loggedFailure = true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

}  // namespace

void NinjabrainSetUrl(const std::string& baseUrl) {
    std::string u = baseUrl;
    if (u.rfind("http://", 0) == 0) u = u.substr(7);
    size_t slash = u.find('/');
    if (slash != std::string::npos) u = u.substr(0, slash);
    std::string host = u;
    int port = 52533;
    size_t colon = u.rfind(':');
    if (colon != std::string::npos) {
        host = u.substr(0, colon);
        port = atoi(u.c_str() + colon + 1);
    }
    if (host.empty()) host = "127.0.0.1";
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_host = host;
        g_port = port > 0 ? port : 52533;
    }
    static bool started = false;
    if (!started) {
        started = true;
        for (const char* t : {"stronghold", "information-messages", "boat", "blind"}) std::thread(TopicLoop, std::string(t)).detach();
    }
}

NinjabrainData NinjabrainSnapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_data;
}

NinjabrainFlags NinjabrainFlagsNow() {
    std::lock_guard<std::mutex> lock(g_mutex);
    NinjabrainFlags f;
    f.connected = g_data.connected;
    f.hasData = (g_data.resultType != "NONE" && !g_data.resultType.empty()) || g_data.hasBlindResult || !g_data.messages.empty();
    f.lastUpdate = g_data.lastUpdate;
    f.lastMeasurement = g_data.lastMeasurement;
    return f;
}
