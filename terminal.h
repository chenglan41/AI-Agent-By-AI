// terminal.h - ConPTY Terminal Manager
#ifndef TERMINAL_H
#define TERMINAL_H

#include <windows.h>
#include <string>
#include <map>

// Define missing structures for older MinGW
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

#ifndef EXTENDED_STARTUPINFO_PRESENT
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#endif

// STARTUPINFOEXA is already defined in MinGW-w64, no need to redefine

struct TerminalSession {
    int id;
    HANDLE hProcess;
    HANDLE hThread;
    HANDLE hInPipe;
    HANDLE hOutPipe;
    void* hConPTY; // HPCON
    std::string buffer;
    bool active;
};

class TerminalManager {
public:
    TerminalManager();
    ~TerminalManager();
    
    // Create a new terminal session
    int createProcess();
    
    // Remove a terminal session
    std::string removeProcess(int terminalId);
    
    // Send input to terminal
    std::string inputProcess(int terminalId, const std::string& command);
    
    // Get terminal output buffer
    std::string outputProcess(int terminalId);
    
    // Get first session id (-1 if no session exists)
    int getFirstSessionId() const;

private:
    std::map<int, TerminalSession> sessions_;
    int nextId_;
    
    // ConPTY function pointers (for dynamic loading)
    typedef HRESULT (WINAPI *CreatePseudoConsole_t)(
        COORD size, HANDLE hInput, HANDLE hOutput, DWORD dwFlags, void** phPC);
    typedef void (WINAPI *ClosePseudoConsole_t)(void* hPC);
    typedef HRESULT (WINAPI *ResizePseudoConsole_t)(void* hPC, COORD size);
    
    // ProcThreadAttribute function pointers
    typedef BOOL (WINAPI *InitializeProcThreadAttributeList_t)(
        PVOID lpAttributeList, DWORD dwAttributeCount, DWORD dwFlags, PSIZE_T lpSize);
    typedef BOOL (WINAPI *UpdateProcThreadAttribute_t)(
        PVOID lpAttributeList, DWORD dwFlags, DWORD_PTR Attribute,
        PVOID lpValue, SIZE_T cbSize, PVOID lpPreviousValue, PSIZE_T lpReturnSize);
    typedef BOOL (WINAPI *DeleteProcThreadAttributeList_t)(PVOID lpAttributeList);
    
    CreatePseudoConsole_t pCreatePseudoConsole;
    ClosePseudoConsole_t pClosePseudoConsole;
    ResizePseudoConsole_t pResizePseudoConsole;
    
    InitializeProcThreadAttributeList_t pInitializeProcThreadAttributeList;
    UpdateProcThreadAttribute_t pUpdateProcThreadAttribute;
    DeleteProcThreadAttributeList_t pDeleteProcThreadAttributeList;
    
    HMODULE hKernel32;
    
    bool loadConPTY();
};

#endif // TERMINAL_H
