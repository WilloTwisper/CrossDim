#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <algorithm>

class Logger {
public:
    static Logger& Instance() {
        static Logger s;
        return s;
    }
    
    void Init(const std::string& logPath = "") {
        if (m_inited) return;
        m_inited = true;

        std::string dir;
        if (!logPath.empty()) {
            dir = logPath;
        } else {
            WCHAR exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            std::wstring wdir(exePath);
            size_t pos = wdir.rfind(L'\\');
            if (pos != std::wstring::npos) wdir = wdir.substr(0, pos);
            wdir += L"\\logs";
            CreateDirectoryW(wdir.c_str(), nullptr);
            int len = WideCharToMultiByte(CP_UTF8, 0, wdir.c_str(), (int)wdir.size(), nullptr, 0, nullptr, nullptr);
            dir.resize(len);
            WideCharToMultiByte(CP_UTF8, 0, wdir.c_str(), (int)wdir.size(), &dir[0], len, nullptr, nullptr);
        }

        SYSTEMTIME st; GetLocalTime(&st);
        char buf[512];
        sprintf_s(buf, "%s\\crossdim_%04u%02u%02u_%02u%02u%02u.log",
            dir.c_str(),
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        m_file = fopen(buf, "w");
        if (m_file) {
            setvbuf(m_file, nullptr, _IONBF, 0);
        }
        m_logDir = dir;

        PruneOldLogs();

        Log("=== CrossDim Logger Started ===%s", m_file ? "" : " [file failed, DebugView only]");
    }
    
    void Shutdown() {
        if (m_file) {
            Log("=== CrossDim Logger Shutdown ===");
            fclose(m_file);
            m_file = nullptr;
        }
        m_inited = false;
    }
    
    void Log(const char* fmt, ...) {
        char msg[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);
        
        ULONGLONG ms = GetTickCount64();
        unsigned int sec = (unsigned int)(ms / 1000);
        unsigned int msec = (unsigned int)(ms % 1000);
        
        char line[1280];
        sprintf_s(line, "[%5u.%03u] %s\n", sec, msec, msg);
        
        OutputDebugStringA(line);
        if (m_file) {
            fputs(line, m_file);
        }
    }
    
    void LogW(const wchar_t* fmt, ...) {
        wchar_t msg[1024];
        va_list args;
        va_start(args, fmt);
        _vsnwprintf_s(msg, sizeof(msg)/sizeof(wchar_t), sizeof(msg)/sizeof(wchar_t) - 1, fmt, args);
        va_end(args);
        
        char narrow[1024];
        WideCharToMultiByte(CP_UTF8, 0, msg, -1, narrow, sizeof(narrow), nullptr, nullptr);
        Log("%s", narrow);
    }
    
private:
    Logger() = default;
    bool m_inited = false;
    FILE* m_file = nullptr;
    std::string m_logDir;

    void PruneOldLogs() {
        if (m_logDir.empty()) return;
        WCHAR wdir[MAX_PATH];
        int wlen = MultiByteToWideChar(CP_UTF8, 0, m_logDir.c_str(), (int)m_logDir.size(), wdir, MAX_PATH);
        if (wlen <= 0) return;
        wdir[wlen] = L'\0';

        std::wstring pattern = std::wstring(wdir) + L"\\crossdim_*.log";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;

        std::vector<std::wstring> files;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(std::wstring(wdir) + L"\\" + fd.cFileName);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);

        std::sort(files.begin(), files.end());
        constexpr int KEEP_COUNT = 10;
        for (int i = 0; i < (int)files.size() - KEEP_COUNT; ++i) {
            DeleteFileW(files[i].c_str());
        }
    }
};

#define LOG(fmt, ...) Logger::Instance().Log(fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) Logger::Instance().LogW(fmt, ##__VA_ARGS__)
