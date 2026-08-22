// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "Logger.h"

#include "POM1Build.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

namespace pom1 {

const char* levelName(LogLevel l)
{
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// StreamLogger
// ---------------------------------------------------------------------------
void StreamLogger::log(LogLevel level, const char* tag, const std::string& message)
{
    if (level < minLevel) return;

    std::lock_guard<std::mutex> lock(m);
    std::ostream& os = (level >= LogLevel::Warn) ? std::cerr : std::cout;
    if (tag && tag[0]) {
        os << '[' << tag << "] ";
    }
    if (level >= LogLevel::Warn) {
        os << levelName(level) << ": ";
    }
    os << message << std::endl;
}

// ---------------------------------------------------------------------------
// RingBufferLogger
// ---------------------------------------------------------------------------
void RingBufferLogger::log(LogLevel level, const char* tag, const std::string& message)
{
    std::lock_guard<std::mutex> lock(m);
    if (entries.size() >= cap) entries.pop_front();
    entries.push_back(Entry{ level, tag ? tag : "", message });
}

std::vector<RingBufferLogger::Entry> RingBufferLogger::snapshot(LogLevel minLevel) const
{
    std::lock_guard<std::mutex> lock(m);
    std::vector<Entry> out;
    out.reserve(entries.size());
    for (const auto& e : entries) {
        if (e.level >= minLevel) out.push_back(e);
    }
    return out;
}

void RingBufferLogger::clear()
{
    std::lock_guard<std::mutex> lock(m);
    entries.clear();
}

// ---------------------------------------------------------------------------
// TeeLogger
// ---------------------------------------------------------------------------
void TeeLogger::log(LogLevel level, const char* tag, const std::string& message)
{
    if (a) a->log(level, tag, message);
    if (b) b->log(level, tag, message);
    if (c) c->log(level, tag, message);
}

// ---------------------------------------------------------------------------
// Global accessor + default Tee setup. Singletons are function-local statics
// so initialisation order is well-defined even if a peripheral logs from a
// constructor before main() has run.
// ---------------------------------------------------------------------------
namespace {
StreamLogger& streamSingleton()
{
    static StreamLogger inst;
    return inst;
}
RingBufferLogger& uiSingleton()
{
    static RingBufferLogger inst(512);
    return inst;
}
FileLogger& fileSingleton()
{
    static FileLogger inst("logs/pom1.log");
    return inst;
}
TeeLogger& teeSingleton()
{
    // WASM's filesystem is virtual and thrown away on reload, so a file sink
    // there would cost MEMFS for nothing.
    static TeeLogger inst(&streamSingleton(), &uiSingleton(),
                          POM1_IS_WASM ? nullptr : &fileSingleton());
    return inst;
}
Logger* g_active = nullptr;
} // namespace

Logger& log()
{
    return g_active ? *g_active : static_cast<Logger&>(streamSingleton());
}

void setLogger(Logger* logger)
{
    g_active = logger;
}

void initDefaultTeeLogger()
{
    setLogger(&teeSingleton());
}

RingBufferLogger& uiRingBuffer()
{
    return uiSingleton();
}

FileLogger& sessionFileLog()
{
    return fileSingleton();
}

// ---------------------------------------------------------------------------
// FileLogger
// ---------------------------------------------------------------------------
namespace {

/// "2026-08-22 14:07:31.482" — local time, millisecond resolution. The
/// thread-safe reentrant variants, because POM1 logs from the CPU thread, the
/// UI thread and the modem's connect thread.
std::string timestampNow()
{
    using namespace std::chrono;
    const auto now  = system_clock::now();
    const auto secs = time_point_cast<seconds>(now);
    const auto ms   = duration_cast<milliseconds>(now - secs).count();
    const std::time_t t = system_clock::to_time_t(secs);

    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);

    std::ostringstream os;
    os << buf << '.' << std::setfill('0') << std::setw(3) << ms;
    return os.str();
}

} // namespace

FileLogger::FileLogger(const std::string& path) : filePath(path)
{
    // Everything here is best-effort. A logger that throws out of a static
    // initialiser would take the process down before main() runs, which is a
    // spectacularly bad trade for a diagnostic convenience.
    std::error_code ec;
    const std::filesystem::path p(filePath);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path(), ec);

    // Keep the previous session: pom1.log -> pom1.log.1, overwriting the older
    // rotation. Two files is the right depth here — the question a user is
    // answering is almost always "this run, or the one before it?".
    if (std::filesystem::exists(p, ec)) {
        std::filesystem::path prev = p;
        prev += ".1";
        std::filesystem::remove(prev, ec);
        std::filesystem::rename(p, prev, ec);
    }

    auto f = std::make_unique<std::ofstream>(filePath, std::ios::out | std::ios::trunc);
    if (f && f->is_open()) {
        *f << "=== POM1 session started " << timestampNow() << " ===\n";
        f->flush();
        out = std::move(f);
    }
}

FileLogger::~FileLogger()
{
    std::lock_guard<std::mutex> lock(m);
    if (out && out->is_open()) {
        *out << "=== POM1 session ended " << timestampNow() << " ===\n";
        out->flush();
    }
}

bool FileLogger::isOpen() const
{
    std::lock_guard<std::mutex> lock(m);
    return out && out->is_open();
}

void FileLogger::log(LogLevel level, const char* tag, const std::string& message)
{
    if (level < minLevel.load(std::memory_order_relaxed)) return;

    std::lock_guard<std::mutex> lock(m);
    if (!out || !out->is_open()) return;

    *out << timestampNow() << "  " << std::setw(5) << std::left << levelName(level)
         << "  ";
    if (tag && tag[0]) *out << '[' << tag << "] ";
    *out << message << '\n';

    // Flushed per entry on purpose: the entries that matter most are the last
    // ones before a hang or a hard kill, and those are exactly the ones a
    // buffered stream loses.
    out->flush();
}

} // namespace pom1
