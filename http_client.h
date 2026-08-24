// http_client.h - CURL HTTP client wrapper
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>
#include <map>

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    // Set default headers
    void setHeader(const std::string& key, const std::string& value);
    
    // Enable/disable debug output
    void setDebug(bool enabled);
    
    // Set request timeout in seconds (applied to both POST and GET)
    // 非法值（<=0）回退为默认 120 秒
    void setTimeout(long seconds);
    
    // POST request with JSON body
    std::string post(const std::string& url, const std::string& jsonBody);
    
    // GET request
    std::string get(const std::string& url);
    
    // Get last error
    std::string getLastError() const;
    
    // Get last HTTP status code
    long getLastStatusCode() const;
    
    // Get last response (for debugging)
    std::string getLastResponse() const;

private:
    std::map<std::string, std::string> headers_;
    std::string lastError_;
    std::string lastResponse_;
    long lastStatusCode_;
    long timeout_;   // 请求超时（秒），默认 120
    bool debug_;
    void* curl_; // CURL handle
};

#endif // HTTP_CLIENT_H
