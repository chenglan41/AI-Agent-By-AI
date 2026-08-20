// keyboard.h - Keyboard simulation
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <windows.h>
#include <string>

class KeyboardController {
public:
    // Simulate key press
    static std::string press(const std::string& key, int duration_ms = 50);
    
    // Simulate key combination (e.g., Ctrl+C)
    static std::string combo(const std::string& key1, const std::string& key2, int duration_ms = 50);
    
private:
    // Convert key string to virtual key code
    static WORD keyToVK(const std::string& key);
};

#endif // KEYBOARD_H
