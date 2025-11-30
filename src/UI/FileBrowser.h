#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <set>
#include <algorithm>
#include <windows.h>
#include "imgui.h"

namespace fs = std::filesystem;

/**
 * @brief Represents a single entry (file or directory) in the browser view.
 */
struct FileEntry {
    fs::path path;
    bool isDirectory;
    std::string displayString;
};

/**
 * @brief A self-contained File Browser component.
 * * Handles filesystem navigation, drive selection, file filtering, 
 * and multi-selection state.
 */
class FileBrowser {
public:
    FileBrowser();

    /**
     * @brief Renders the File Browser UI.
     * @param id Unique ImGui ID for this specific browser instance.
     * @param height The height of the file list child window.
     */
    void Render(const char* id, float height);

    /**
     * @brief Refreshes the file list for the current directory.
     * Should be called when an external operation modifies the filesystem.
     */
    void Refresh();

    /**
     * @brief Returns a list of all currently selected file paths.
     */
    std::vector<fs::path> GetSelectedPaths() const;

    /**
     * @brief Returns the directory currently currently open in the browser.
     */
    fs::path GetCurrentPath() const { return m_currentPath; }

private:
    // Navigation Helpers
    void NavigateUp();
    void NavigateToFile(const fs::path& targetFile);
    void ChangeDrive(char driveLetter);
    std::string GetDisplayPath() const;

    // Internal State
    fs::path m_currentPath;
    std::vector<FileEntry> m_entries;
    std::set<int> m_selectedIndices;
    int m_lastClickedIndex = -1;
    char m_currentDrive = 'C';
    char m_searchFilter[256] = ""; 
};