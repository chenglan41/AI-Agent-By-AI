// mouse.h - Mouse simulation
#ifndef MOUSE_H
#define MOUSE_H

#include <windows.h>
#include <string>

class MouseController {
public:
    // Simulate mouse click at coordinates
    static std::string click(int x, int y, int duration_ms = 100);
    
    // Simulate mouse drag from (x1,y1) to (x2,y2)
    static std::string drag(int x1, int y1, int x2, int y2);
    
private:
    // Move mouse with human-like curve
    static void humanMove(int x1, int y1, int x2, int y2);
};

#endif // MOUSE_H
