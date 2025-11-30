#include "FileBrowser.h"
#include "../Core/PlatformUtils.h"
#include <cctype> // For toupper

// Helper for case-insensitive search
static bool StringContains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
    );
    return (it != haystack.end());
}

FileBrowser::FileBrowser() {
    m_currentPath = fs::current_path().root_path();
    Refresh();
}

void FileBrowser::Refresh() {
    m_entries.clear();
    m_selectedIndices.clear();
    m_lastClickedIndex = -1;

    try {
        for (const auto& entry : fs::directory_iterator(m_currentPath)) {
            FileEntry e;
            e.path = entry.path();
            e.isDirectory = entry.is_directory();
            // Prefix directories for visual distinction
            e.displayString = (e.isDirectory ? "[DIR] " : "      ") + e.path.filename().string();
            m_entries.push_back(e);
        }
        // Sort: Directories first, then alphabetical
        std::sort(m_entries.begin(), m_entries.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            return a.path < b.path;
        });
    } catch (...) {
        // Fail silently or log if needed; implies permission denied usually
    }
}

std::vector<fs::path> FileBrowser::GetSelectedPaths() const {
    std::vector<fs::path> result;
    for (int index : m_selectedIndices) {
        if (index >= 0 && index < m_entries.size()) {
            result.push_back(m_entries[index].path);
        }
    }
    return result;
}

std::string FileBrowser::GetDisplayPath() const {
    std::string str = m_currentPath.string();
    
    // Normalize root path string
    if (str.length() > 3 && str.back() == '\\') str.pop_back();

    if (m_selectedIndices.size() == 1) {
        int idx = *m_selectedIndices.begin();
        if (idx < m_entries.size()) {
            if (str.back() != '\\') str += "\\";
            str += m_entries[idx].path.filename().string();
        }
    } else if (m_selectedIndices.size() > 1) {
        if (str.back() != '\\') str += "\\";
        str += "*"; // Indicator for multiple selection
    }
    return str;
}

void FileBrowser::Render(const char* id, float height) {
    ImGui::PushID(id);
    ImGui::BeginGroup();

    // --- Toolbar ---
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { NavigateUp(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to Parent Folder");
    
    ImGui::SameLine();

    // Drive Selector
    char driveLabel[4] = "C:\\"; 
    driveLabel[0] = m_currentDrive;
    ImGui::SetNextItemWidth(50);
    if (ImGui::BeginCombo("##drive", driveLabel)) {
        DWORD mask = GetLogicalDrives();
        for (char c = 'A'; c <= 'Z'; c++) {
            if (mask & 1) {
                char d[4] = "X:\\"; d[0] = c;
                if (ImGui::Selectable(d, m_currentDrive == c)) ChangeDrive(c);
            }
            mask >>= 1;
        }
        ImGui::EndCombo();
    }
    
    ImGui::SameLine();

    // Search Filter
    float availableWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(availableWidth - 40.0f);
    ImGui::InputTextWithHint("##search", "Search files...", m_searchFilter, IM_ARRAYSIZE(m_searchFilter));
    
    ImGui::SameLine();

    // Native Explorer Picker ("Deep Link")
    if (ImGui::Button("...")) {
        std::string picked = PlatformUtils::OpenFilePicker();
        if (!picked.empty()) NavigateToFile(fs::path(picked));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Locate file using Windows Explorer");

    // Path Display
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", GetDisplayPath().c_str());

    // --- File List ---
    ImGui::BeginChild("Files", ImVec2(0, height), true);
    
    for (int i = 0; i < m_entries.size(); i++) {
        const auto& entry = m_entries[i];
        
        if (!StringContains(entry.path.filename().string(), m_searchFilter)) continue;

        bool isSelected = (m_selectedIndices.find(i) != m_selectedIndices.end());
        
        if (ImGui::Selectable(entry.displayString.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
            
            if (ImGui::GetIO().KeyCtrl) {
                // Toggle Selection
                if (isSelected) m_selectedIndices.erase(i);
                else m_selectedIndices.insert(i);
                m_lastClickedIndex = i;
            }
            else if (ImGui::GetIO().KeyShift && m_lastClickedIndex != -1) {
                // Range Selection
                m_selectedIndices.clear();
                int start = std::min(m_lastClickedIndex, i);
                int end = std::max(m_lastClickedIndex, i);
                for (int k = start; k <= end; k++) m_selectedIndices.insert(k);
            }
            else {
                // Single Selection
                m_selectedIndices.clear();
                m_selectedIndices.insert(i);
                m_lastClickedIndex = i;
            }

            if (ImGui::IsMouseDoubleClicked(0) && entry.isDirectory) {
                m_currentPath = entry.path;
                memset(m_searchFilter, 0, sizeof(m_searchFilter));
                Refresh();
            }
        }
        
        if (isSelected && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
    }
    ImGui::EndChild();

    ImGui::EndGroup();
    ImGui::PopID();
}

void FileBrowser::NavigateUp() {
    if (m_currentPath.has_parent_path() && m_currentPath != m_currentPath.root_path()) {
        m_currentPath = m_currentPath.parent_path();
        Refresh();
    }
}

void FileBrowser::ChangeDrive(char driveLetter) {
    std::string d = std::string(1, driveLetter) + ":\\";
    m_currentPath = d;
    m_currentDrive = driveLetter;
    Refresh();
}

void FileBrowser::NavigateToFile(const fs::path& targetFile) {
    if (!fs::exists(targetFile)) return;
    
    m_currentPath = targetFile.parent_path();
    std::string pathStr = m_currentPath.string();
    if (pathStr.length() >= 2 && pathStr[1] == ':') m_currentDrive = toupper(pathStr[0]);
    
    Refresh();
    
    memset(m_searchFilter, 0, sizeof(m_searchFilter));
    for (int i = 0; i < m_entries.size(); i++) {
        if (m_entries[i].path == targetFile) {
            m_selectedIndices.insert(i);
            m_lastClickedIndex = i;
            break;
        }
    }
}