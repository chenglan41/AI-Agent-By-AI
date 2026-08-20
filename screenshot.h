// screenshot.h - Screenshot capture using GDI
#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <windows.h>
#include <vector>
#include <string>

// Forward declare jpeg compressor functions
namespace jpg {
    class jdwp;
}

class Screenshot {
public:
    // Capture screen to BMP data
    static bool captureScreen(int x, int y, int width, int height, 
                              std::vector<unsigned char>& bmpData,
                              int& bmpWidth, int& bmpHeight, int& bmpStride);
    
    // Convert BMP to JPEG using jpeg-compressor
    static bool bmpToJpeg(const std::vector<unsigned char>& bmpData,
                          int width, int height, int stride,
                          std::vector<unsigned char>& jpegData,
                          int quality = 85);
    
    // Capture screen and return as JPEG bytes
    static bool captureAsJpeg(std::vector<unsigned char>& jpegData, 
                              int quality = 85);
    
    // Capture screen and return as base64 encoded JPEG
    static std::string captureAsBase64(int quality = 85);
};

#endif // SCREENSHOT_H
