// keyboard.cpp - Keyboard simulation implementation
#include "keyboard.h"
#include <map>
#include <algorithm>

// Define missing VK_OEM_* constants for older MinGW
#ifndef VK_OEM_PLUS
#define VK_OEM_PLUS 0xBB
#endif
#ifndef VK_OEM_COMMA
#define VK_OEM_COMMA 0xBC
#endif
#ifndef VK_OEM_MINUS
#define VK_OEM_MINUS 0xBD
#endif
#ifndef VK_OEM_PERIOD
#define VK_OEM_PERIOD 0xBE
#endif

WORD KeyboardController::keyToVK(const std::string& key) {
    // Convert to uppercase for comparison
    std::string upperKey = key;
    std::transform(upperKey.begin(), upperKey.end(), upperKey.begin(), ::toupper);
    
    // Special keys
    if (upperKey == "ENTER" || upperKey == "RETURN") return VK_RETURN;
    if (upperKey == "ESC" || upperKey == "ESCAPE") return VK_ESCAPE;
    if (upperKey == "TAB") return VK_TAB;
    if (upperKey == "SPACE") return VK_SPACE;
    if (upperKey == "BACKSPACE" || upperKey == "BACK") return VK_BACK;
    if (upperKey == "DELETE" || upperKey == "DEL") return VK_DELETE;
    if (upperKey == "UP") return VK_UP;
    if (upperKey == "DOWN") return VK_DOWN;
    if (upperKey == "LEFT") return VK_LEFT;
    if (upperKey == "RIGHT") return VK_RIGHT;
    if (upperKey == "HOME") return VK_HOME;
    if (upperKey == "END") return VK_END;
    if (upperKey == "PAGEUP" || upperKey == "PGUP") return VK_PRIOR;
    if (upperKey == "PAGEDOWN" || upperKey == "PGDN") return VK_NEXT;
    if (upperKey == "INSERT" || upperKey == "INS") return VK_INSERT;
    if (upperKey == "F1") return VK_F1;
    if (upperKey == "F2") return VK_F2;
    if (upperKey == "F3") return VK_F3;
    if (upperKey == "F4") return VK_F4;
    if (upperKey == "F5") return VK_F5;
    if (upperKey == "F6") return VK_F6;
    if (upperKey == "F7") return VK_F7;
    if (upperKey == "F8") return VK_F8;
    if (upperKey == "F9") return VK_F9;
    if (upperKey == "F10") return VK_F10;
    if (upperKey == "F11") return VK_F11;
    if (upperKey == "F12") return VK_F12;
    if (upperKey == "CTRL" || upperKey == "CONTROL") return VK_CONTROL;
    if (upperKey == "ALT") return VK_MENU;
    if (upperKey == "SHIFT") return VK_SHIFT;
    if (upperKey == "WIN" || upperKey == "WINDOWS") return VK_LWIN;
    
    // Single character
    if (key.length() == 1) {
        char c = key[0];
        if (c >= 'a' && c <= 'z') return (WORD)(c - 'a' + 0x41);
        if (c >= 'A' && c <= 'Z') return (WORD)(c - 'A' + 0x41);
        if (c >= '0' && c <= '9') return (WORD)c;
        
        // Special characters
        switch (c) {
            case ';': return VK_OEM_1;
            case '=': return VK_OEM_PLUS;
            case ',': return VK_OEM_COMMA;
            case '-': return VK_OEM_MINUS;
            case '.': return VK_OEM_PERIOD;
            case '/': return VK_OEM_2;
            case '`': return VK_OEM_3;
            case '[': return VK_OEM_4;
            case '\\': return VK_OEM_5;
            case ']': return VK_OEM_6;
            case '\'': return VK_OEM_7;
        }
    }
    
    return 0;
}

std::string KeyboardController::press(const std::string& key, int duration_ms) {
    WORD vk = keyToVK(key);
    if (vk == 0) {
        return "Unknown key: " + key;
    }
    
    // Key down
    keybd_event(vk, 0, 0, 0);
    
    // Wait
    if (duration_ms > 0) {
        Sleep(duration_ms);
    }
    
    // Key up
    keybd_event(vk, 0, KEYEVENTF_KEYUP, 0);
    
    return "ok";
}

std::string KeyboardController::combo(const std::string& key1, const std::string& key2, int duration_ms) {
    // ========== 防自杀硬拦截（第一层） ==========
    // ctrl+c / ctrl+break / ctrl+pause 会向控制台进程组发送中断信号，
    // 直接杀死 Agent 自身进程，导致任务中断。
    // 代码层硬拦截：无论模型怎么调用，这类组合键一律拒绝执行，
    // 并提示改用 terminal_remove 或向终端发送 exit。
    std::string up1 = key1;
    std::string up2 = key2;
    std::transform(up1.begin(), up1.end(), up1.begin(), ::toupper);
    std::transform(up2.begin(), up2.end(), up2.begin(), ::toupper);
    
    bool hasCtrl = (up1 == "CTRL" || up1 == "CONTROL" || up2 == "CTRL" || up2 == "CONTROL");
    bool hasKill = (up1 == "C" || up2 == "C" ||
                    up1 == "BREAK" || up2 == "BREAK" ||
                    up1 == "PAUSE" || up2 == "PAUSE");
    if (hasCtrl && hasKill) {
        return "Blocked: this key combo (ctrl+c / ctrl+break / ctrl+pause) would kill the Agent process itself. "
               "To stop a program inside a terminal, use terminal_remove to close that terminal, "
               "or send the 'exit' command via terminal_input instead.";
    }
    
    WORD vk1 = keyToVK(key1);
    WORD vk2 = keyToVK(key2);
    
    if (vk1 == 0) return "Unknown key: " + key1;
    if (vk2 == 0) return "Unknown key: " + key2;
    
    // First key down
    keybd_event(vk1, 0, 0, 0);
    Sleep(20);
    
    // Second key down
    keybd_event(vk2, 0, 0, 0);
    
    // Wait
    if (duration_ms > 0) {
        Sleep(duration_ms);
    }
    
    // Second key up
    keybd_event(vk2, 0, KEYEVENTF_KEYUP, 0);
    Sleep(20);
    
    // First key up
    keybd_event(vk1, 0, KEYEVENTF_KEYUP, 0);
    
    return "ok";
}
