#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <string>

class Logger {
public:
    static Logger& Instance() {
        static Logger s;
        return s;
    }
    
    void Init(const std::string& logPath = "") {
        if (m_inited) return;
        m_inited = true;
        SYSTEMTIME st; GetLocalTime(&st);
        char buf[256];
        sprintf_s(buf, "%s\\crossdim_%04u%02u%02u_%02u%02u%02u.log",
            logPath.empty() ? "." : logPath.c_str(),
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        m_file = fopen(buf, "w");
        if (m_file) {
            setvbuf(m_file, nullptr, _IONBF, 0);
        }
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
};

#define LOG(fmt, ...) Logger::Instance().Log(fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) Logger::Instance().LogW(fmt, ##__VA_ARGS__)
