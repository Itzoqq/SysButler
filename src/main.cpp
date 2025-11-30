#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string>
#include <filesystem> 
#include <objbase.h> // Required for CoInitializeEx

// YOUR HEADERS
#include "Jobs/TransferManager.h"
#include "Core/Logger.h"
#include "Core/PlatformUtils.h"

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
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- SYSBUTLER UI START ---
        
        // Make the window fill the whole app for a "Dashboard" feel
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

        // Header
        ImGui::TextDisabled("SYSBUTLER // FILE OPERATIONS");
        ImGui::Separator();
        ImGui::Spacing();

        // SPLIT LAYOUT: LEFT (Input) vs RIGHT (Queue)
        ImGui::Columns(2, "MainLayout", true); 
        
        // --- LEFT COLUMN: INPUTS ---
        ImGui::Text("Add to Transfer Queue");
        
        // 1. SOURCE INPUT + PICKERS
        ImGui::Text("Source Path");
        ImGui::PushItemWidth(-100); // Leave room for 2 buttons
        ImGui::InputText("##Source", uiState.sourceBuffer, IM_ARRAYSIZE(uiState.sourceBuffer));
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        if (ImGui::Button("File##Src", ImVec2(40, 0))) { 
            std::string selected = PlatformUtils::OpenFilePicker();
            if (!selected.empty()) {
                strncpy(uiState.sourceBuffer, selected.c_str(), sizeof(uiState.sourceBuffer) - 1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Fold##Src", ImVec2(40, 0))) { 
            std::string selected = PlatformUtils::OpenFolderPicker();
            if (!selected.empty()) {
                strncpy(uiState.sourceBuffer, selected.c_str(), sizeof(uiState.sourceBuffer) - 1);
            }
        }

        // 2. DEST INPUT + PICKERS
        ImGui::Text("Dest Path");
        ImGui::PushItemWidth(-100); 
        ImGui::InputText("##Dest", uiState.destBuffer, IM_ARRAYSIZE(uiState.destBuffer));
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("File##Dst", ImVec2(40, 0))) { 
            // File picker for dest: assume user wants the parent folder of the file they picked
            std::string selected = PlatformUtils::OpenFilePicker();
            if (!selected.empty()) {
                std::filesystem::path p(selected);
                std::string folder = p.parent_path().string();
                strncpy(uiState.destBuffer, folder.c_str(), sizeof(uiState.destBuffer) - 1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Fold##Dst", ImVec2(40, 0))) { 
             std::string selected = PlatformUtils::OpenFolderPicker();
             if (!selected.empty()) {
                strncpy(uiState.destBuffer, selected.c_str(), sizeof(uiState.destBuffer) - 1);
             }
        }
        
        ImGui::Spacing();

        // ACTION BUTTONS (Split 2 Columns internally)
        ImGui::Columns(2, "ButtonColumns", false); 

        // Button 1: COPY (Blue)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.8f, 1.0f)); 
        if (ImGui::Button("Queue COPY Job", ImVec2(-1, 40))) {
            if (strlen(uiState.sourceBuffer) > 0 && strlen(uiState.destBuffer) > 0) {
                transferManager.QueueJob(uiState.sourceBuffer, uiState.destBuffer, JobType::Copy);
            }
        }
        ImGui::PopStyleColor();

        ImGui::NextColumn();

        // Button 2: MOVE (Orange)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.2f, 1.0f)); 
        if (ImGui::Button("Queue MOVE Job", ImVec2(-1, 40))) {
            if (strlen(uiState.sourceBuffer) > 0 && strlen(uiState.destBuffer) > 0) {
                transferManager.QueueJob(uiState.sourceBuffer, uiState.destBuffer, JobType::Move);
            }
        }
        ImGui::PopStyleColor();

        ImGui::Columns(1); // Reset internal columns
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextWrapped("Tip: Use [Fold] to select entire folders. 'Copy' duplicates. 'Move' deletes source.");

        ImGui::NextColumn(); 
        
        // --- RIGHT COLUMN: QUEUE ---

        ImGui::Text("Active Transfer Queue");
        
        // Control Buttons
        if (ImGui::Button(transferManager.IsRunning() ? "Running..." : "START QUEUE")) {
            transferManager.StartQueue();
        }
        ImGui::SameLine();
        if (ImGui::Button(transferManager.IsPaused() ? "RESUME" : "PAUSE")) {
            if (transferManager.IsPaused()) transferManager.ResumeQueue();
            else transferManager.PauseQueue();
        }

        ImGui::Spacing();

        // THE TABLE
        if (ImGui::BeginTable("QueueTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            
            // Setup Headers
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("To",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Act", ImGuiTableColumnFlags_WidthFixed, 40.0f); // Action (Delete)
            ImGui::TableHeadersRow();

            // Store index to delete (cannot delete while iterating)
            int jobToRemove = -1;

            auto queue = transferManager.GetQueue();
            for (int i = 0; i < queue.size(); i++) {
                auto job = queue[i];

                ImGui::TableNextRow();
                
                // Col 0: TYPE
                ImGui::TableSetColumnIndex(0);
                if (job->type == JobType::Copy) 
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1), "CPY");
                else 
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1), "MOV");

                // Col 1: Filename
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", job->source.filename().string().c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", job->source.string().c_str());

                // Col 2: Source Folder (Parent Path)
                ImGui::TableSetColumnIndex(2);
                std::string srcFolder = job->source.parent_path().string();
                ImGui::Text("%s", srcFolder.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", srcFolder.c_str());

                // Col 3: Dest Folder (Parent Path)
                ImGui::TableSetColumnIndex(3);
                std::string destFolder = job->destination.parent_path().string();
                if (destFolder.empty()) destFolder = job->destination.string();
                ImGui::Text("%s", destFolder.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", destFolder.c_str());

                // Col 4: Status
                ImGui::TableSetColumnIndex(4);
                const char* statusStr = "...";
                ImVec4 color = ImVec4(1,1,1,1);
                switch(job->status.load()) {
                    case JobStatus::Pending: statusStr = "WAIT"; color = ImVec4(0.5,0.5,0.5,1); break;
                    case JobStatus::Calculating: statusStr = "SCAN";  color = ImVec4(0.0f, 0.8f, 0.8f, 1.0f); break;
                    case JobStatus::Copying: statusStr = "BUSY"; color = ImVec4(0,1,1,1); break;
                    case JobStatus::Paused:  statusStr = "PAUSE";color = ImVec4(1,1,0,1); break;
                    case JobStatus::Completed: statusStr = "OK"; color = ImVec4(0,1,0,1); break;
                    case JobStatus::Failed:  statusStr = "ERR";  color = ImVec4(1,0,0,1); break;
                }
                ImGui::TextColored(color, statusStr);
                if (job->status == JobStatus::Failed && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Error: %s", job->errorMessage.c_str());
                }

                // Col 5: Progressgit
                ImGui::TableSetColumnIndex(5);
                ImGui::ProgressBar(job->progress, ImVec2(-1, 0), ""); 

                // Col 6: Action (Delete)
                ImGui::TableSetColumnIndex(6);
                ImGui::PushID(i); // Unique ID for button
                
                // Disable delete if currently copying
                if (job->status == JobStatus::Copying) ImGui::BeginDisabled();
                if (ImGui::Button("X")) {
                    jobToRemove = i;
                }
                if (job->status == JobStatus::Copying) ImGui::EndDisabled();
                
                ImGui::PopID();
            }

            ImGui::EndTable();

            // Perform Deletion Safely
            if (jobToRemove != -1) {
                transferManager.RemoveJob(jobToRemove);
            }
        }

        ImGui::End(); // End Dashboard
        // --- SYSBUTLER UI END ---

        // --- RENDER ---
        ImGui::Render(); // <--- THIS WAS MISSING AND CAUSED THE CRASH
        
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