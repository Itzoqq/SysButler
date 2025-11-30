#include "TransferManager.h"
#include "../Core/Logger.h"
#include <windows.h>
#include <iostream>
#include <algorithm>

// --- Helper Functions ---

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
    // Return PROGRESS_CONTINUE to keep copying
    return PROGRESS_CONTINUE; 
}

// --- TransferManager Implementation ---

TransferManager::TransferManager() {
    m_workerThread = std::thread(&TransferManager::WorkerLoop, this);
}

TransferManager::~TransferManager() {
    m_stopThread = true;
    if (m_workerThread.joinable()) m_workerThread.join();
}

void TransferManager::QueueJob(const std::filesystem::path& src, const std::filesystem::path& dest, JobType type) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    auto job = std::make_shared<FileJob>();
    job->source = src;
    
    // Pre-calculate destination to display correct target in UI immediately
    std::filesystem::path finalDest = dest;
    if (std::filesystem::is_directory(finalDest)) {
        finalDest /= src.filename();
    }
    job->destination = finalDest;

    job->type = type; 
    job->status = JobStatus::Pending;
    m_queue.push_back(job);
}

void TransferManager::StartQueue() { if (!m_running) m_running = true; }

void TransferManager::PauseQueue() { 
    m_paused = true; 
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto& job : m_queue) {
        if (job->status == JobStatus::Copying) job->status = JobStatus::Paused;
    }
}

void TransferManager::ResumeQueue() { 
    m_paused = false; 
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto& job : m_queue) {
        if (job->status == JobStatus::Paused) job->status = JobStatus::Copying;
    }
}

void TransferManager::RemoveJob(int index) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (index < 0 || index >= m_queue.size()) return;
    auto job = m_queue[index];
    if (job->status != JobStatus::Copying) {
        m_queue.erase(m_queue.begin() + index);
    }
}

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
        
        // 1. Same Drive Move (Instant)
        bool sameDrive = (currentJob->source.root_name() == finalDest.root_name());

        if (isFolder && currentJob->type == JobType::Move && sameDrive) {
             success = MoveFileWithProgressW(currentJob->source.c_str(), finalDest.c_str(), NULL, NULL, MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
             if (success) currentJob->progress = 1.0f;
        }
        // 2. File Operation
        else if (!isFolder) {
            BOOL cancel = FALSE;
            if (currentJob->type == JobType::Copy || !sameDrive) {
                success = CopyFileExW(currentJob->source.c_str(), finalDest.c_str(), CopyProgressRoutine, currentJob.get(), &cancel, 0);
            } else {
                success = MoveFileWithProgressW(currentJob->source.c_str(), finalDest.c_str(), CopyProgressRoutine, currentJob.get(), MOVEFILE_COPY_ALLOWED);
            }
            if (success && currentJob->type == JobType::Move && !sameDrive) {
                std::filesystem::remove(currentJob->source);
            }
        }
        // 3. Recursive Folder Copy/Move
        else {
            try {
                currentJob->status = JobStatus::Calculating; 
                ButlerLogger::Log(LogLevel::INFO, "Calculating folder size: " + currentJob->source.string());
                
                uint64_t totalBytes = GetDirectorySize(currentJob->source);
                uint64_t bytesCopied = 0;

                currentJob->status = JobStatus::Copying;
                std::filesystem::create_directories(finalDest);

                for (const auto& entry : std::filesystem::recursive_directory_iterator(currentJob->source)) {
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
                    currentJob->status = JobStatus::Calculating;
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