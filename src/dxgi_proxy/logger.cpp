#include "logger.h"
#include <windows.h>

namespace adrena {

LogLevel Logger::s_level = LogLevel::Info;
FILE*    Logger::s_file = nullptr;
bool     Logger::s_initialized = false;

const char* Logger::LevelStr(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

void Logger::Init(LogLevel level)
{
    if (s_initialized) return;
    s_level = level;

    // Try to open log file next to the DLL
    char path[MAX_PATH] = {};
    HMODULE hMod = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       (LPCSTR)&Logger::Init, &hMod);
    GetModuleFileNameA(hMod, path, MAX_PATH);

    // Replace dll name with log name
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) {
        strcpy(lastSlash + 1, "adrena_proxy.log");
        s_file = fopen(path, "w");
    }

    s_initialized = true;
    Log(LogLevel::Info, "=== AdrenaProxy Logger Initialized ===");
}

void Logger::Log(LogLevel level, const char* fmt, ...)
{
    if (level < s_level) return;

    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Console output (debug builds)
#ifdef _DEBUG
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
#endif

    // File output
    if (s_file) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(s_file, "[%02d:%02d:%02d.%03d][%s] %s\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                LevelStr(level), buf);
        fflush(s_file);
    }
}

void Logger::Shutdown()
{
    if (s_file) {
        Log(LogLevel::Info, "=== AdrenaProxy Logger Shutdown ===");
        fclose(s_file);
        s_file = nullptr;
    }
    s_initialized = false;
}

} // namespace adrena
