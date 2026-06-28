#pragma once
// -----------------------------------------------------------------------
// VansLog  –  Lightweight engine-wide logging utility.
//
//   VANS_LOG("loaded mesh: " << name);
//   VANS_LOG_WARN("texture missing: " << path);
//   VANS_LOG_ERROR("shader compile failed: " << err);
//
// * Thread-safe
// * Writes every entry to a timestamped file inside a LOG/ folder
//   next to the executable.
// * Broadcasts entries to registered sinks such as the editor console.
// -----------------------------------------------------------------------

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#include "ILogSink.h"

enum class VansLogLevel
{
    Info,
    Warning,
    Error
};

class VansLog
{
public:
    static VansLog& Get()
    {
        static VansLog instance;
        return instance;
    }

    /// Call once at startup (optional – lazy-inits on first log otherwise).
    void Init();

    /// Core logging function.
    void Log(VansLogLevel level, const std::string& msg);
    void Log(VansLogChannel channel, VansLogLevel level, const std::string& msg);
    void RegisterSink(ILogSink* sink);
    void UnregisterSink(ILogSink* sink);

private:
    VansLog() = default;
    ~VansLog();

    void EnsureInitialized();

    std::mutex  m_Mutex;
    std::ofstream m_File;
    bool m_Initialized = false;
    std::vector<ILogSink*> m_Sinks;
};

// -----------------------------------------------------------------------
// Convenience macros  – support << streaming syntax
// -----------------------------------------------------------------------
#define VANS_LOG(msg)                                           \
    do {                                                        \
        std::ostringstream _vans_oss;                           \
        _vans_oss << msg;                                       \
        VansLog::Get().Log(VansLogLevel::Info, _vans_oss.str());\
    } while (0)

#define VANS_LOG_WARN(msg)                                          \
    do {                                                            \
        std::ostringstream _vans_oss;                               \
        _vans_oss << msg;                                           \
        VansLog::Get().Log(VansLogLevel::Warning, _vans_oss.str()); \
    } while (0)

#define VANS_LOG_ERROR(msg)                                         \
    do {                                                            \
        std::ostringstream _vans_oss;                               \
        _vans_oss << msg;                                           \
        VansLog::Get().Log(VansLogLevel::Error, _vans_oss.str());   \
    } while (0)

#define VANS_LOG_PYTHON(msg)                                                       \
    do {                                                                           \
        std::ostringstream _vans_oss;                                              \
        _vans_oss << msg;                                                          \
        VansLog::Get().Log(VansLogChannel::Python, VansLogLevel::Info, _vans_oss.str()); \
    } while (0)
