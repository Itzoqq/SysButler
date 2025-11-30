#pragma once

#include <string>
#include <vector>
#include <deque>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>

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
    JobType type; 
    std::atomic<float> progress{ 0.0f };
    std::atomic<JobStatus> status{ JobStatus::Pending };
    std::string errorMessage;
};

/**
 * @brief Manages the background worker thread and job queue for file operations.
 */
class TransferManager {
public:
    TransferManager();
    ~TransferManager();

    /**
     * @brief Adds a new file operation to the queue.
     * @param src The source file or directory path.
     * @param dest The destination folder. The filename is automatically appended and de-duplicated.
     * @param type The operation type (Copy or Move).
     */
    void QueueJob(const std::filesystem::path& src, const std::filesystem::path& dest, JobType type);

    /**
     * @brief Starts processing the queue if the worker is currently idle.
     */
    void StartQueue();

    /**
     * @brief Pauses all currently running jobs.
     */
    void PauseQueue(); 

    /**
     * @brief Resumes all paused jobs.
     */
    void ResumeQueue();

    /**
     * @brief Removes a pending or completed job from the queue.
     * @param index The index of the job in the queue.
     */
    void RemoveJob(int index);
    
    const std::deque<std::shared_ptr<FileJob>>& GetQueue() const { return m_queue; }
    bool IsRunning() const { return m_running; }
    bool IsPaused() const { return m_paused; }

private:
    /**
     * @brief Main background loop that processes jobs sequentially.
     */
    void WorkerLoop(); 
    
    std::deque<std::shared_ptr<FileJob>> m_queue;
    std::thread m_workerThread;
    
    // Thread synchronization
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_stopThread{ false };
    std::atomic<bool> m_paused{ false };
    mutable std::mutex m_queueMutex;
};