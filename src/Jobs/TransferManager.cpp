#include "TransferManager.h"
#include "../Core/Logger.h"
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <algorithm>

// --- HELPER FUNCTIONS ---

// Calculates total size of a directory for progress bars
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
        // If it's a directory, stem is the folder name, ext is empty
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
    // Standard File Copy callback
    // We don't use this for Folder progress because we calculate that manually
    return PROGRESS_CONTINUE; 
}

// --- CLASS METHODS ---

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
    
    // FIX: Pre-calculate the full destination path immediately.
    // This ensures the UI shows the full path (e.g., ".../Movies/File.mkv") 
    // instead of just the folder (".../Movies") while waiting.
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
    for (auto& job : m_queue) if (job->status == JobStatus::Copying) job->status = JobStatus::Paused;
}
void TransferManager::ResumeQueue() { 
    m_paused = false; 
    std::lock_guard<std::mutex> lock(m_queueMutex);
    for (auto& job : m_queue) if (job->status == JobStatus::Paused) job->status = JobStatus::Copying;
}

void TransferManager::RemoveJob(int index) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (index < 0 || index >= m_queue.size()) return;
    auto job = m_queue[index];
    if (job->status != JobStatus::Copying) m_queue.erase(m_queue.begin() + index);
}

// --- WORKER LOOP ---

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

        // 1. Resolve Destination
        std::filesystem::path finalDest = currentJob->destination;

        // If Dest is a directory (or we force it to be one because source is folder), append source name
        if (std::filesystem::is_directory(finalDest) || std::filesystem::is_directory(currentJob->source)) {
            // E.g. Move "C:/Games/WoW" to "D:/Backups" -> "D:/Backups/WoW"
            // Only append if the destination folder actually exists or is intended to be the parent
            if (std::filesystem::exists(finalDest)) {
                finalDest /= currentJob->source.filename();
            }
        }

        finalDest = GetUniquePath(finalDest);
        currentJob->destination = finalDest; // Update UI with the (1) version if needed

        // 2. CHECK: IS THIS A FOLDER?
        bool isFolder = std::filesystem::is_directory(currentJob->source);
        
        // 3. LOGIC BRANCH
        bool success = false;
        
        // --- CASE A: FOLDER ON SAME DRIVE (MOVE) ---
        // Windows can rename/move folders instantly on the same drive
        bool sameDrive = (currentJob->source.root_name() == finalDest.root_name());

        if (isFolder && currentJob->type == JobType::Move && sameDrive) {
             success = MoveFileWithProgressW(currentJob->source.c_str(), finalDest.c_str(), NULL, NULL, MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
             if (success) currentJob->progress = 1.0f;
        }
        // --- CASE B: FILE ---
        else if (!isFolder) {
            BOOL cancel = FALSE;
            if (currentJob->type == JobType::Copy || !sameDrive) {
                success = CopyFileExW(currentJob->source.c_str(), finalDest.c_str(), CopyProgressRoutine, currentJob.get(), &cancel, 0);
            } else {
                success = MoveFileWithProgressW(currentJob->source.c_str(), finalDest.c_str(), CopyProgressRoutine, currentJob.get(), MOVEFILE_COPY_ALLOWED);
            }
            // If it was a cross-drive move, we must delete source after copy
            if (success && currentJob->type == JobType::Move && !sameDrive) {
                std::filesystem::remove(currentJob->source);
            }
        }
        // --- CASE C: FOLDER CROSS-DRIVE (OR COPY) ---
        else {
            // Recursive Copy Logic
            try {
                // UPDATE: Set Status to Calculating so user knows we are scanning
                currentJob->status = JobStatus::Calculating; 
                ButlerLogger::Log(LogLevel::INFO, "Calculating folder size: " + currentJob->source.string());
                
                // Step 1: Calculate Total Size
                uint64_t totalBytes = GetDirectorySize(currentJob->source);
                uint64_t bytesCopied = 0;

                // UPDATE: Now we are actually copying
                currentJob->status = JobStatus::Copying;
                ButlerLogger::Log(LogLevel::INFO, "Starting recursive transfer...");

                // Step 2: Create Root Dest
                std::filesystem::create_directories(finalDest);

                // Step 3: Iterate and Copy
                for (const auto& entry : std::filesystem::recursive_directory_iterator(currentJob->source)) {
                    // Check Pause
                    while (currentJob->status == JobStatus::Paused) std::this_thread::sleep_for(std::chrono::milliseconds(100));

                    // Build relative path
                    std::filesystem::path relative = std::filesystem::relative(entry.path(), currentJob->source);
                    std::filesystem::path targetPath = finalDest / relative;

                    if (entry.is_directory()) {
                        std::filesystem::create_directories(targetPath);
                    } else {
                        BOOL c = FALSE;
                        if (CopyFileExW(entry.path().c_str(), targetPath.c_str(), NULL, NULL, &c, 0)) {
                            bytesCopied += entry.file_size();
                            // Update Progress
                            if (totalBytes > 0) currentJob->progress = (float)bytesCopied / (float)totalBytes;
                        }
                    }
                }
                
                success = true;

                // Step 4: If Move, delete source folder
                if (currentJob->type == JobType::Move) {
                    currentJob->status = JobStatus::Calculating; // Reuse this status for "Cleaning up"
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
            if (currentJob->errorMessage.empty()) currentJob->errorMessage = "Win32 Error: " + std::to_string(GetLastError());
            ButlerLogger::Log(LogLevel::ERR, opName + " FAILED: " + currentJob->errorMessage);
        }
    }
}