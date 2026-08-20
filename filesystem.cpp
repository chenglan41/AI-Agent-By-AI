// filesystem.cpp - File system operations implementation
#include "filesystem.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

std::string FileSystem::createFile(const std::string& path) {
    std::ofstream file(path.c_str());
    if (!file.is_open()) {
        return "Failed to create file: " + path;
    }
    file.close();
    return "ok";
}

std::string FileSystem::createFolder(const std::string& path) {
    if (CreateDirectoryA(path.c_str(), NULL)) {
        return "ok";
    }
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        return "ok"; // Folder already exists
    }
    return "Failed to create folder: " + path;
}

std::string FileSystem::deleteFile(const std::string& path) {
    if (DeleteFileA(path.c_str())) {
        return "ok";
    }
    return "Failed to delete file: " + path;
}

std::string FileSystem::deleteFolder(const std::string& path) {
    // Use SHFileOperation for recursive delete
    std::string doubleNullPath = path + "\0\0";
    SHFILEOPSTRUCTA fileOp;
    ZeroMemory(&fileOp, sizeof(fileOp));
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = doubleNullPath.c_str();
    fileOp.fFlags = FOF_NOCONFIRMATION | FOF_SILENT;
    
    int result = SHFileOperationA(&fileOp);
    if (result == 0) {
        return "ok";
    }
    return "Failed to delete folder: " + path;
}

std::string FileSystem::getFileInfo(const std::string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
        return "File not found: " + path;
    }
    
    std::stringstream ss;
    ss << "File: " << path << "\n";
    ss << "Size: " << info.st_size << " bytes\n";
    ss << "Modified: " << info.st_mtime << "\n";
    ss << "Read-only: " << ((info.st_mode & _S_IWRITE) ? "No" : "Yes") << "\n";
    
    return ss.str();
}

std::string FileSystem::getFolderInfo(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return "Folder not found: " + path;
    }
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return "Not a folder: " + path;
    }
    
    std::stringstream ss;
    ss << "Folder: " << path << "\n";
    ss << "Attributes: ";
    if (attr & FILE_ATTRIBUTE_HIDDEN) ss << "Hidden ";
    if (attr & FILE_ATTRIBUTE_SYSTEM) ss << "System ";
    if (attr & FILE_ATTRIBUTE_READONLY) ss << "ReadOnly ";
    ss << "\n";
    
    return ss.str();
}

std::string FileSystem::listFolder(const std::string& path) {
    std::stringstream ss;
    std::string searchPath = path + "\\*";
    
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return "Failed to list folder: " + path;
    }
    
    int fileCount = 0;
    int folderCount = 0;
    
    do {
        std::string name = findData.cFileName;
        if (name == "." || name == "..") continue;
        
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ss << "[DIR] " << name << "\n";
            folderCount++;
        } else {
            ss << "[FILE] " << name << " (" << findData.nFileSizeLow << " bytes)\n";
            fileCount++;
        }
    } while (FindNextFileA(hFind, &findData));
    
    FindClose(hFind);
    
    ss << "\nTotal: " << fileCount << " files, " << folderCount << " folders";
    
    return ss.str();
}

std::string FileSystem::readFile(const std::string& path) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open()) {
        return "Failed to open file: " + path;
    }
    
    std::stringstream ss;
    ss << file.rdbuf();
    file.close();
    
    return ss.str();
}

std::string FileSystem::writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path.c_str(), std::ios::binary);
    if (!file.is_open()) {
        return "Failed to open file for writing: " + path;
    }
    
    file.write(content.c_str(), content.size());
    file.close();
    
    return "ok";
}
