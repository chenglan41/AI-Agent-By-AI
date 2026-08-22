// filesystem.cpp - File system operations implementation
// 所有路径操作统一使用宽字符(W) API：
//   输入路径：AI 传入 UTF-8 → 转 UTF-16 后调用 W 版 API，解决中文路径找不到的问题
//   输出名称：W 版 API 返回 UTF-16（如文件名）→ 转 UTF-8 返回，避免 GBK 字节混入
#include "filesystem.h"
#include <windows.h>
#include <cstdio>
#include <sstream>
#include <sys/stat.h>

// ========== 编码转换工具 ==========
// UTF-8 -> UTF-16
static std::wstring UTF8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    if (len <= 0) return L"";
    std::wstring w(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], len);
    return w;
}

// UTF-16 -> UTF-8
static std::string WideToUTF8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string s(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, NULL, NULL);
    return s;
}
// ==================================

std::string FileSystem::createFile(const std::string& path) {
    FILE* f = _wfopen(UTF8ToWide(path).c_str(), L"wb");
    if (!f) {
        return "Failed to create file: " + path;
    }
    fclose(f);
    return "ok";
}

std::string FileSystem::createFolder(const std::string& path) {
    if (CreateDirectoryW(UTF8ToWide(path).c_str(), NULL)) {
        return "ok";
    }
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        return "ok"; // Folder already exists
    }
    return "Failed to create folder: " + path;
}

std::string FileSystem::deleteFile(const std::string& path) {
    if (DeleteFileW(UTF8ToWide(path).c_str())) {
        return "ok";
    }
    return "Failed to delete file: " + path;
}

std::string FileSystem::deleteFolder(const std::string& path) {
    // Use SHFileOperationW for recursive delete
    // pFrom 需要双 null 结尾；用 wstring 显式补两个 L'\0'（旧代码 path + "\0\0" 实际没追加）
    std::wstring doubleNullPath = UTF8ToWide(path);
    doubleNullPath.push_back(L'\0');
    doubleNullPath.push_back(L'\0');
    
    SHFILEOPSTRUCTW fileOp;
    ZeroMemory(&fileOp, sizeof(fileOp));
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = doubleNullPath.c_str();
    fileOp.fFlags = FOF_NOCONFIRMATION | FOF_SILENT;
    
    int result = SHFileOperationW(&fileOp);
    if (result == 0) {
        return "ok";
    }
    return "Failed to delete folder: " + path;
}

std::string FileSystem::getFileInfo(const std::string& path) {
    struct _stat info;
    if (_wstat(UTF8ToWide(path).c_str(), &info) != 0) {
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
    DWORD attr = GetFileAttributesW(UTF8ToWide(path).c_str());
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
    std::wstring searchPath = UTF8ToWide(path) + L"\\*";
    
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return "Failed to list folder: " + path;
    }
    
    int fileCount = 0;
    int folderCount = 0;
    
    do {
        // cFileName 是 UTF-16，转成 UTF-8 返回，中文名不再乱码
        std::string name = WideToUTF8(findData.cFileName);
        if (name == "." || name == "..") continue;
        
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ss << "[DIR] " << name << "\n";
            folderCount++;
        } else {
            ss << "[FILE] " << name << " (" << findData.nFileSizeLow << " bytes)\n";
            fileCount++;
        }
    } while (FindNextFileW(hFind, &findData));
    
    FindClose(hFind);
    
    ss << "\nTotal: " << fileCount << " files, " << folderCount << " folders";
    
    return ss.str();
}

std::string FileSystem::readFile(const std::string& path) {
    FILE* f = _wfopen(UTF8ToWide(path).c_str(), L"rb");
    if (!f) {
        return "Failed to open file: " + path;
    }
    
    std::string content;
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        content.append(buffer, n);
    }
    fclose(f);
    
    return content;
}

std::string FileSystem::writeFile(const std::string& path, const std::string& content) {
    FILE* f = _wfopen(UTF8ToWide(path).c_str(), L"wb");
    if (!f) {
        return "Failed to open file for writing: " + path;
    }
    
    if (!content.empty()) {
        fwrite(content.c_str(), 1, content.size(), f);
    }
    fclose(f);
    
    return "ok";
}
