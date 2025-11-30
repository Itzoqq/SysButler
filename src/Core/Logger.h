#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <filesystem>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERR  // "ERROR" is often a macro in Windows headers, so we use ERR
};

class ButlerLogger {
public:
    // Initialize the logger (opens file, creates directory)
    static void Init();
    
    // Main log function
    static void Log(LogLevel level, const std::string& message);
    
    // Overload to log simple strings easily
    static void Log(const std::string& message) { Log(LogLevel::INFO, message); }

private:
    static std::ofstream m_logFile;
    static std::mutex m_logMutex;
    static std::string GetTimeStamp();
    static std::string LevelToString(LogLevel level);
};