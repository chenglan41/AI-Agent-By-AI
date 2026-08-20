// base64.h - Base64 encoding/decoding
#ifndef BASE64_H
#define BASE64_H

#include <string>
#include <vector>

namespace base64 {

static const std::string chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

inline std::string encode(const unsigned char* data, size_t len) {
    std::string ret;
    ret.reserve(((len + 2) / 3) * 4);
    
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = ((unsigned int)data[i]) << 16;
        if (i + 1 < len) n |= ((unsigned int)data[i + 1]) << 8;
        if (i + 2 < len) n |= data[i + 2];
        
        ret += chars[(n >> 18) & 0x3F];
        ret += chars[(n >> 12) & 0x3F];
        ret += (i + 1 < len) ? chars[(n >> 6) & 0x3F] : '=';
        ret += (i + 2 < len) ? chars[n & 0x3F] : '=';
    }
    return ret;
}

inline std::string encode(const std::string& s) {
    return encode((const unsigned char*)s.c_str(), s.size());
}

inline std::string encode(const std::vector<unsigned char>& v) {
    return encode(v.data(), v.size());
}

inline std::vector<unsigned char> decode(const std::string& s) {
    std::vector<unsigned char> ret;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[chars[i]] = i;
    
    int val = 0, bits = -8;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        bits += 6;
        if (bits >= 0) {
            ret.push_back((unsigned char)((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return ret;
}

} // namespace base64

#endif // BASE64_H
