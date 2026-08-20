// filesystem.h - File system operations
#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <string>

class FileSystem {
public:
    // Create a file
    static std::string createFile(const std::string& path);
    
    // Create a folder
    static std::string createFolder(const std::string& path);
    
    // Delete a file
    static std::string deleteFile(const std::string& path);
    
    // Delete a folder
    static std::string deleteFolder(const std::string& path);
    
    // Get file info
    static std::string getFileInfo(const std::string& path);
    
    // Get folder info
    static std::string getFolderInfo(const std::string& path);
    
    // List folder contents
    static std::string listFolder(const std::string& path);
    
    // Read file content
    static std::string readFile(const std::string& path);
    
    // Write to file
    static std::string writeFile(const std::string& path, const std::string& content);
};

#endif // FILESYSTEM_H
