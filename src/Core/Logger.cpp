#include "Logger.h"
#include <iostream>
#include <ctime>
#include <iomanip>

std::ofstream ButlerLogger::m_logFile;
std::mutex ButlerLogger::m_logMutex;

void ButlerLogger::Init() {
    std::lock_guard<std::mutex> lock(m_logMutex);
    
    // Create logs directory if it doesn't exist
    std::filesystem::path logDir("logs");
    if (!std::filesystem::exists(logDir)) {
        std::filesystem::create_directory(logDir);
    }

    // Open the file in Append mode
    m_logFile.open("logs/sysbutler_core.log", std::ios::app);
    
    if (m_logFile.is_open()) {
        m_logFile << "\n=== SysButler Session Started: " << GetTimeStamp() << " ===\n";
    }
}

void ButlerLogger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_logMutex);
    
    std::string timestamp = GetTimeStamp();
    std::string levelStr = LevelToString(level);
    std::string finalMsg = "[" + timestamp + "] [" + levelStr + "] " + message;

    // Write to file
    if (m_logFile.is_open()) {
        m_logFile << finalMsg << std::endl;
        m_logFile.flush(); // Ensure it writes immediately
    }

    // Also write to standard console for debug visibility
    std::cout << finalMsg << std::endl;
}

std::string ButlerLogger::GetTimeStamp() {
    std::time_t now = std::time(nullptr);
    std::tm localTime;
    localtime_s(&localTime, &now); // Thread-safe version of localtime
    
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string ButlerLogger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERR:   return "ERROR";
        default: return "UNKNOWN";
    }
}