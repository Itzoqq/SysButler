#pragma once
#include <windows.h>
#include <string>
#include <commdlg.h>
#include <shobjidl.h> // Required for IFileOpenDialog

class PlatformUtils {
public:
    // Existing File Picker
    static std::string OpenFilePicker() {
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn) == TRUE) return std::string(ofn.lpstrFile);
        return "";
    }

    // NEW: Proper Folder Picker
    static std::string OpenFolderPicker() {
        std::string result = "";
        IFileOpenDialog* pFileOpen;

        // Create the FileOpenDialog object.
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, 
            IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

        if (SUCCEEDED(hr)) {
            // Set options to pick folders
            DWORD dwOptions;
            if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
                pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
            }

            // Show the dialog
            if (SUCCEEDED(pFileOpen->Show(NULL))) {
                IShellItem* pItem;
                if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                    PWSTR pszFilePath;
                    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                        // Convert WCHAR to std::string (simple ANSI conversion)
                        char buffer[512];
                        WideCharToMultiByte(CP_ACP, 0, pszFilePath, -1, buffer, 512, NULL, NULL);
                        result = std::string(buffer);
                        CoTaskMemFree(pszFilePath);
                    }
                    pItem->Release();
                }
            }
            pFileOpen->Release();
        }
        return result;
    }
};