// jpg.h - JPEG compressor wrapper
// Based on jpeg-compressor by Rich Geldreich
#ifndef JPG_H
#define JPG_H

#include <vector>

namespace jpg {

struct compress_params {
    int m_quality;
    int m_compression_level;
    
    compress_params() : m_quality(85), m_compression_level(1) {}
};

// Compress RGB image data to JPEG
// Returns true on success
bool compress_image(int width, int height, const unsigned char* rgb_data,
                    std::vector<unsigned char>& jpeg_output,
                    const compress_params& params = compress_params());

} // namespace jpg

#endif // JPG_H
