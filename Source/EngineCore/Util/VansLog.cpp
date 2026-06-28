#include "VansLog.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <algorithm>

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------
static std::string GetTimestamp()
{
    std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << local.tm_hour << ":"
        << std::setw(2) << local.tm_min  << ":"
        << std::setw(2) << local.tm_sec;
    return oss.str();
}

static std::string GetDateStamp()
{
    std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream oss;
    oss << (1900 + local.tm_year) << "-"
        << std::setfill('0') << std::setw(2) << (local.tm_mon + 1) << "-"
        << std::setfill('0') << std::setw(2) << local.tm_mday;
    return oss.str();
}

static const char* LevelTag(VansLogLevel level)
{
    switch (level)
    {
    case VansLogLevel::Info:    return "[INFO]";
    case VansLogLevel::Warning: return "[WARN]";
    case VansLogLevel::Error:   return "[ERROR]";
    default:                    return "[???]";
    }
}

// -----------------------------------------------------------------------
// VansLog implementation
// -----------------------------------------------------------------------

VansLog::~VansLog()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_File.is_open())
        m_File.close();
}

void VansLog::Init()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    EnsureInitialized();
}

void VansLog::EnsureInitialized()
{
    if (m_Initialized)
        return;

    // Create LOG/ folder next to the executable
    std::filesystem::path logDir = std::filesystem::current_path() / "LOG";
    if (!std::filesystem::exists(logDir))
        std::filesystem::create_directories(logDir);

    // Open log file: LOG/ForestEngine_YYYY-MM-DD.log
    std::string filename = "ForestEngine.log";
    std::filesystem::path logPath = logDir / filename;

    m_File.open(logPath, std::ios::out | std::ios::trunc);
    if (m_File.is_open())
    {
        m_File << "\n========================================\n";
        m_File << "  ForestEngine Log  " << GetDateStamp() << " " << GetTimestamp() << "\n";
        m_File << "========================================\n\n";
        m_File.flush();
    }

    m_Initialized = true;
}

void VansLog::Log(VansLogLevel level, const std::string& msg)
{
    Log(VansLogChannel::Engine, level, msg);
}

void VansLog::Log(VansLogChannel channel, VansLogLevel level, const std::string& msg)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    EnsureInitialized();

    std::string timestamp = GetTimestamp();
    const char* tag = LevelTag(level);

    // Format: [HH:MM:SS] [LEVEL] message
    std::ostringstream formatted;
    formatted << "[" << timestamp << "] " << tag << " " << msg;
    std::string line = formatted.str();

    // Write to file
    if (m_File.is_open())
    {
        m_File << line << "\n";
        m_File.flush();
    }

    // Also print to stdout for debug
    std::cout << line << std::endl;

    for (ILogSink* sink : m_Sinks)
    {
        if (sink)
            sink->OnLog(channel, level, msg);
    }
}

void VansLog::RegisterSink(ILogSink* sink)
{
    if (!sink) return;
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (std::find(m_Sinks.begin(), m_Sinks.end(), sink) == m_Sinks.end())
        m_Sinks.push_back(sink);
}

void VansLog::UnregisterSink(ILogSink* sink)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Sinks.erase(std::remove(m_Sinks.begin(), m_Sinks.end(), sink), m_Sinks.end());
}
