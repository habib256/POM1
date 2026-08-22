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

#include <atomic>
#include <cstddef>
#include <deque>
#include <fstream>
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

/// Appends every entry to a file, one line per entry, with a wall-clock stamp.
///
/// This is the ONLY sink that outlives the process. StreamLogger writes to
/// stdout, which goes nowhere when POM1 is launched from Finder, Explorer or a
/// desktop shortcut; RingBufferLogger dies with the process. Without a file
/// sink a user has nothing to attach to a bug report — and that is true of an
/// ordinary misbehaviour ("the SID card is silent"), not just of a crash.
///
/// One file per session: an existing log is rotated to `<path>.1` when this
/// opens, so the previous run stays inspectable after a restart.
///
/// Never throws and never fails loudly: a logging sink that can take the
/// application down is worse than no sink, so an unopenable path degrades to a
/// silent no-op and `isOpen()` reports it.
class FileLogger : public Logger
{
public:
    /// Creates the parent directory if needed and rotates any existing file.
    explicit FileLogger(const std::string& path);
    ~FileLogger() override;

    void log(LogLevel level, const char* tag, const std::string& message) override;

    bool isOpen() const;
    const std::string& path() const { return filePath; }

    void setMinLevel(LogLevel l) { minLevel = l; }

private:
    mutable std::mutex m;
    std::string filePath;
    std::unique_ptr<std::ofstream> out;   // null when the file could not be opened
    std::atomic<LogLevel> minLevel{LogLevel::Debug};
};

/// Forwards each entry to up to three child loggers (stream + ring buffer +
/// file). The owners keep the children alive; TeeLogger holds non-owning
/// pointers, and a null child is simply skipped.
class TeeLogger : public Logger
{
public:
    TeeLogger(Logger* a, Logger* b, Logger* c = nullptr) : a(a), b(b), c(c) {}
    void log(LogLevel level, const char* tag, const std::string& message) override;
private:
    Logger* a;
    Logger* b;
    Logger* c;
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

/// Process-wide file sink installed by initDefaultTeeLogger(), writing to
/// `logs/pom1.log` relative to the working directory — the same convention
/// `ini/` and `screenshots/` already use, and one that lands inside
/// `~/Library/Application Support/POM1/` on macOS because the app chdirs there
/// at startup. Not installed under WASM, where the filesystem is virtual and
/// discarded on reload. Always exists; check `isOpen()` before promising a user
/// the file is there.
FileLogger& sessionFileLog();

} // namespace pom1

#endif // POM1_LOGGER_H
