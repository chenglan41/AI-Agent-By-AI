// jpg.cpp - JPEG compressor implementation
// Simplified wrapper that uses Windows GDI+ for JPEG compression
#include "jpg.h"
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <iostream>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace jpg {

// Helper to get encoder CLSID
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    
    ImageCodecInfo* pImageCodecInfo = NULL;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    
    pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;
    
    GetImageEncoders(num, size, pImageCodecInfo);
    
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    
    free(pImageCodecInfo);
    return -1;
}

bool compress_image(int width, int height, const unsigned char* rgb_data,
                    std::vector<unsigned char>& jpeg_output,
                    const compress_params& params) {
    // 防御无效参数，避免 GDI+ 对空数据/0尺寸崩溃
    if (width <= 0 || height <= 0 || rgb_data == NULL) {
        std::cerr << "[ShotDBG] compress_image: bad params" << std::endl;
        return false;
    }
    
    std::cerr << "[ShotDBG] compress_image start " << width << "x" << height << std::endl;
    
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        std::cerr << "[ShotDBG] GdiplusStartup failed" << std::endl;
        return false;
    }
    std::cerr << "[ShotDBG] GdiplusStartup ok" << std::endl;
    
    bool success = false;
    
    // Convert RGB to BGR for GDI+
    // GDI+ PixelFormat24bppRGB 要求每行 stride 为 4 字节对齐，否则 Bitmap 无效
    int stride = ((width * 3 + 3) / 4) * 4;
    std::vector<unsigned char> bgr_data(stride * height, 0);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int srcIdx = (y * width + x) * 3;
            int dstIdx = y * stride + x * 3;
            bgr_data[dstIdx + 0] = rgb_data[srcIdx + 2]; // B
            bgr_data[dstIdx + 1] = rgb_data[srcIdx + 1]; // G
            bgr_data[dstIdx + 2] = rgb_data[srcIdx + 0]; // R
        }
    }
    std::cerr << "[ShotDBG] bgr convert done stride=" << stride << std::endl;
    
    // 关键修复：Bitmap 必须放在花括号作用域内，
    // 确保它在 GdiplusShutdown 之前完成析构。
    // 否则函数返回时 Bitmap 析构时 GDI+ 已关闭，会崩溃。
    {
        Bitmap bitmap(width, height, stride, PixelFormat24bppRGB, bgr_data.data());
        std::cerr << "[ShotDBG] bitmap status=" << bitmap.GetLastStatus() << std::endl;
        
        if (bitmap.GetLastStatus() == Ok) {
            // Get JPEG encoder
            CLSID encoderClsid;
            if (GetEncoderClsid(L"image/jpeg", &encoderClsid) >= 0) {
                std::cerr << "[ShotDBG] jpeg encoder found" << std::endl;
                
                // Create IStream for memory
                IStream* stream = NULL;
                CreateStreamOnHGlobal(NULL, TRUE, &stream);
                
                if (stream) {
                    // Set quality parameter
                    EncoderParameters encoderParams;
                    encoderParams.Count = 1;
                    encoderParams.Parameter[0].Guid = EncoderQuality;
                    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
                    encoderParams.Parameter[0].NumberOfValues = 1;
                    ULONG quality = params.m_quality;
                    encoderParams.Parameter[0].Value = &quality;
                    
                    // Save to stream
                    Status status = bitmap.Save(stream, &encoderClsid, &encoderParams);
                    std::cerr << "[ShotDBG] bitmap.Save status=" << status << std::endl;
                    
                    if (status == Ok) {
                        // Get stream size
                        STATSTG stat;
                        stream->Stat(&stat, STATFLAG_NONAME);
                        std::cerr << "[ShotDBG] jpeg stream size=" << (long long)stat.cbSize.QuadPart << std::endl;
                        
                        // Read data from stream
                        jpeg_output.resize((size_t)stat.cbSize.QuadPart);
                        LARGE_INTEGER pos;
                        pos.QuadPart = 0;
                        stream->Seek(pos, STREAM_SEEK_SET, NULL);
                        
                        ULONG bytesRead;
                        stream->Read(jpeg_output.data(), (ULONG)jpeg_output.size(), &bytesRead);
                        std::cerr << "[ShotDBG] stream read bytes=" << bytesRead << std::endl;
                        
                        success = true;
                    }
                    
                    stream->Release();
                } else {
                    std::cerr << "[ShotDBG] CreateStreamOnHGlobal failed" << std::endl;
                }
            } else {
                std::cerr << "[ShotDBG] jpeg encoder not found" << std::endl;
            }
        }
    } // bitmap 在这里析构（GDI+ 仍存活）
    
    // Cleanup GDI+
    GdiplusShutdown(gdiplusToken);
    std::cerr << "[ShotDBG] GdiplusShutdown ok" << std::endl;
    
    return success;
}

} // namespace jpg
