#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <objbase.h> 

#include "Jobs/TransferManager.h"
#include "Core/Logger.h"
#include "UI/FileBrowser.h" 

/**
 * @brief Counts the number of jobs that have been completed.
 * * This helper function is used by the main loop to detect when a job finishes
 * so that the file browser UIs can be auto-refreshed.
 * * @param queue The deque of jobs to check.
 * @return int The count of completed jobs.
 */
int CountCompletedJobs(const std::deque<std::shared_ptr<FileJob>>& queue) {
    int count = 0;
    for (const auto& job : queue) {
        if (job->status == JobStatus::Completed) count++;
    }
    return count;
}

/**
 * @brief The application entry point.
 * * Initializes COM, Logger, GLFW, and ImGui.
 * Contains the main application loop which renders the UI and manages global state.
 */
int main(int, char**)
{
    // Initialize COM for native dialog support
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    ButlerLogger::Init();
    ButlerLogger::Log("Application Starting...");

    if (!glfwInit()) {
        ButlerLogger::Log(LogLevel::ERR, "Failed to initialize GLFW");
        return 1;
    }
    GLFWwindow* window = glfwCreateWindow(1280, 720, "System Butler", nullptr, nullptr);
    if (window == nullptr) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); 

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.0f, 0.7f, 0.0f, 1.0f);
    style.CellPadding = ImVec2(5.0f, 5.0f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // --- SYSTEMS INITIALIZATION ---
    TransferManager transferManager;
    FileBrowser leftBrowser;
    FileBrowser rightBrowser;
    
    int selectedQueueIndex = -1; 
    int previousCompletedCount = 0;

    // --- MAIN LOOP ---
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Check for Auto-Refresh (If a job finished, refresh file lists)
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

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

        ImGui::TextDisabled("SYSBUTLER // FILE COMMANDER");
        ImGui::Separator();
        
        // --- DUAL PANE EXPLORER ---
        float paneHeight = 350.0f;
        
        ImGui::Columns(2, "ExplorerCols", true);
        
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "SOURCE");
        leftBrowser.Render("LeftPane", paneHeight);
        
        ImGui::NextColumn();
        
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "DESTINATION");
        rightBrowser.Render("RightPane", paneHeight);
        
        ImGui::Columns(1); 

        // --- ACTION BUTTONS ---
        ImGui::Spacing();
        ImGui::Separator();
        
        float width = ImGui::GetWindowWidth();
        ImGui::SetCursorPosX((width - 300) * 0.5f);
        
        // Batch Processing Logic
        std::vector<fs::path> sources = leftBrowser.GetSelectedPaths();
        bool canCopy = !sources.empty();

        if (ImGui::Button("COPY >>>", ImVec2(140, 40)) && canCopy) {
            for (const auto& src : sources) {
                transferManager.QueueJob(src, rightBrowser.GetCurrentPath(), JobType::Copy);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("MOVE >>>", ImVec2(140, 40)) && canCopy) {
             for (const auto& src : sources) {
                transferManager.QueueJob(src, rightBrowser.GetCurrentPath(), JobType::Move);
             }
        }
        
        ImGui::Separator();

        // --- QUEUE & CONTROLS ---
        float bottomHeight = 250.0f;
        float controlsWidth = 180.0f;
        float tableWidth = ImGui::GetContentRegionAvail().x - controlsWidth - 10.0f;

        // 1. Queue Table Child
        ImGui::BeginChild("QueueRegion", ImVec2(tableWidth, bottomHeight), true);
        ImGui::Text("Active Transfer Queue");
        ImGui::Separator();

        auto queue = transferManager.GetQueue();

        if (queue.empty()) {
             float winHeight = ImGui::GetWindowHeight();
             ImGui::SetCursorPosY(winHeight * 0.4f);
             ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.35f);
             ImGui::TextDisabled("No active jobs pending.");
        } 
        else {
            if (ImGui::BeginTable("QueueTable", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
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
                    
                    ImGui::TableSetColumnIndex(0);
                    std::string typeLabel = (job->type == JobType::Copy) ? "COPY" : "MOVE";
                    ImVec4 typeColor = (job->type == JobType::Copy) ? ImVec4(0.4f, 0.8f, 1.0f, 1) : ImVec4(1.0f, 0.6f, 0.2f, 1);
                    ImGui::PushStyleColor(ImGuiCol_Text, typeColor);
                    if (ImGui::Selectable(typeLabel.c_str(), selectedQueueIndex == i, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedQueueIndex = i;
                    }
                    ImGui::PopStyleColor();

                    ImGui::TableSetColumnIndex(1); ImGui::Text("%s", job->source.filename().string().c_str());
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%s", job->source.parent_path().string().c_str());
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%s", job->destination.parent_path().string().c_str());
                    
                    ImGui::TableSetColumnIndex(4);
                    const char* statusStr = "...";
                    ImVec4 color = ImVec4(1,1,1,1);
                    switch(job->status.load()) {
                        case JobStatus::Pending:    statusStr = "WAIT"; color = ImVec4(0.5,0.5,0.5,1); break;
                        case JobStatus::Calculating:statusStr = "SCAN"; color = ImVec4(0,0.8,0.8,1); break;
                        case JobStatus::Copying:    statusStr = "BUSY"; color = ImVec4(0,1,1,1); break;
                        case JobStatus::Paused:     statusStr = "PAUSE"; color = ImVec4(1,1,0,1); break;
                        case JobStatus::Completed:  statusStr = "DONE"; color = ImVec4(0,1,0,1); break;
                        case JobStatus::Failed:     statusStr = "ERR"; color = ImVec4(1,0,0,1); break;
                    }
                    ImGui::TextColored(color, statusStr);

                    ImGui::TableSetColumnIndex(5);
                    ImGui::ProgressBar(job->progress, ImVec2(-1, 0), ""); 
                    ImGui::PopID(); 
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // 2. Controls Child
        ImGui::BeginChild("ControlsRegion", ImVec2(0, bottomHeight), true);
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

        bool hasSelection = (selectedQueueIndex >= 0 && selectedQueueIndex < queue.size());
        bool busy = (hasSelection && queue[selectedQueueIndex]->status == JobStatus::Copying);

        if (!hasSelection || busy) ImGui::BeginDisabled();
        if (ImGui::Button("REMOVE ITEM", ImVec2(-1, 30))) {
            transferManager.RemoveJob(selectedQueueIndex);
            selectedQueueIndex = -1; 
        }
        if (!hasSelection || busy) ImGui::EndDisabled();

        ImGui::Spacing();
        if (!hasSelection) ImGui::TextWrapped("Select a job to remove it");

        ImGui::EndChild();
        ImGui::End(); 

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    CoUninitialize();

    return 0;
}