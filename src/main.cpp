#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string>
#include <filesystem> 
#include <objbase.h> // Required for CoInitializeEx
#include <vector>
#include <filesystem>
#include <windows.h> // For GetLogicalDrives
#include <algorithm> // For search case-insensitivity
#include <cctype>

// YOUR HEADERS
#include "Jobs/TransferManager.h"
#include "Core/Logger.h"
#include "Core/PlatformUtils.h"

namespace fs = std::filesystem;

// --- FILE BROWSER UTILS ---

struct FileEntry {
    fs::path path;
    bool isDirectory;
    std::string displayString;
};

struct BrowserState {
    fs::path currentPath;
    fs::path selectedPath; 
    std::vector<FileEntry> entries;
    int selectedIndex = -1;
    char currentDrive = 'C';
    
    // NEW: Search Buffer
    char searchFilter[256] = ""; 

    BrowserState() {
        currentPath = fs::current_path().root_path(); 
        Refresh();
    }

    void Refresh() {
        entries.clear();
        // Note: We do NOT reset selectedIndex here if we are trying to preserve selection,
        // but typically a refresh invalidates indices. We will handle "Jump to File" separately.
        selectedIndex = -1; 
        
        try {
            for (const auto& entry : fs::directory_iterator(currentPath)) {
                FileEntry e;
                e.path = entry.path();
                e.isDirectory = entry.is_directory();
                e.displayString = (e.isDirectory ? "[DIR] " : "      ") + e.path.filename().string();
                entries.push_back(e);
            }
            std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) {
                if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
                return a.path < b.path;
            });
        } catch (...) {}
    }

    void NavigateToFile(const std::filesystem::path& targetFile) {
        if (!fs::exists(targetFile)) return;

        // 1. Change Directory to the file's parent
        currentPath = targetFile.parent_path();
        
        // 2. Update Drive Letter
        std::string pathStr = currentPath.string();
        if (pathStr.length() >= 2 && pathStr[1] == ':') {
            currentDrive = toupper(pathStr[0]);
        }

        // 3. Refresh the list to load new files
        Refresh();

        // 4. Find and Select the specific file
        selectedPath = targetFile;
        // Clear search so the file is actually visible
        memset(searchFilter, 0, sizeof(searchFilter)); 

        for (int i = 0; i < entries.size(); i++) {
            if (entries[i].path == targetFile) {
                selectedIndex = i;
                break;
            }
        }
    }

    void NavigateUp() { /* ... existing code ... */ 
        if (currentPath.has_parent_path() && currentPath != currentPath.root_path()) {
            currentPath = currentPath.parent_path();
            Refresh();
            selectedPath.clear();
        }
    }
    
    void ChangeDrive(char driveLetter) { /* ... existing code ... */ 
        std::string d = std::string(1, driveLetter) + ":\\";
        currentPath = d;
        currentDrive = driveLetter;
        Refresh();
        selectedPath.clear();
    }
};

bool StringContains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
    );
    return (it != haystack.end());
}

void RenderFileBrowser(const char* id, BrowserState& state, float height) {
    ImGui::PushID(id);
    ImGui::BeginGroup();

    // --- TOOLBAR ROW ---
    
    // 1. Up Arrow (Replaces ".." button)
    // ImGuiDir_Up renders a small triangle pointing up
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { 
        state.NavigateUp(); 
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to Parent Folder");

    ImGui::SameLine();

    // 2. Drive Selector
    char driveLabel[4] = "C:\\";
    driveLabel[0] = state.currentDrive;
    ImGui::SetNextItemWidth(50);
    if (ImGui::BeginCombo("##drive", driveLabel)) {
        DWORD mask = GetLogicalDrives();
        for (char c = 'A'; c <= 'Z'; c++) {
            if (mask & 1) {
                char d[4] = "X:\\"; d[0] = c;
                if (ImGui::Selectable(d, state.currentDrive == c)) {
                    state.ChangeDrive(c);
                }
            }
            mask >>= 1;
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();

    // 3. Search Box (Takes available width minus the space needed for the "..." button)
    float availableWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(availableWidth - 40.0f); // Leave 40px for the "..." button
    ImGui::InputTextWithHint("##search", "Search files...", state.searchFilter, IM_ARRAYSIZE(state.searchFilter));

    ImGui::SameLine();

    // 4. "..." Button (Native Explorer Integration)
    if (ImGui::Button("...")) {
        // Use your existing PlatformUtils to pick a file
        std::string picked = PlatformUtils::OpenFilePicker();
        if (!picked.empty()) {
            state.NavigateToFile(std::filesystem::path(picked));
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Locate file using Windows Explorer");

    // --- PATH DISPLAY ---
    // Small text showing full current path
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", state.currentPath.string().c_str());

    // --- FILE LIST ---
    ImGui::BeginChild("Files", ImVec2(0, height), true);
    
    for (int i = 0; i < state.entries.size(); i++) {
        const auto& entry = state.entries[i];
        
        // FILTER LOGIC: Skip item if it doesn't match search
        if (!StringContains(entry.path.filename().string(), state.searchFilter)) {
            continue;
        }

        bool isSelected = (state.selectedIndex == i);
        
        // Render Item
        if (ImGui::Selectable(entry.displayString.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
            state.selectedIndex = i;
            state.selectedPath = entry.path;
            
            if (ImGui::IsMouseDoubleClicked(0) && entry.isDirectory) {
                state.currentPath = entry.path;
                // Clear search when entering a new folder so we see contents
                memset(state.searchFilter, 0, sizeof(state.searchFilter));
                state.Refresh();
                state.selectedPath = state.currentPath;
            }
        }

        // Auto-scroll to selection if we just jumped here (optional polish)
        if (isSelected && ImGui::IsWindowAppearing()) {
            ImGui::SetScrollHereY();
        }
    }
    ImGui::EndChild();

    ImGui::EndGroup();
    ImGui::PopID();
}

int CountCompletedJobs(const std::deque<std::shared_ptr<FileJob>>& queue) {
    int count = 0;
    for (const auto& job : queue) {
        if (job->status == JobStatus::Completed) count++;
    }
    return count;
}

// Helper to keep the UI state clean
struct UIState {
    char sourceBuffer[512] = ""; 
    char destBuffer[512] = "";
};

int main(int, char**)
{
    // 1. Initialize COM (Required for the Folder Picker to work)
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // 2. Init Logger
    ButlerLogger::Init();
    ButlerLogger::Log("Application Starting...");

    // 3. Setup Window (Boilerplate)
    if (!glfwInit()) {
        ButlerLogger::Log(LogLevel::ERR, "Failed to initialize GLFW");
        return 1;
    }
    GLFWwindow* window = glfwCreateWindow(1280, 720, "System Butler", nullptr, nullptr);
    if (window == nullptr) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // 4. Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    // Style: Dark Mode 
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.0f, 0.7f, 0.0f, 1.0f); // Green Progress bars
    style.CellPadding = ImVec2(5.0f, 5.0f); // Make table rows a bit taller

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // --- INSTANTIATE OUR MANAGER ---
    TransferManager transferManager;
    UIState uiState;

    // 5. Main Loop
   // --- INSTANTIATE MANAGER & STATES ---
    BrowserState leftBrowser;  
    BrowserState rightBrowser; 
    
    // UI State Tracking
    int selectedQueueIndex = -1; 
    int previousCompletedCount = 0; // To detect when to refresh

    // 5. Main Loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // 1. Check for Auto-Refresh (If a job finished, refresh file lists)
        // We do this at the start of the frame to keep UI up to date
        int currentCompletedCount = CountCompletedJobs(transferManager.GetQueue());
        if (currentCompletedCount > previousCompletedCount) {
            leftBrowser.Refresh();
            rightBrowser.Refresh();
            ButlerLogger::Log("Job finished. Refreshing file browsers.");
        }
        previousCompletedCount = currentCompletedCount;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Setup Window
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

        // Header
        ImGui::TextDisabled("SYSBUTLER // FILE COMMANDER");
        ImGui::Separator();
        
        // --- TOP SECTION: DUAL PANE EXPLORER ---
        float paneHeight = 350.0f; // Adjusted height
        
        ImGui::Columns(2, "ExplorerCols", true);
        
        // LEFT PANE
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "SOURCE");
        RenderFileBrowser("LeftPane", leftBrowser, paneHeight);
        ImGui::NextColumn();
        
        // RIGHT PANE
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "DESTINATION");
        RenderFileBrowser("RightPane", rightBrowser, paneHeight);
        ImGui::Columns(1); 

        // --- MIDDLE BAR: ACTIONS ---
        ImGui::Spacing();
        ImGui::Separator();
        
        float width = ImGui::GetWindowWidth();
        ImGui::SetCursorPosX((width - 300) * 0.5f);
        
        bool canCopy = !leftBrowser.selectedPath.empty();

        if (ImGui::Button("COPY >>>", ImVec2(140, 40)) && canCopy) {
            transferManager.QueueJob(leftBrowser.selectedPath, rightBrowser.currentPath, JobType::Copy);
        }
        ImGui::SameLine();
        if (ImGui::Button("MOVE >>>", ImVec2(140, 40)) && canCopy) {
             transferManager.QueueJob(leftBrowser.selectedPath, rightBrowser.currentPath, JobType::Move);
        }
        ImGui::Separator();

        // --- BOTTOM SECTION: QUEUE & CONTROLS ---
        // We use Child windows to create distinct "Boxes" for the table and controls
        
        float bottomHeight = 250.0f;
        float controlsWidth = 180.0f;
        float tableWidth = ImGui::GetContentRegionAvail().x - controlsWidth - 10.0f; // 10px spacing

        // 1. LEFT BOX: THE QUEUE TABLE
        ImGui::BeginChild("QueueRegion", ImVec2(tableWidth, bottomHeight), true); // true = show border
        
        ImGui::Text("Active Transfer Queue");
        ImGui::Separator();

        auto queue = transferManager.GetQueue();

        if (queue.empty()) {
             // Clean "Empty State" message
             float winHeight = ImGui::GetWindowHeight();
             ImGui::SetCursorPosY(winHeight * 0.4f);
             ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.35f);
             ImGui::TextDisabled("No active jobs pending.");
        } 
        else {
            // TABLE STYLE: No Borders, just Row Backgrounds for a clean look
            ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
            
            if (ImGui::BeginTable("QueueTable", 6, tableFlags)) {
                
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("To",   ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < queue.size(); i++) {
                    auto job = queue[i];
                    ImGui::PushID(i); 
                    
                    ImGui::TableNextRow();
                    
                    // Col 0: Type
                    ImGui::TableSetColumnIndex(0);
                    std::string typeLabel = (job->type == JobType::Copy) ? "COPY" : "MOVE";
                    ImVec4 typeColor = (job->type == JobType::Copy) ? ImVec4(0.4f, 0.8f, 1.0f, 1) : ImVec4(1.0f, 0.6f, 0.2f, 1);
                    
                    ImGui::PushStyleColor(ImGuiCol_Text, typeColor);
                    bool isSelected = (selectedQueueIndex == i);
                    if (ImGui::Selectable(typeLabel.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedQueueIndex = i;
                    }
                    ImGui::PopStyleColor();

                    // Col 1: File
                    ImGui::TableSetColumnIndex(1); 
                    ImGui::Text("%s", job->source.filename().string().c_str());

                    // Col 2: From
                    ImGui::TableSetColumnIndex(2); 
                    ImGui::Text("%s", job->source.parent_path().string().c_str());

                    // Col 3: To
                    ImGui::TableSetColumnIndex(3); 
                    // FIX: Since we fixed QueueJob, this now correctly shows the parent folder of the full path
                    ImGui::Text("%s", job->destination.parent_path().string().c_str());
                    
                    // Col 4: Status
                    ImGui::TableSetColumnIndex(4); 
                    const char* statusStr = "...";
                    ImVec4 color = ImVec4(1,1,1,1);
                    switch(job->status.load()) {
                        case JobStatus::Pending: statusStr = "WAIT"; color = ImVec4(0.5,0.5,0.5,1); break;
                        case JobStatus::Calculating: statusStr = "SCAN"; color = ImVec4(0,0.8,0.8,1); break;
                        case JobStatus::Copying: statusStr = "BUSY"; color = ImVec4(0,1,1,1); break;
                        case JobStatus::Paused:  statusStr = "PAUSE"; color = ImVec4(1,1,0,1); break;
                        case JobStatus::Completed: statusStr = "DONE"; color = ImVec4(0,1,0,1); break; // Changed OK to DONE
                        case JobStatus::Failed:  statusStr = "ERR"; color = ImVec4(1,0,0,1); break;
                    }
                    ImGui::TextColored(color, statusStr);

                    // Col 5: Progress
                    ImGui::TableSetColumnIndex(5);
                    ImGui::ProgressBar(job->progress, ImVec2(-1, 0), ""); 

                    ImGui::PopID(); 
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild(); // End Queue Box

        ImGui::SameLine();

        // 2. RIGHT BOX: CONTROLS
        ImGui::BeginChild("ControlsRegion", ImVec2(0, bottomHeight), true); // 0 width = fill remaining
        
        ImGui::Text("Controls");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("START ALL", ImVec2(-1, 30))) { transferManager.StartQueue(); }
        ImGui::Spacing();
        if (ImGui::Button("PAUSE ALL", ImVec2(-1, 30))) { 
             if (transferManager.IsPaused()) transferManager.ResumeQueue();
             else transferManager.PauseQueue();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Remove Logic
        bool hasSelection = (selectedQueueIndex >= 0 && selectedQueueIndex < queue.size());
        bool busy = false;
        if (hasSelection && queue[selectedQueueIndex]->status == JobStatus::Copying) busy = true;

        if (!hasSelection || busy) ImGui::BeginDisabled();
        
        if (ImGui::Button("REMOVE ITEM", ImVec2(-1, 30))) {
            transferManager.RemoveJob(selectedQueueIndex);
            selectedQueueIndex = -1; 
        }

        if (!hasSelection || busy) ImGui::EndDisabled();

        ImGui::Spacing();
        if (!hasSelection) ImGui::TextWrapped("Select a job to remove it");

        ImGui::EndChild(); // End Controls Box

        ImGui::End(); // End Main Window

        // Rendering...
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    
    // Uninitialize COM
    CoUninitialize();

    return 0;
}