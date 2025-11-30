#include "Logger.h"
#include <iostream>
#include <ctime>
#include <iomanip>

std::ofstream ButlerLogger::m_logFile;
std::mutex ButlerLogger::m_logMutex;

/**
 * @brief Initializes the logging subsystem.
 * * This function checks for the existence of the "logs" directory and creates it if missing.
 * It then opens the "sysbutler_core.log" file in append mode and writes a session start header.
 * Thread-safety is ensured via a mutex.
 */
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

/**
 * @brief Writes a formatted log message to the log file and standard console.
 * * The message is prefixed with a current timestamp and the severity level.
 * The output is flushed immediately to ensure logs are captured even in the event of a crash.
 * * @param level The severity level of the log (DEBUG, INFO, WARN, ERR).
 * @param message The content string to log.
 */
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

/**
 * @brief Generates a string representation of the current system time.
 * * @return std::string formatted as "YYYY-MM-DD HH:MM:SS".
 */
std::string ButlerLogger::GetTimeStamp() {
    std::time_t now = std::time(nullptr);
    std::tm localTime;
    localtime_s(&localTime, &now); // Thread-safe version of localtime
    
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

/**
 * @brief Converts the LogLevel enum to a readable string representation.
 * * @param level The log level to convert.
 * @return std::string The string representation (e.g., "INFO ", "ERROR").
 */
std::string ButlerLogger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERR:   return "ERROR";
        default: return "UNKNOWN";
    }
}