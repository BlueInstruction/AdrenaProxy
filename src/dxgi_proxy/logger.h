#pragma once
#include <cstdio>
#include <cstdarg>
#include <string>

namespace adrena {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static void Init(LogLevel level);
    static void Log(LogLevel level, const char* fmt, ...);
    static void Shutdown();

private:
    static LogLevel s_level;
    static FILE*    s_file;
    static bool     s_initialized;
    static const char* LevelStr(LogLevel level);
};

#define AD_LOG_D(fmt, ...) adrena::Logger::Log(adrena::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define AD_LOG_I(fmt, ...) adrena::Logger::Log(adrena::LogLevel::Info,  fmt, ##__VA_ARGS__)
#define AD_LOG_W(fmt, ...) adrena::Logger::Log(adrena::LogLevel::Warn,  fmt, ##__VA_ARGS__)
#define AD_LOG_E(fmt, ...) adrena::Logger::Log(adrena::LogLevel::Error, fmt, ##__VA_ARGS__)

} // namespace adrena
