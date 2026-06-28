#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <ctime>
#include <sstream>
#include <iomanip>
#include "../../Util/VansLog.h"

// -----------------------------------------------------------------------
// VansConsole  –  A thread-safe, singleton ring-buffer log shared by
//                 the engine (C++) side and the embedded Python side.
//                 The Console *Window* reads from here.
// -----------------------------------------------------------------------

enum class VansConsoleLogType
{
    Engine,
    Python
};

enum class VansConsoleSeverity
{
    Info,
    Warning,
    Error
};

struct VansConsoleEntry
{
    VansConsoleLogType  type;
    VansConsoleSeverity severity = VansConsoleSeverity::Info;
    std::string         message;
    std::string         timestamp;  // HH:MM:SS
};

class VansConsole : public ILogSink
{
public:
    static VansConsole& Get()
    {
        static VansConsole instance;
        return instance;
    }

    // ---------- writing --------------------------------------------------

    void LogEngine(const std::string& msg)
    {
        Push(VansConsoleLogType::Engine, VansConsoleSeverity::Info, msg);
    }

    /// Overload used by VansLog to forward severity level.
    void LogEngine(VansLogLevel level, const std::string& msg)
    {
        VansConsoleSeverity sev = VansConsoleSeverity::Info;
        // VansLogLevel values mirror VansConsoleSeverity (0=Info,1=Warn,2=Error)
        sev = static_cast<VansConsoleSeverity>(static_cast<int>(level));
        Push(VansConsoleLogType::Engine, sev, msg);
    }

    void LogPython(const std::string& msg)
    {
        Push(VansConsoleLogType::Python, VansConsoleSeverity::Info, msg);
    }

    void OnLog(VansLogChannel channel, VansLogLevel level, const std::string& msg) override
    {
        VansConsoleSeverity sev = static_cast<VansConsoleSeverity>(static_cast<int>(level));
        const VansConsoleLogType type = channel == VansLogChannel::Python
            ? VansConsoleLogType::Python
            : VansConsoleLogType::Engine;
        Push(type, sev, msg);
    }

    // ---------- reading (called from UI thread) --------------------------

    const std::vector<VansConsoleEntry>& GetEntries() const { return m_Entries; }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Entries.clear();
    }

    bool ScrollToBottom = true;

private:
    VansConsole() = default;

    void Push(VansConsoleLogType type, VansConsoleSeverity severity, const std::string& msg)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        VansConsoleEntry entry;
        entry.type = type;
        entry.severity = severity;
        entry.message = msg;

        // Timestamp
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
        entry.timestamp = oss.str();

        m_Entries.push_back(std::move(entry));

        // Keep a reasonable cap
        if (m_Entries.size() > MaxEntries)
            m_Entries.erase(m_Entries.begin(), m_Entries.begin() + (m_Entries.size() - MaxEntries));

        ScrollToBottom = true;
    }

    mutable std::mutex          m_Mutex;
    std::vector<VansConsoleEntry> m_Entries;
    static constexpr size_t     MaxEntries = 2048;
};
