// tools.cpp - Tool dispatcher implementation
#include "tools.h"
#include <sstream>

ToolDispatcher::ToolDispatcher() {
}

ToolDispatcher::~ToolDispatcher() {
}

std::string ToolDispatcher::execute(const std::string& toolName, const json::Value& params) {
    // Terminal tools
    if (toolName == "terminal_create") {
        int id = terminalMgr_.createProcess();
        if (id < 0) return "Failed to create terminal";
        std::stringstream ss;
        ss << id;
        return ss.str();
    }
    if (toolName == "terminal_remove") {
        if (!params.has("terminal_id")) return "Missing terminal_id";
        int id = params["terminal_id"].asInt();
        return terminalMgr_.removeProcess(id);
    }
    if (toolName == "terminal_input") {
        if (!params.has("terminal_id") || !params.has("command")) 
            return "Missing terminal_id or command";
        int id = params["terminal_id"].asInt();
        std::string cmd = params["command"].asString();
        return terminalMgr_.inputProcess(id, cmd);
    }
    if (toolName == "terminal_output") {
        if (!params.has("terminal_id")) return "Missing terminal_id";
        int id = params["terminal_id"].asInt();
        return terminalMgr_.outputProcess(id);
    }
    
    // Mouse tools
    if (toolName == "mouse_click") {
        if (!params.has("x") || !params.has("y")) return "Missing x or y";
        int x = params["x"].asInt();
        int y = params["y"].asInt();
        int duration = params.has("duration_ms") ? params["duration_ms"].asInt() : 100;
        return MouseController::click(x, y, duration);
    }
    if (toolName == "mouse_drag") {
        if (!params.has("x1") || !params.has("y1") || 
            !params.has("x2") || !params.has("y2")) 
            return "Missing coordinates";
        int x1 = params["x1"].asInt();
        int y1 = params["y1"].asInt();
        int x2 = params["x2"].asInt();
        int y2 = params["y2"].asInt();
        return MouseController::drag(x1, y1, x2, y2);
    }
    
    // Keyboard tools
    if (toolName == "key_press") {
        if (!params.has("key")) return "Missing key";
        std::string key = params["key"].asString();
        int duration = params.has("duration_ms") ? params["duration_ms"].asInt() : 50;
        return KeyboardController::press(key, duration);
    }
    if (toolName == "key_combo") {
        if (!params.has("key1") || !params.has("key2")) return "Missing key1 or key2";
        std::string key1 = params["key1"].asString();
        std::string key2 = params["key2"].asString();
        int duration = params.has("duration_ms") ? params["duration_ms"].asInt() : 50;
        return KeyboardController::combo(key1, key2, duration);
    }
    
    // File system tools
    if (toolName == "file_create") {
        if (!params.has("path")) return "Missing path";
        return FileSystem::createFile(params["path"].asString());
    }
    if (toolName == "folder_create") {
        if (!params.has("path")) return "Missing path";
        return FileSystem::createFolder(params["path"].asString());
    }
    if (toolName == "file_delete") {
        if (!params.has("path")) return "Missing path";
        return FileSystem::deleteFile(params["path"].asString());
    }
    if (toolName == "folder_delete") {
        if (!params.has("path")) return "Missing path";
        return FileSystem::deleteFolder(params["path"].asString());
    }
    if (toolName == "file_info") {
        if (!params.has("path")) return "Missing path";
        return FileSystem::getFileInfo(params["path"].asString());
    }
    if (toolName == "folder_info") {
        if (!params.has("path")) return "Missing path";
        return FileSystem::getFolderInfo(params["path"].asString());
    }
    if (toolName == "folder_list") {
        if (!params.has("path")) return "Missing path";
        return FileSystem::listFolder(params["path"].asString());
    }
    if (toolName == "file_read") {
        if (!params.has("path")) return "Missing path";
        return FileSystem::readFile(params["path"].asString());
    }
    if (toolName == "file_write") {
        if (!params.has("path") || !params.has("content")) 
            return "Missing path or content";
        return FileSystem::writeFile(params["path"].asString(), 
                                     params["content"].asString());
    }
    
    return "Unknown tool: " + toolName;
}

std::string ToolDispatcher::getToolList() {
    std::stringstream ss;
    ss << "Available tools:\n";
    ss << "- terminal_create: Create terminal session\n";
    ss << "- terminal_remove: Close terminal (terminal_id)\n";
    ss << "- terminal_input: Send command (terminal_id, command)\n";
    ss << "- terminal_output: Read buffer (terminal_id)\n";
    ss << "- mouse_click: Click (x, y, duration_ms)\n";
    ss << "- mouse_drag: Drag (x1, y1, x2, y2)\n";
    ss << "- key_press: Press key (key, duration_ms)\n";
    ss << "- key_combo: Combo keys (key1, key2, duration_ms)\n";
    ss << "- file_create: Create file (path)\n";
    ss << "- folder_create: Create folder (path)\n";
    ss << "- file_delete: Delete file (path)\n";
    ss << "- folder_delete: Delete folder (path)\n";
    ss << "- file_info: File info (path)\n";
    ss << "- folder_info: Folder info (path)\n";
    ss << "- folder_list: List folder (path)\n";
    ss << "- file_read: Read file (path)\n";
    ss << "- file_write: Write file (path, content)\n";
    return ss.str();
}
