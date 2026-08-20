// mouse.cpp - Mouse simulation implementation
#include "mouse.h"
#include <cmath>
#include <cstdlib>
#include <ctime>

std::string MouseController::click(int x, int y, int duration_ms) {
    // Move cursor to position
    if (!SetCursorPos(x, y)) {
        return "Failed to move cursor";
    }
    
    // Mouse down
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    
    // Wait
    if (duration_ms > 0) {
        Sleep(duration_ms);
    }
    
    // Mouse up
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    
    return "ok";
}

std::string MouseController::drag(int x1, int y1, int x2, int y2) {
    // Move to start position
    if (!SetCursorPos(x1, y1)) {
        return "Failed to move cursor";
    }
    Sleep(50);
    
    // Mouse down
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    Sleep(50);
    
    // Human-like movement
    humanMove(x1, y1, x2, y2);
    
    Sleep(50);
    
    // Mouse up
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    
    return "ok";
}

void MouseController::humanMove(int x1, int y1, int x2, int y2) {
    // Calculate distance
    double dx = x2 - x1;
    double dy = y2 - y1;
    double distance = sqrt(dx * dx + dy * dy);
    
    if (distance < 1) return;
    
    // Number of steps based on distance
    int steps = (int)(distance / 5.0);
    if (steps < 5) steps = 5;
    if (steps > 50) steps = 50;
    
    // Use bezier curve for more natural movement
    srand((unsigned int)time(NULL));
    
    // Control points for bezier curve
    double cx1 = x1 + dx * 0.25 + (rand() % 20 - 10);
    double cy1 = y1 + dy * 0.25 + (rand() % 20 - 10);
    double cx2 = x1 + dx * 0.75 + (rand() % 20 - 10);
    double cy2 = y1 + dy * 0.75 + (rand() % 20 - 10);
    
    for (int i = 0; i <= steps; i++) {
        double t = (double)i / steps;
        
        // Cubic bezier
        double t2 = t * t;
        double t3 = t2 * t;
        double mt = 1 - t;
        double mt2 = mt * mt;
        double mt3 = mt2 * mt;
        
        int px = (int)(mt3 * x1 + 3 * mt2 * t * cx1 + 3 * mt * t2 * cx2 + t3 * x2);
        int py = (int)(mt3 * y1 + 3 * mt2 * t * cy1 + 3 * mt * t2 * cy2 + t3 * y2);
        
        SetCursorPos(px, py);
        
        // Variable delay for more natural movement
        int delay = 5 + (rand() % 10);
        Sleep(delay);
    }
}
