#pragma once
#include <cstdio>
#include <cstdarg>
#include <mutex>

namespace adrena {

class Logger {
public:
    static Logger& Instance();
    void Init(const char* filePath);
    void Shutdown();
    void Log(const char* fmt, ...);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    void Flush();

    FILE*      m_file = nullptr;
    std::mutex m_mutex;
    bool       m_initialized = false;
};

} // namespace adrena

#ifdef ADRENA_LOGGING_ENABLED
#define AD_LOG_I(fmt, ...) adrena::Logger::Instance().Log("[INFO ] " fmt, ##__VA_ARGS__)
#define AD_LOG_W(fmt, ...) adrena::Logger::Instance().Log("[WARN ] " fmt, ##__VA_ARGS__)
#define AD_LOG_E(fmt, ...) adrena::Logger::Instance().Log("[ERROR] " fmt, ##__VA_ARGS__)
#else
#define AD_LOG_I(fmt, ...) ((void)0)
#define AD_LOG_W(fmt, ...) ((void)0)
#define AD_LOG_E(fmt, ...) ((void)0)
#endif