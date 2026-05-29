// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud

#include "Logger.h"

#include <iostream>

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
TeeLogger& teeSingleton()
{
    static TeeLogger inst(&streamSingleton(), &uiSingleton());
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

} // namespace pom1
