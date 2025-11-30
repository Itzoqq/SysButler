#pragma once

#include <string>
#include <vector>
#include <deque>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>

// NEW: Define the type of operation
enum class JobType {
    Copy,
    Move
};

enum class JobStatus {
    Pending,
    Calculating,
    Copying,
    Paused,
    Completed,
    Failed
};

struct FileJob {
    std::filesystem::path source;
    std::filesystem::path destination;
    JobType type; // NEW: Store the operation type here
    std::atomic<float> progress{ 0.0f };
    std::atomic<JobStatus> status{ JobStatus::Pending };
    std::string errorMessage;
};

class TransferManager {
public:
    TransferManager();
    ~TransferManager();

    // UPDATE: Now accepts the JobType (default to Copy if omitted, or you can force it)
    void QueueJob(const std::filesystem::path& src, const std::filesystem::path& dest, JobType type);

    void StartQueue();
    void PauseQueue(); 
    void ResumeQueue();
    void RemoveJob(int index);
    
    const std::deque<std::shared_ptr<FileJob>>& GetQueue() const { return m_queue; }
    bool IsRunning() const { return m_running; }
    bool IsPaused() const { return m_paused; }

private:
    void WorkerLoop(); 
    
    std::deque<std::shared_ptr<FileJob>> m_queue;
    std::thread m_workerThread;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_stopThread{ false };
    std::atomic<bool> m_paused{ false };
    mutable std::mutex m_queueMutex;
};