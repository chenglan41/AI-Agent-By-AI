// screenshot.cpp - Screenshot capture implementation
#include "screenshot.h"
#include "base64.h"
#include "jpg.h" // jpeg-compressor header
#include <iostream>

bool Screenshot::captureScreen(int x, int y, int width, int height,
                               std::vector<unsigned char>& bmpData,
                               int& bmpWidth, int& bmpHeight, int& bmpStride) {
    // 防御无效参数
    if (width <= 0 || height <= 0) {
        std::cerr << "[ShotDBG] captureScreen: bad size " << width << "x" << height << std::endl;
        return false;
    }
    
    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) {
        std::cerr << "[ShotDBG] GetDC failed" << std::endl;
        return false;
    }
    std::cerr << "[ShotDBG] GetDC ok" << std::endl;
    
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    if (!hMemDC) {
        std::cerr << "[ShotDBG] CreateCompatibleDC failed" << std::endl;
        ReleaseDC(NULL, hScreenDC);
        return false;
    }
    std::cerr << "[ShotDBG] CreateCompatibleDC ok" << std::endl;
    
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    if (!hBitmap) {
        std::cerr << "[ShotDBG] CreateCompatibleBitmap failed" << std::endl;
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hScreenDC);
        return false;
    }
    std::cerr << "[ShotDBG] CreateCompatibleBitmap ok" << std::endl;
    
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);
    if (!hOldBitmap) {
        std::cerr << "[ShotDBG] SelectObject failed" << std::endl;
        DeleteObject(hBitmap);
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hScreenDC);
        return false;
    }
    std::cerr << "[ShotDBG] SelectObject ok" << std::endl;
    
    // Capture screen (CAPTUREBLT 让分层窗口/硬件加速窗口也能被截到)
    BitBlt(hMemDC, 0, 0, width, height, hScreenDC, x, y, SRCCOPY | CAPTUREBLT);
    std::cerr << "[ShotDBG] BitBlt done" << std::endl;
    
    // Get bitmap info
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    bmpWidth = width;
    bmpHeight = height;
    bmpStride = ((width * 3 + 3) / 4) * 4; // align to 4 bytes
    bmpData.resize(bmpStride * height);
    
    // Get bitmap bits
    int lines = GetDIBits(hMemDC, hBitmap, 0, height, bmpData.data(), &bmi, DIB_RGB_COLORS);
    std::cerr << "[ShotDBG] GetDIBits lines=" << lines << std::endl;
    
    // Cleanup
    SelectObject(hMemDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    
    // GetDIBits 失败返回 0，说明拿不到像素，报失败
    if (lines == 0) {
        std::cerr << "[ShotDBG] GetDIBits returned 0" << std::endl;
        return false;
    }
    
    std::cerr << "[ShotDBG] captureScreen ok" << std::endl;
    return true;
}

bool Screenshot::bmpToJpeg(const std::vector<unsigned char>& bmpData,
                           int width, int height, int stride,
                           std::vector<unsigned char>& jpegData,
                           int quality) {
    std::cerr << "[ShotDBG] bmpToJpeg start " << width << "x" << height << " stride=" << stride << std::endl;
    
    // Convert BGR to RGB
    std::vector<unsigned char> rgbData(width * height * 3);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int srcIdx = y * stride + x * 3;
            int dstIdx = (y * width + x) * 3;
            rgbData[dstIdx + 0] = bmpData[srcIdx + 2]; // R
            rgbData[dstIdx + 1] = bmpData[srcIdx + 1]; // G
            rgbData[dstIdx + 2] = bmpData[srcIdx + 0]; // B
        }
    }
    std::cerr << "[ShotDBG] bmpToJpeg rgb convert done" << std::endl;
    
    // Compress to JPEG using jpeg-compressor
    jpg::compress_params params;
    params.m_quality = quality;
    
    std::vector<unsigned char> output;
    bool result = jpg::compress_image(width, height, rgbData.data(), output, params);
    std::cerr << "[ShotDBG] compress_image result=" << (result ? "true" : "false") << std::endl;
    
    if (result) {
        jpegData = output;
    }
    return result;
}

bool Screenshot::captureAsJpeg(std::vector<unsigned char>& jpegData, int quality) {
    // Get virtual screen geometry:
    // SM_X/Y_VIRTUALSCREEN are top-left corner of the virtual desktop,
    // SM_CX/CY_VIRTUALSCREEN are its full width/height.
    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    
    std::cerr << "[ShotDBG] captureAsJpeg geometry: x=" << screenX << " y=" << screenY
              << " w=" << screenWidth << " h=" << screenHeight << std::endl;
    
    std::vector<unsigned char> bmpData;
    int bmpWidth, bmpHeight, bmpStride;
    
    if (!captureScreen(screenX, screenY, screenWidth, screenHeight, bmpData, 
                       bmpWidth, bmpHeight, bmpStride)) {
        std::cerr << "[ShotDBG] captureScreen failed" << std::endl;
        return false;
    }
    
    return bmpToJpeg(bmpData, bmpWidth, bmpHeight, bmpStride, jpegData, quality);
}

std::string Screenshot::captureAsBase64(int quality) {
    std::cerr << "[ShotDBG] captureAsBase64 start" << std::endl;
    
    std::vector<unsigned char> jpegData;
    if (!captureAsJpeg(jpegData, quality)) {
        std::cerr << "[ShotDBG] captureAsJpeg failed" << std::endl;
        return "";
    }
    
    std::cerr << "[ShotDBG] jpeg size=" << jpegData.size() << std::endl;
    
    std::string result = base64::encode(jpegData);
    std::cerr << "[ShotDBG] base64 done size=" << result.size() << std::endl;
    return result;
}
