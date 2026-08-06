#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "Engine/Logger.h"

#pragma comment(lib, "ws2_32.lib")

// AI Control Interface
// Runs an HTTP/JSON server on a background thread.
// Read/Write operations are dispatched to the main thread via a request queue,
// because D3D context and rendering state live on the main thread only.
//
// Endpoints:
//   GET  /api/state        -> JSON snapshot of cubes, windows, virtual desktops
//   POST /api/action       -> execute an action: { "action": "...", ... }
//   GET  /api/screenshot   -> PNG of current frame (base64 in JSON)
//
// Port: default 52317, override with env CROSSDIM_PORT.

namespace AIServer {

// --- Main-thread interface (call from render loop) ---
// The HTTP thread publishes requests via atomics; the main thread consumes
// them periodically in the render loop and writes back results.

inline std::mutex g_reqMutex;
inline std::atomic<bool> g_finished{false};

inline std::string g_sharedStateJson;         // published by main thread (mutex-protected)
inline std::string g_pendingAction;           // action JSON written by HTTP thread
inline std::atomic<bool> g_actionPending{false};
inline std::string g_actionResult;
inline std::atomic<bool> g_actionResultReady{false};
inline std::atomic<bool> g_screenshotRequested{false};
inline std::string g_screenshotResult;        // JSON containing base64 PNG
inline std::atomic<bool> g_screenshotReady{false};
inline std::atomic<int> g_screenshotMaxDim{0}; // 0 = full res, else max edge length
inline std::atomic<bool> g_screenshotRawMode{false}; // true = store raw bytes not base64 JSON
inline std::string g_screenshotRaw;           // raw BMP bytes (when RawMode)
inline std::string g_screenshotRawMime;       // "image/bmp"

// --- Event stream (perception layer) ---
// A ring buffer of JSON events describing desktop changes. AI can poll
// /api/events?since=<seq> to incrementally perceive what changed.
inline std::mutex g_evtMutex;
inline std::vector<std::string> g_events;      // oldest first
inline long long g_eventSeq = 0;               // global sequence counter
inline const size_t kMaxEvents = 500;          // drop oldest beyond this

// Emit an event. Thread-safe; call from anywhere (main thread recommended).
// type: e.g. window_open, folder_enter, desktop_switch, launch, error
// data: optional JSON object/string describing the event.
static void EmitEvent(const std::string& type, const std::string& data = "") {
    std::lock_guard<std::mutex> lock(g_evtMutex);
    long long seq = ++g_eventSeq;
    std::string ev = "{\"seq\":" + std::to_string(seq) + ",\"type\":\"" + type + "\"";
    if (!data.empty()) ev += ",\"data\":" + data;
    ev += "}";
    g_events.push_back(ev);
    if (g_events.size() > kMaxEvents) {
        g_events.erase(g_events.begin(), g_events.end() - kMaxEvents);
    }
}

// --- HTTP thread state ---
inline std::atomic<bool> g_srvRunning{false};
inline std::atomic<int> g_srvPort{52317};

// --- Encoding helpers ---
static std::string UrlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            char hex[3] = { in[i+1], in[i+2], 0 };
            out += (char)strtoul(hex, nullptr, 16);
            i += 2;
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

static std::string HttpEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (c == '"' || c == '\\') out += '\\';
        out += (char)c;
    }
    return out;
}

// Extract path from request line. Returns e.g. "/api/state"
static std::string GetRequestPath(const std::string& reqText) {
    size_t sp = reqText.find(' ');
    if (sp == std::string::npos) return "";
    size_t sp2 = reqText.find(' ', sp + 1);
    if (sp2 == std::string::npos) return "";
    return reqText.substr(sp + 1, sp2 - sp - 1);
}

// Read request body after "\r\n\r\n" (Content-Length aware is better; simple for now)
static std::string GetRequestBody(const std::string& reqText) {
    size_t pos = reqText.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return reqText.substr(pos + 4);
}

// Send a full HTTP response
static void SendHttp(SOCKET sock, int statusCode, const char* statusText,
                     const std::string& body, const std::string& contentType = "application/json") {
    char head[512];
    sprintf_s(head, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
              statusCode, statusText, contentType.c_str(), (unsigned)body.size());
    send(sock, head, (int)strlen(head), 0);
    send(sock, body.data(), (int)body.size(), 0);
}

// Raise an event window-message to the main window so it can process pending actions.
// (Optional callable stub - main.cpp wires this.)
static void(*g_NotifyMainFn)(void) = nullptr;
static void SetNotifyMain(void(*fn)(void)) { g_NotifyMainFn = fn; }

static void HandleConnection(SOCKET sock) {
    char buf[8192];
    int received = recv(sock, buf, sizeof(buf) - 1, 0);
    if (received <= 0) { closesocket(sock); return; }
    buf[received] = '\0';
    std::string reqText(buf, received);
    std::string path = GetRequestPath(reqText);

    if (path.rfind("/api/help", 0) == 0) {
        const char* help = "{\"endpoints\":[\"/api/help\",\"/api/state\",\"/api/action\",\"/api/screenshot?scale=N\",\"/api/events?since=N\"],"
            "\"actions\":[\"ping\",\"launch\",\"switch_desktop\",\"create_desktop\",\"close_desktop\","
            "\"open_folder\",\"exit_folder\",\"focus_window\",\"select\",\"deselect_all\",\"delete_selected\","
            "\"reload_apps\",\"toggle_mode\",\"set_camera\",\"set_model\",\"reset_model\",\"set_volume\",\"get_log\"]}";
        SendHttp(sock, 200, "OK", help);
    }
    else if (path.rfind("/api/events", 0) == 0) {
        // Return events with seq > since. Also supports long-poll: ?wait_ms=N
        long long since = 0;
        int waitMs = 0;
        size_t q = path.find('?');
        if (q != std::string::npos) {
            std::string query = path.substr(q + 1);
            size_t s = query.find("since=");
            if (s != std::string::npos) since = _strtoi64(query.c_str() + s + 6, nullptr, 10);
            size_t w = query.find("wait=");
            if (w != std::string::npos) waitMs = atoi(query.c_str() + w + 5);
        }
        // Long-poll: wait up to waitMs for new events beyond 'since'
        if (waitMs > 0) {
            int waited = 0;
            for (;;) {
                {
                    std::lock_guard<std::mutex> lock(g_evtMutex);
                    if (g_eventSeq > since) break;
                }
                if (waited >= waitMs) break;
                Sleep(50);
                waited += 50;
            }
        }
        std::string body = "{\"since\":" + std::to_string(since) + ",\"events\":[";
        {
            std::lock_guard<std::mutex> lock(g_evtMutex);
            bool first = true;
            for (const auto& ev : g_events) {
                // Extract seq from "seq":N to filter (simple approach: parse leading seq)
                size_t pos = ev.find("\"seq\":");
                long long evSeq = 0;
                if (pos != std::string::npos) {
                    evSeq = _strtoi64(ev.c_str() + pos + 6, nullptr, 10);
                }
                if (evSeq <= since) continue;
                if (!first) body += ",";
                body += ev;
                first = false;
            }
        }
        body += "]}";
        SendHttp(sock, 200, "OK", body);
    }
    else if (path.rfind("/api/state", 0) == 0) {
        std::lock_guard<std::mutex> lock(g_reqMutex);
        std::string body = g_sharedStateJson.empty() ? "{\"error\":\"state not ready\"}" : g_sharedStateJson;
        SendHttp(sock, 200, "OK", body);
    }
    else if (path.rfind("/api/action", 0) == 0) {
        std::string body = GetRequestBody(reqText);
        {
            std::lock_guard<std::mutex> lock(g_reqMutex);
            g_pendingAction = body;
            g_actionPending.store(true);
        }
        if (g_NotifyMainFn) g_NotifyMainFn();
        // Wait for result (with timeout) - main thread processes and signals.
        int waits = 0;
        while (!g_actionResultReady.load() && waits < 200) {
            Sleep(10);
            waits++;
        }
        std::string result;
        {
            std::lock_guard<std::mutex> lock(g_reqMutex);
            result = g_actionResultReady.load() ? g_actionResult : "{\"error\":\"timeout\"}";
            g_actionResultReady.store(false);
        }
        SendHttp(sock, 200, "OK", result);
    }
    else if (path.rfind("/api/screenshot", 0) == 0) {
        // Parse query params: scale=N, raw=1
        int maxDim = 0;
        bool rawMode = false;
        size_t q = path.find('?');
        if (q != std::string::npos) {
            std::string query = path.substr(q + 1);
            size_t s = query.find("scale=");
            if (s != std::string::npos) maxDim = atoi(query.c_str() + s + 6);
            size_t r = query.find("raw=");
            if (r != std::string::npos && strstr(query.c_str() + r + 4, "1") != nullptr) rawMode = true;
        }
        g_screenshotMaxDim.store(maxDim);
        g_screenshotRawMode.store(rawMode);
        g_screenshotRequested.store(true);
        if (g_NotifyMainFn) g_NotifyMainFn();
        int waits = 0;
        while (!g_screenshotReady.load() && waits < 300) {
            Sleep(10);
            waits++;
        }
        std::string result;
        if (rawMode) {
            std::string body;
            std::string errJson;
            {
                std::lock_guard<std::mutex> lock(g_reqMutex);
                body = g_screenshotReady.load() ? g_screenshotRaw : "";
                errJson = g_screenshotResult;  // may hold error JSON when capture failed
                g_screenshotReady.store(false);
                g_screenshotRaw.clear();
                g_screenshotResult.clear();
            }
            if (!body.empty()) {
                char head[256];
                sprintf_s(head, "HTTP/1.1 200 OK\r\nContent-Type: image/bmp\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                          (unsigned)body.size());
                send(sock, head, (int)strlen(head), 0);
                send(sock, body.data(), (int)body.size(), 0);
            } else if (!errJson.empty()) {
                SendHttp(sock, 200, "OK", errJson);  // report error reason as JSON
            } else {
                SendHttp(sock, 500, "Internal Server Error", "screenshot failed");
            }
        } else {
            {
                std::lock_guard<std::mutex> lock(g_reqMutex);
                result = g_screenshotReady.load() ? g_screenshotResult : "{\"error\":\"screenshot timeout\"}";
                g_screenshotReady.store(false);
                g_screenshotResult.clear();
            }
            SendHttp(sock, 200, "OK", result);
        }
    }
    else {
        SendHttp(sock, 404, "Not Found", "{\"error\":\"unknown endpoint\"}");
    }

    closesocket(sock);
}

static void ServerThreadProc() {
    int port = g_srvPort.load();
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) { WSACleanup(); return; }

    BOOL reuse = TRUE;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listenSock);
        WSACleanup();
        return;
    }
    if (listen(listenSock, 8) == SOCKET_ERROR) {
        closesocket(listenSock);
        WSACleanup();
        return;
    }

    g_srvRunning.store(true);
    LOG("[ai] AIServer listening on 127.0.0.1:%d", port);

    while (!g_finished.load()) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listenSock, &readfds);
        timeval tv = { 0, 200000 };  // 200ms poll for shutdown
        int sel = select(0, &readfds, nullptr, nullptr, &tv);
        if (sel == SOCKET_ERROR) break;
        if (sel > 0 && FD_ISSET(listenSock, &readfds)) {
            SOCKET client = accept(listenSock, nullptr, nullptr);
            if (client != INVALID_SOCKET) {
                // Handle synchronously (one at a time) for simplicity in this milestone.
                HandleConnection(client);
            }
        }
    }

    closesocket(listenSock);
    WSACleanup();
}

// Start the server. Call once during init.
static void Start(int port = 0) {
    if (g_srvRunning.load()) return;
    if (port == 0) {
        const char* env = getenv("CROSSDIM_PORT");
        port = env ? atoi(env) : 52317;
        if (port <= 0 || port > 65535) port = 52317;
    }
    g_srvPort.store(port);
    g_finished.store(false);
    std::thread t(ServerThreadProc);
    t.detach();
}

// Stop the server. Call during cleanup.
static void Stop() {
    g_finished.store(true);
    Sleep(300);
    g_srvRunning.store(false);
}

} // namespace AIServer
