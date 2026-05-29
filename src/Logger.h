// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// Logger — minimal levelled logging shared by every POM1 subsystem. Replaces
// the ad-hoc std::cout / std::cerr scattered across peripherals (WiFi,
// Terminal, microSD, CFFA1) with a single sink that:
//   - filters by level (Debug / Info / Warn / Error)
//   - tags each entry with the originating subsystem ("WiFi", "Term", ...)
//   - can be redirected (UI installs a TeeLogger to capture for the in-app
//     debug console while still echoing to stdout/stderr).
//
// Usage from any TU:
//     #include "Logger.h"
//     pom1::log().info("WiFi", "connected to host");
//     pom1::log().error("SD", "Cannot open file: " + name);
//
// All implementations are thread-safe (one mutex per logger). The default
// global is a StreamLogger writing to std::cout (Debug/Info) and std::cerr
// (Warn/Error). main_imgui replaces it with a TeeLogger combining a stream
// sink with a RingBufferLogger that the Debug Console reads.

#ifndef POM1_LOGGER_H
#define POM1_LOGGER_H

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace pom1 {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

const char* levelName(LogLevel l);  // "DEBUG" / "INFO" / "WARN" / "ERROR"

class Logger
{
public:
    virtual ~Logger() = default;

    /// Implementations should be thread-safe.
    virtual void log(LogLevel level, const char* tag, const std::string& message) = 0;

    // Convenience helpers (non-virtual).
    void debug(const char* tag, const std::string& m) { log(LogLevel::Debug, tag, m); }
    void info (const char* tag, const std::string& m) { log(LogLevel::Info,  tag, m); }
    void warn (const char* tag, const std::string& m) { log(LogLevel::Warn,  tag, m); }
    void error(const char* tag, const std::string& m) { log(LogLevel::Error, tag, m); }
};

/// Default logger: writes to std::cout (Debug/Info) and std::cerr (Warn/Error)
/// with a "[TAG] message" prefix. Filters by minimum level (default: Info).
class StreamLogger : public Logger
{
public:
    void log(LogLevel level, const char* tag, const std::string& message) override;
    void setMinLevel(LogLevel l) { minLevel = l; }
    LogLevel getMinLevel() const { return minLevel; }
private:
    std::mutex m;
    LogLevel minLevel = LogLevel::Info;
};

/// Ring buffer of the last N entries, exposed for the debug-UI log viewer.
/// Drops oldest entries when full (no allocation in steady state).
class RingBufferLogger : public Logger
{
public:
    struct Entry {
        LogLevel level;
        std::string tag;
        std::string message;
    };

    explicit RingBufferLogger(std::size_t capacity = 256) : cap(capacity) {}

    void log(LogLevel level, const char* tag, const std::string& message) override;

    /// Snapshot for the UI thread. Filters to entries with level >= minLevel.
    std::vector<Entry> snapshot(LogLevel minLevel = LogLevel::Debug) const;
    void clear();
    std::size_t capacity() const { return cap; }

private:
    mutable std::mutex m;
    std::deque<Entry> entries;
    std::size_t cap;
};

/// Forwards each entry to two child loggers (e.g. stream + ring buffer).
/// The owners keep the children alive; TeeLogger holds non-owning pointers.
class TeeLogger : public Logger
{
public:
    TeeLogger(Logger* a, Logger* b) : a(a), b(b) {}
    void log(LogLevel level, const char* tag, const std::string& message) override;
private:
    Logger* a;
    Logger* b;
};

// ---------------------------------------------------------------------------
// Process-wide accessor. Defaults to a StreamLogger living in Logger.cpp;
// main_imgui calls initDefaultTeeLogger() at startup to upgrade it to a
// TeeLogger combining stream + uiRingBuffer() so debug-console viewers can
// snapshot the captured entries. setLogger() does NOT take ownership.
// ---------------------------------------------------------------------------
Logger& log();
void setLogger(Logger* logger);

/// Install the default Tee(stream + uiRingBuffer) as the active logger.
/// Idempotent. Call once at process start.
void initDefaultTeeLogger();

/// Process-wide ring buffer captured by initDefaultTeeLogger(). The debug UI
/// snapshots it for display. Always exists (function-local static).
RingBufferLogger& uiRingBuffer();

} // namespace pom1

#endif // POM1_LOGGER_H
