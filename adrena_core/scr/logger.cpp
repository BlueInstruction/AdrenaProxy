#include "adrena_core/logger.h"
#include <windows.h>
#include <cstdio>

namespace adrena {

Logger& Logger::Instance() {
    static Logger inst;
    return inst;
}

Logger::~Logger() { Shutdown(); }

void Logger::Init(const char* filePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return;
#ifdef _MSC_VER
    m_file = _fsopen(filePath, "w", _SH_DENYNO);
    if (!m_file) m_file = _fsopen("adrena_proxy.log", "w", _SH_DENYNO);
#else
    m_file = std::fopen(filePath, "w");
    if (!m_file) m_file = std::fopen("adrena_proxy.log", "w");
#endif
    m_initialized = (m_file != nullptr);
    if (m_initialized) {
        std::fprintf(m_file, "[INFO ] === AdrenaProxy v2.0 Logger Initialized ===\n");
        Flush();
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file) {
        std::fprintf(m_file, "[INFO ] Logger shutting down\n");
        std::fclose(m_file);
        m_file = nullptr;
    }
    m_initialized = false;
}

void Logger::Log(const char* fmt, ...) {
    if (!m_initialized) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::fprintf(m_file, "[%02d:%02d:%02d.%03d] ",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(m_file, fmt, args);
    va_end(args);
    std::fprintf(m_file, "\n");
    Flush();
}

void Logger::Flush() {
    if (m_file) std::fflush(m_file);
}

} // namespace adrena