// terminal.cpp - ConPTY Terminal Manager implementation
#include "terminal.h"
#include <sstream>
#include <iostream>
#include <vector>

// ========== 编码转换 ==========
// 微软官方约定：ConPTY 输入/输出管道均为 UTF-8。
//   - 写入命令：直接写 UTF-8
//   - 读取输出：管道内容就是 UTF-8，直接返回；若出现非 UTF-8 字节流（个别 GBK 情况），兜底转 UTF-8。

static std::string GBKToUTF8(const std::string& gbkStr) {
    if (gbkStr.empty()) return gbkStr;
    
    // GBK -> UTF-16
    int wlen = MultiByteToWideChar(CP_ACP, 0, gbkStr.c_str(), (int)gbkStr.size(), NULL, 0);
    if (wlen <= 0) return gbkStr;
    
    std::vector<wchar_t> wstr(wlen);
    MultiByteToWideChar(CP_ACP, 0, gbkStr.c_str(), (int)gbkStr.size(), &wstr[0], wlen);
    
    // UTF-16 -> UTF-8
    int ulen = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], wlen, NULL, 0, NULL, NULL);
    if (ulen <= 0) return gbkStr;
    
    std::vector<char> utf8Str(ulen);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], wlen, &utf8Str[0], ulen, NULL, NULL);
    
    return std::string(&utf8Str[0], ulen);
}

static bool isUTF8String(const std::string& str) {
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = (unsigned char)str[i];
        int bytes = 0;
        if (c < 0x80) {
            bytes = 1;
        } else if ((c & 0xE0) == 0xC0) {
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
            bytes = 4;
        } else {
            return false;
        }
        if (i + bytes > str.size()) return false;
        for (int j = 1; j < bytes; j++) {
            if (((unsigned char)str[i + j] & 0xC0) != 0x80) return false;
        }
        i += bytes;
    }
    return true;
}
// ====================================================================

TerminalManager::TerminalManager() : nextId_(1), pCreatePseudoConsole(NULL),
    pClosePseudoConsole(NULL), pResizePseudoConsole(NULL),
    pInitializeProcThreadAttributeList(NULL), pUpdateProcThreadAttribute(NULL),
    pDeleteProcThreadAttributeList(NULL), hKernel32(NULL) {
    loadConPTY();
}

TerminalManager::~TerminalManager() {
    for (std::map<int, TerminalSession>::iterator it = sessions_.begin(); 
         it != sessions_.end(); ++it) {
        removeProcess(it->first);
    }
    if (hKernel32) FreeLibrary(hKernel32);
}

bool TerminalManager::loadConPTY() {
    hKernel32 = LoadLibraryA("kernel32.dll");
    if (!hKernel32) return false;
    
    pCreatePseudoConsole = (CreatePseudoConsole_t)GetProcAddress(hKernel32, "CreatePseudoConsole");
    pClosePseudoConsole = (ClosePseudoConsole_t)GetProcAddress(hKernel32, "ClosePseudoConsole");
    pResizePseudoConsole = (ResizePseudoConsole_t)GetProcAddress(hKernel32, "ResizePseudoConsole");
    
    // Load ProcThreadAttribute functions
    pInitializeProcThreadAttributeList = (InitializeProcThreadAttributeList_t)GetProcAddress(hKernel32, "InitializeProcThreadAttributeList");
    pUpdateProcThreadAttribute = (UpdateProcThreadAttribute_t)GetProcAddress(hKernel32, "UpdateProcThreadAttribute");
    pDeleteProcThreadAttributeList = (DeleteProcThreadAttributeList_t)GetProcAddress(hKernel32, "DeleteProcThreadAttributeList");
    
    return (pCreatePseudoConsole != NULL && pInitializeProcThreadAttributeList != NULL);
}

int TerminalManager::createProcess() {
    if (!pCreatePseudoConsole || !pInitializeProcThreadAttributeList) {
        return -1;
    }
    
    HANDLE hInput = INVALID_HANDLE_VALUE;
    HANDLE hOutput = INVALID_HANDLE_VALUE;
    
    // Create pipes
    HANDLE hInPipeRead, hInPipeWrite;
    HANDLE hOutPipeRead, hOutPipeWrite;
    
    if (!CreatePipe(&hInPipeRead, &hInPipeWrite, NULL, 0)) return -1;
    if (!CreatePipe(&hOutPipeRead, &hOutPipeWrite, NULL, 0)) {
        CloseHandle(hInPipeRead);
        CloseHandle(hInPipeWrite);
        return -1;
    }
    
    // Create pseudo console
    COORD consoleSize = {80, 25};
    void* hConPTY = NULL;
    
    HRESULT hr = pCreatePseudoConsole(consoleSize, hInPipeRead, hOutPipeWrite, 0, &hConPTY);
    if (hr != S_OK) {
        CloseHandle(hInPipeRead);
        CloseHandle(hInPipeWrite);
        CloseHandle(hOutPipeRead);
        CloseHandle(hOutPipeWrite);
        return -1;
    }
    
    // Prepare startup info
    STARTUPINFOEXA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    
    si.StartupInfo.cb = sizeof(STARTUPINFOEXA);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = hInPipeRead;
    si.StartupInfo.hStdOutput = hOutPipeWrite;
    si.StartupInfo.hStdError = hOutPipeWrite;
    
    // Initialize attribute list
    SIZE_T attrListSize = 0;
    pInitializeProcThreadAttributeList(NULL, 1, 0, &attrListSize);
    
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attrListSize);
    pInitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrListSize);
    pUpdateProcThreadAttribute(si.lpAttributeList, 0, 
        PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hConPTY, sizeof(void*), NULL, NULL);
    
    // Create cmd.exe process
    char cmdPath[] = "C:\\Windows\\System32\\cmd.exe";
    BOOL success = CreateProcessA(NULL, cmdPath, NULL, NULL, FALSE,
        EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &si.StartupInfo, &pi);
    
    pDeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    
    if (!success) {
        pClosePseudoConsole(hConPTY);
        CloseHandle(hInPipeRead);
        CloseHandle(hInPipeWrite);
        CloseHandle(hOutPipeRead);
        CloseHandle(hOutPipeWrite);
        return -1;
    }
    
    // Close handles that are now owned by the child process
    CloseHandle(hInPipeRead);
    CloseHandle(hOutPipeWrite);
    
    // Create session
    TerminalSession session;
    session.id = nextId_++;
    session.hProcess = pi.hProcess;
    session.hThread = pi.hThread;
    session.hInPipe = hInPipeWrite;
    session.hOutPipe = hOutPipeRead;
    session.hConPTY = hConPTY;
    session.buffer = "";
    session.active = true;
    
    sessions_[session.id] = session;
    
    return session.id;
}

std::string TerminalManager::removeProcess(int terminalId) {
    std::map<int, TerminalSession>::iterator it = sessions_.find(terminalId);
    if (it == sessions_.end()) {
        return "Terminal not found";
    }
    
    TerminalSession& session = it->second;
    
    // Close handles
    if (session.hInPipe != INVALID_HANDLE_VALUE) CloseHandle(session.hInPipe);
    if (session.hOutPipe != INVALID_HANDLE_VALUE) CloseHandle(session.hOutPipe);
    if (session.hProcess != INVALID_HANDLE_VALUE) {
        TerminateProcess(session.hProcess, 0);
        CloseHandle(session.hProcess);
    }
    if (session.hThread != INVALID_HANDLE_VALUE) CloseHandle(session.hThread);
    if (session.hConPTY && pClosePseudoConsole) {
        pClosePseudoConsole(session.hConPTY);
    }
    
    sessions_.erase(it);
    return "ok";
}

std::string TerminalManager::inputProcess(int terminalId, const std::string& command) {
    std::map<int, TerminalSession>::iterator it = sessions_.find(terminalId);
    if (it == sessions_.end()) {
        return "Terminal not found";
    }
    
    TerminalSession& session = it->second;
    if (!session.active) return "Terminal not active";
    
    // ConPTY 输入管道期望 UTF-8，AI 的命令本身就是 UTF-8，直接写入
    std::string cmd = command + "\r\n";
    DWORD written;
    if (!WriteFile(session.hInPipe, cmd.c_str(), (DWORD)cmd.size(), &written, NULL)) {
        return "Write failed";
    }
    
    return "ok";
}

std::string TerminalManager::outputProcess(int terminalId) {
    std::map<int, TerminalSession>::iterator it = sessions_.find(terminalId);
    if (it == sessions_.end()) {
        return "Terminal not found";
    }
    
    TerminalSession& session = it->second;
    if (!session.active) return "Terminal not active";
    
    // Read available data（ConPTY 输出管道内容为 UTF-8）
    char buffer[4096];
    DWORD bytesRead;
    std::string output;
    
    while (PeekNamedPipe(session.hOutPipe, NULL, 0, NULL, &bytesRead, NULL) && bytesRead > 0) {
        if (ReadFile(session.hOutPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
            output.append(buffer, bytesRead);
        } else {
            break;
        }
    }
    
    session.buffer += output;
    
    // Keep buffer size reasonable（裁剪时避免劈开 UTF-8 多字节字符）
    if (session.buffer.size() > 10000) {
        size_t cut = session.buffer.size() - 10000;
        std::string tail = session.buffer.substr(cut);
        // 若 tail 以 UTF-8 续字节(0x80-0xBF)开头，说明一个多字节字符被劈开，丢弃残缺字节
        while (!tail.empty() && (((unsigned char)tail[0] & 0xC0) == 0x80)) {
            tail = tail.substr(1);
        }
        session.buffer = tail;
    }
    
    // 输出已是 UTF-8，直接返回；若检测到 GBK 字节流则兜底转换
    if (isUTF8String(session.buffer)) {
        return session.buffer;
    }
    return GBKToUTF8(session.buffer);
}

int TerminalManager::getFirstSessionId() const {
    if (sessions_.empty()) return -1;
    return sessions_.begin()->first;
}
