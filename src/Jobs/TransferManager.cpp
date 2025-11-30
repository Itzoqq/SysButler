#include "TransferManager.h"
#include "../Core/Logger.h"
#include <windows.h>
#include <iostream>
#include <algorithm>

// --- Helper Functions ---

/**
 * @brief Recursively calculates the total size of a directory.
 * * Iterates through the given directory and all its subdirectories, summing the size
 * of all regular files found. Exceptions (e.g., permission denied) are caught and ignored.
 * * @param dir The directory path to scan.
 * @return uint64_t Total size in bytes.
 */
uint64_t GetDirectorySize(const std::filesystem::path& dir) {
    uint64_t size = 0;
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                size += entry.file_size();
            }
        }
    } catch (...) {}
    return size;
}

/**
 * @brief Generates a unique file path to prevent overwriting existing files.
 * * If the target path already exists, this function appends a counter (e.g., "File (1).txt")
 * until a unique path is found.
 * * @param target The desired destination path.
 * @return std::filesystem::path A unique path that does not currently exist.
 */
std::filesystem::path GetUniquePath(std::filesystem::path target) {
    if (!std::filesystem::exists(target)) return target;
    std::filesystem::path folder = target.parent_path();
    std::filesystem::path stem = target.stem();
    std::filesystem::path ext = target.extension();
    int counter = 1;
    while (std::filesystem::exists(target)) {
        std::string newName = stem.string() + " (" + std::to_string(counter) + ")" + ext.string();
        target = folder / newName;
        counter++;
    }
    return target;
}

/**
 * @brief Callback function used by Windows CopyFileEx API.
 * * Currently serves as a placeholder to allow the copy operation to continue.
 * Future implementations can use this to update file-level progress for large single files.
 */
DWORD CALLBACK CopyProgressRoutine(
    LARGE_INTEGER TotalFileSize,
    LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER StreamSize,
    LARGE_INTEGER StreamBytesTransferred,
    DWORD dwStreamNumber,
    DWORD dwCallbackReason,
    HANDLE hSourceFile,
    HANDLE hDestinationFile,
    LPVOID lpData
) {
    return PROGRESS_CONTINUE; 
}

// --- TransferManager Implementation ---

/**
 * @brief Constructs the TransferManager and starts the worker thread.
 */
TransferManager::TransferManager() {
    m_workerThread = std::thread(&TransferManager::WorkerLoop, this);
}

/**
 * @brief Destructor. Signals the worker thread to stop and joins it.
 */
TransferManager::~TransferManager() {
    m_stopThread = true;
    if (m_workerThread.joinable()) m_workerThread.join();
}

/**
 * @brief Adds a new file operation job to the queue.
 * * Pre-calculates the final destination path (handling directory merging) immediately
 * so the UI displays the correct target before the job starts processing.
 * * @param src Source path.
 * @param dest Destination directory.
 * @param type Operation type (Copy or Move).
 */
void TransferManager::QueueJob(const std::filesystem::path& src, const std::filesystem::path& dest, JobType type) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    auto job = std::make_shared<FileJob>();
    job->source = src;
    
    // Logic to determine full path if destination is a directory
    std::filesystem::path finalDest = dest;
    if (std::filesystem::is_directory(finalDest)) {
        finalDest /= src.filename();
    }
    job->destination = finalDest;

    job->type = type; 
    job->status = JobStatus::Pending;
    m_queue.push_back(job);
}

/**
 * @brief Sets the running flag to true, allowing the worker loop to process jobs.
 */
void TransferManager::StartQueue() { if (!m_running) m_running = true; }

/**
 * @brief Pauses all currently active copying jobs.
 * * The worker loop checks this flag during recursive operations to halt progress.
 */
void TransferManager::PauseQueue() { 
    m_paused = true; 
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto& job : m_queue) {
        if (job->status == JobStatus::Copying) job->status = JobStatus::Paused;
    }
}

/**
 * @brief Resumes all paused jobs.
 */
void TransferManager::ResumeQueue() { 
    m_paused = false; 
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto& job : m_queue) {
        if (job->status == JobStatus::Paused) job->status = JobStatus::Copying;
    }
}

/**
 * @brief Removes a job from the queue by index.
 * * Prevents removal if the job is currently being processed (Copying state).
 * * @param index The index of the job in the deque.
 */
void TransferManager::RemoveJob(int index) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (index < 0 || index >= m_queue.size()) return;
    auto job = m_queue[index];
    if (job->status != JobStatus::Copying) {
        m_queue.erase(m_queue.begin() + index);
    }
}

/**
 * @brief The main worker loop running in a background thread.
 * * Continuously checks for pending jobs. If a job is found, it executes the transfer logic:
 * 1. Checks if the source is a file or folder.
 * 2. Resolves unique destination paths.
 * 3. Handles Move vs Copy logic, including cross-drive optimizations.
 * 4. Updates progress and status (Pending -> Copying -> Completed/Failed).
 */
void TransferManager::WorkerLoop() {
    ButlerLogger::Log(LogLevel::INFO, "Worker Thread Started.");

    while (!m_stopThread) {
        std::shared_ptr<FileJob> currentJob = nullptr;
        bool pendingJobsExist = false;

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_running && !m_queue.empty()) {
                for (auto& job : m_queue) {
                    if (job->status == JobStatus::Pending) {
                        currentJob = job;
                        pendingJobsExist = true;
                        break;
                    }
                }
            }
            if (m_running && !pendingJobsExist && !currentJob) m_running = false;
        }

        if (!currentJob) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        currentJob->status = JobStatus::Copying;
        std::string opName = (currentJob->type == JobType::Move) ? "MOVE" : "COPY";
        ButlerLogger::Log(LogLevel::INFO, "Processing " + opName + ": " + currentJob->source.string());

        // Resolve Destination and handle duplicates
        std::filesystem::path finalDest = currentJob->destination;
        if (std::filesystem::is_directory(finalDest) || std::filesystem::is_directory(currentJob->source)) {
            if (std::filesystem::exists(finalDest)) {
                finalDest /= currentJob->source.filename();
            }
        }
        finalDest = GetUniquePath(finalDest);
        currentJob->destination = finalDest; 

        bool isFolder = std::filesystem::is_directory(currentJob->source);
        bool success = false;
        
        // CASE 1: Folder Move on Same Drive (Instant Rename)
        bool sameDrive = (currentJob->source.root_name() == finalDest.root_name());

        if (isFolder && currentJob->type == JobType::Move && sameDrive) {
             success = MoveFileWithProgressW(currentJob->source.c_str(), finalDest.c_str(), NULL, NULL, MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
             if (success) currentJob->progress = 1.0f;
        }
        // CASE 2: Single File Operation
        else if (!isFolder) {
            BOOL cancel = FALSE;
            if (currentJob->type == JobType::Copy || !sameDrive) {
                success = CopyFileExW(currentJob->source.c_str(), finalDest.c_str(), CopyProgressRoutine, currentJob.get(), &cancel, 0);
            } else {
                success = MoveFileWithProgressW(currentJob->source.c_str(), finalDest.c_str(), CopyProgressRoutine, currentJob.get(), MOVEFILE_COPY_ALLOWED);
            }
            // Cleanup source if it was a cross-drive move
            if (success && currentJob->type == JobType::Move && !sameDrive) {
                std::filesystem::remove(currentJob->source);
            }
        }
        // CASE 3: Recursive Folder Copy/Move (Cross-Drive)
        else {
            try {
                currentJob->status = JobStatus::Calculating; 
                ButlerLogger::Log(LogLevel::INFO, "Calculating folder size: " + currentJob->source.string());
                
                uint64_t totalBytes = GetDirectorySize(currentJob->source);
                uint64_t bytesCopied = 0;

                currentJob->status = JobStatus::Copying;
                std::filesystem::create_directories(finalDest);

                for (const auto& entry : std::filesystem::recursive_directory_iterator(currentJob->source)) {
                    // Check for pause state
                    while (currentJob->status == JobStatus::Paused) std::this_thread::sleep_for(std::chrono::milliseconds(100));

                    std::filesystem::path relative = std::filesystem::relative(entry.path(), currentJob->source);
                    std::filesystem::path targetPath = finalDest / relative;

                    if (entry.is_directory()) {
                        std::filesystem::create_directories(targetPath);
                    } else {
                        BOOL c = FALSE;
                        if (CopyFileExW(entry.path().c_str(), targetPath.c_str(), NULL, NULL, &c, 0)) {
                            bytesCopied += entry.file_size();
                            if (totalBytes > 0) currentJob->progress = (float)bytesCopied / (float)totalBytes;
                        }
                    }
                }
                
                success = true;
                if (currentJob->type == JobType::Move) {
                    currentJob->status = JobStatus::Calculating; // Cleanup phase
                    std::filesystem::remove_all(currentJob->source);
                }

            } catch (const std::exception& e) {
                currentJob->errorMessage = e.what();
                success = false;
            }
        }

        if (success) {
            currentJob->status = JobStatus::Completed;
            currentJob->progress = 1.0f;
            ButlerLogger::Log(LogLevel::INFO, opName + " SUCCESS: " + finalDest.string());
        } else {
            currentJob->status = JobStatus::Failed;
            if (currentJob->errorMessage.empty()) {
                currentJob->errorMessage = "Win32 Error Code: " + std::to_string(GetLastError());
            }
            ButlerLogger::Log(LogLevel::ERR, opName + " FAILED: " + currentJob->errorMessage);
        }
    }
}